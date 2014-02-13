#
#  service discovery example - UDP version
#
# broadcast the probe request
# collect directed answers
#
import time
import socket
import select

from message_pb2 import Container
from types_pb2 import *

SERVICE_DISCOVERY_PORT  = 10042
BROADCAST_IP = "255.255.255.255"
UDP_ANY = "0.0.0.0"

# probe retry interval, msec
RETRY = 500

# give up if not all services aquired after max_wait
MAX_WAIT = 3.0

# shopping list of services needed
required = [ST_HAL_RCOMP_COMMAND,ST_HAL_RCOMP_STATUS]

# looking for instance 0 services
instance = 0

# services learned
known = dict()

# prepare the probe frame
tx = Container()
tx.type = MT_SERVICE_PROBE
probe = tx.SerializeToString()


# socket to broadcast service discovery request on
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(sock, "SO_REUSEPORT"):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
# enable broadcast tx
# else 'permission denied'
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

start = time.time()
poller = select.poll()
poller.register(sock.fileno(),select.POLLIN)

# initial probe request
sock.sendto(probe, (BROADCAST_IP, SERVICE_DISCOVERY_PORT))

while required:
    if (time.time() - start) > MAX_WAIT:
        break

    ready = poller.poll(RETRY)
    if ready:
        for fd,event in ready:
            if event and select.POLLIN:
                content, source  = sock.recvfrom(1024)

                if source is None:
                    continue

                rx = Container()
                rx.ParseFromString(content)

                if rx.type == MT_SERVICE_PROBE:
                    # skip requests
                    continue

                print "got reply=%s msg=%s" % (source, str(rx))

                if rx.type == MT_SERVICE_ANNOUNCEMENT:
                    for a in rx.service_announcement:
                        if a.stype in required:
                            if a.instance == instance:
                                print "aquiring:", a.stype
                                known[a.stype] = a
                                required.remove(a.stype)
                            else:
                                print "service %d instance: %d" %(a.stype, a.instance)
    else:
        print "resending probe:"
        sock.sendto(probe, (BROADCAST_IP, SERVICE_DISCOVERY_PORT))

if not required:
    print "all services aquired:"

    for k,v in known.iteritems():
        print "service %d : %s" % (k, str(v))
else:
    print "timeout - some services missing:", required
