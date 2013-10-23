
import zmq
from message_pb2 import Container
from types_pb2 import *

print "ZMQ=%s pyzmq=%s" % (zmq.zmq_version(), zmq.pyzmq_version())

context = zmq.Context()

sub = context.socket(zmq.SUB)
sub.connect("tcp://127.0.0.1:6650")
sub.setsockopt(zmq.SUBSCRIBE, "")

# handle
names = {}

# handle -> value
values = {}


while True:
    (channel, msg) = sub.recv_multipart()
    c = Container()
    c.ParseFromString(msg)
    for s in c.signal:
        if s.name:   # a full a.port conveying signal names
            #print s.name,s.handle
            names[s.handle] = s.name
        if hasattr(s,"halbit"):
            values[s.handle] = s.halbit
        if hasattr(s,"hals32"):
            values[s.handle] = s.hals32
        if hasattr(s,"halu32"):
            values[s.handle] = s.halu32
        if hasattr(s,"halfloat"):
            values[s.handle] = s.halfloat

        if names.has_key(s.handle):
            print "channel=%s %s=%s" % (channel,names[s.handle],values[s.handle])
