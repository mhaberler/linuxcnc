import time

from pyczmq import zmq, zctx, zsocket, zstr, zbeacon, zframe
from message_pb2 import Container
from types_pb2 import *

BEACON_PORT = 10042;

HAL_RCOMP_VERSION = 1

ctx = zctx.new()

#  Create a service socket and bind to an ephemeral port
service = zsocket.new(ctx, zmq.ROUTER)
port_nbr = zsocket.bind(service, "tcp://*:*")

#  Create beacon to broadcast our service
a = Container()
a.type =  MT_SERVICE_PROBE
# a.service_type = ST_HAL_RCOMP
# a.port = port_nbr
# a.version = HAL_RCOMP_VERSION
# a.instance = 0
# a.note = "see if this works"
pkt = a.SerializeToString()

service_beacon = zbeacon.new(ctx, BEACON_PORT)
zbeacon.set_interval(service_beacon, 1000)
zbeacon.publish(service_beacon, pkt)

time.sleep(120)


del service_beacon
del ctx
