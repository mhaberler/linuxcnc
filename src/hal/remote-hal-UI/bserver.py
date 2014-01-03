import time

from pyczmq import zmq, zctx, zsocket, zstr, zbeacon, zframe
from message_pb2 import Container
from types_pb2 import *

BEACON_PORT = 10042;


ctx = zctx.new()

beacon = zbeacon.new(ctx, BEACON_PORT)
zbeacon.no_echo(beacon)
zbeacon.subscribe(client_beacon, '')
beacon_socket = zbeacon.socket(beacon)


rx = Container()
tx = Container()

while True:

    ipaddress = zstr.recv(beacon_socket)
    content = zframe.recv(beacon_socket)
    r.ParseFromString(zframe.data(content))
    zframe.destroy(content)
    if r.type == MT_SERVICE_PROBE:
        tx.type = MT_SERVICE_ANNOUNCEMENT
        a = tx.service_announcement
        a.type = ST_HAL_RCOMP
        a.cmd_port = 4711

    if r.type == MT_SERVICE_REQUEST:
        tx.type = MT_SERVICE_ANNOUNCEMENT

    print "got ip=%s msg=%s" % (ipaddress, str(r))

del client_beacon
del ctx
