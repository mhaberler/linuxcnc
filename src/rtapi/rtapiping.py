
import zmq
import time
from message_pb2 import Container
from types_pb2 import *
print "ZMQ=%s pyzmq=%s" % (zmq.zmq_version(), zmq.pyzmq_version())

context = zmq.Context()

s = context.socket(zmq.DEALER)
s.setsockopt(zmq.IDENTITY, "ping-client")

s.connect("tcp://127.0.0.1:10043")

while True:
    r = Container()
    r.type = MT_RTAPI_APP_PING
    buffer = r.SerializeToString()
    print "---send:\n", str(r)
    s.send(buffer)
    msg = s.recv()
    c = Container()
    c.ParseFromString(msg)
    print "---recv: ", str(c)
    time.sleep(1)
