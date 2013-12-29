#
#  service discovery example
#
# broadcast the probe beacon
# collect directed answers
#
import time

from pyczmq import zmq, zctx, zsocket, zstr, zbeacon, zframe
from message_pb2 import Container
from types_pb2 import *

SERVICE_DISCOVERY_PORT  = 10042;

# shopping list of services needed
#required = [ST_STP]

required = [ST_RTAPI_COMMAND]
#required = [ST_HAL_RCOMP, ST_STP]

# services learned
known = dict()

# give up if not all services aquired after max_wait
max_wait = 2.0


ctx = zctx.new()

#  Create service discovery listener
sd_lookup = zbeacon.new (ctx, SERVICE_DISCOVERY_PORT)

# turn on unicast receive
zbeacon.unicast(sd_lookup, 1)

# publish MT_SERVICE_PROBE every second until all services
# collected, or giving up
zbeacon.set_interval(sd_lookup, 1000)

# receive any message
zbeacon.subscribe(sd_lookup, '')

# ignore own queries
zbeacon.noecho(sd_lookup)


tx = Container()
tx.type = MT_SERVICE_PROBE

zbeacon.publish(sd_lookup, tx.SerializeToString())

beacon_socket = zbeacon.socket(sd_lookup)
done = False
start = time.time()

while not done:
    if (time.time() - start) > max_wait:
        break
    ipaddress = zstr.recv(beacon_socket)
    if zctx.interrupted:
        break
    if ipaddress is None:
        continue

    content = zframe.recv(beacon_socket)
    rx = Container()
    rx.ParseFromString(zframe.data(content))

    if rx.type == MT_SERVICE_ANNOUNCEMENT:
        for a in rx.service_announcement:
            if a.stype in required:
                print "aquiring:", a.stype
                known[a.stype] = a
                required.remove(a.stype)
            if not required:
                zbeacon.silence(sd_lookup)
                done = True

    zframe.destroy(content)

if not required:
    print "all services aquired:"

    for k,v in known.iteritems():
        print "service %d : %s" % (k, str(v))
else:
    print "timeout - some services missing:", required
