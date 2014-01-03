import zmq
import os
from message_pb2 import Container
from types_pb2 import *

context = zmq.Context()
socket = context.socket(zmq.DEALER)
socket.setsockopt(zmq.IDENTITY,  "hal-ui%d" % os.getpid() )
socket.connect("tcp://127.0.0.1:4711")

r = Container()
r.type = MT_HALCOMP_CREATE
comp = r.comp
comp.name = "testcomp"

p = r.pin.add()
p.name = "%s.button1" % comp.name
p.type = HAL_BIT
p.dir  = HAL_OUT

p = r.pin.add()
p.name = "%s.led1" % comp.name
p.type = HAL_BIT
p.dir  = HAL_IN

socket.send(r.SerializeToString())

c = Container()
c.ParseFromString(socket.recv())
assert c.type == MT_HALCOMP_CREATE_CONFIRM
