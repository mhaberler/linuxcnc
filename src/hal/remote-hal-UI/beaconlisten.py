import time

from pyczmq import zmq, zctx, zsocket, zstr, zbeacon, zframe
from message_pb2 import Container
from types_pb2 import *

BEACON_PORT = 10042;


ctx = zctx.new()


#  Create beacon to lookup service
beacon = zbeacon.new(ctx, BEACON_PORT)
zbeacon.noecho(beacon)
zbeacon.subscribe(beacon, '')
zbeacon.set_interval(beacon, 3000)
beacon_socket = zbeacon.socket(beacon)
zsocket.set_rcvtimeo(beacon_socket, 500)

r = Container()
r.type = MT_SERVICE_PROBE
zbeacon.publish(beacon, r.SerializeToString())

while True:

    ipaddress = zstr.recv(beacon_socket)
    content = zframe.recv(beacon_socket)
    if not content:
        continue
    r.ParseFromString(zframe.data(content))

    print "got ip=%s msg=%s" % (ipaddress, str(r))
    zframe.destroy(content)

del beacon
del ctx
