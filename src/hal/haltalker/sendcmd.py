
import zmq
import time
from message_pb2 import Container
from types_pb2 import *
from test_pb2 import *

print "ZMQ=%s pyzmq=%s" % (zmq.zmq_version(), zmq.pyzmq_version())

context = zmq.Context()

s = context.socket(zmq.DEALER)
s.setsockopt(zmq.IDENTITY, "fooclient")

s.connect("tcp://127.0.0.1:10042")

while True:
    r = Container()
    r.type = MT_TEST1
    t = r.test1
    t.op = LINE
    e = t.end
    e.tran.x = 1
    e.tran.y = 2
    e.tran.z = 3
    e.a = 3.1415
    buffer = r.SerializeToString()
    print "---send:\n", str(r)
    s.send(buffer)
    time.sleep(1)
