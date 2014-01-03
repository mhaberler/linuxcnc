
import zmq
import time

from message_pb2 import Container
from types_pb2 import *

print "ZMQ=%s pyzmq=%s" % (zmq.zmq_version(), zmq.pyzmq_version())

context = zmq.Context()

sub = context.socket(zmq.XSUB)
sub.connect( "tcp://127.0.0.1:4712")
#"tcp://127.0.0.1:6650")
#sub.setsockopt(zmq.SUBSCRIBE, "")

sub.send("\001topic")
while True:
    sub.send("foobarbaz")
    time.sleep(1)

    # (channel, msg) = sub.recv_multipart()
    # c = Container()
    # c.ParseFromString(msg)
    # for s in c.signal:
    #     if not s.handle in signals:
    #         signals[s.handle] = Sig()
    #     sig = signals[s.handle]
    #     if s.HasField("name"):   # a full report conveying signal names
    #         sig.name = s.name
    #     if s.HasField("halbit"):
    #         sig.value = s.halbit
    #     if s.HasField("hals32"):
    #         sig.value = s.hals32
    #     if s.HasField("halu32"):
    #         sig.value = s.halu32
    #     if s.HasField("halfloat"):
    #         sig.value = s.halfloat

    #     print channel,sig.name,sig.value,s.handle
