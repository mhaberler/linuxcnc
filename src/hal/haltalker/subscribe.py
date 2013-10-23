
import zmq
from message_pb2 import Container
from types_pb2 import *

print "ZMQ=%s pyzmq=%s" % (zmq.zmq_version(), zmq.pyzmq_version())

context = zmq.Context()

sub = context.socket(zmq.SUB)
sub.connect("tcp://127.0.0.1:6650")
sub.setsockopt(zmq.SUBSCRIBE, "")

class Sig:
    def __init__(self):
        pass
    #self.value = None
    #self.name = None

# handle -> value
signals = {}


while True:
    (channel, msg) = sub.recv_multipart()
    c = Container()
    c.ParseFromString(msg)
    for s in c.signal:
        if not s.handle in signals:
            signals[s.handle] = Sig()
        sig = signals[s.handle]
        if s.name:   # a full a.port conveying signal names
            sig.name = s.name
        if hasattr(s,"halbit"):
            sig.value = s.halbit
        if hasattr(s,"hals32"):
            sig.value = s.hals32
        if hasattr(s,"halu32"):
            sig.value = s.halu32
        if hasattr(s,"halfloat"):
            sig.value = s.halfloat

        if hasattr(sig,"name"):
            print "channel=%s %s=%s" % (channel,sig.name,sig.value)
