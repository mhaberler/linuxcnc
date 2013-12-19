
import zmq
from message_pb2 import Container
print "ZMQ=%s pyzmq=%s" % (zmq.zmq_version(), zmq.pyzmq_version())

context = zmq.Context()
socket = context.socket(zmq.SUB)
socket.connect("tcp://127.0.0.1:5550")
socket.setsockopt(zmq.SUBSCRIBE, "pb2")
socket.setsockopt(zmq.SUBSCRIBE, "json")

while True:
    c = Container()
    (channel, msg) = socket.recv_multipart()
    if channel == "json":
        print "channel=%s message:\n%s" % (channel, msg)
    else:
        c.ParseFromString(msg)
        print "channel=%s message:\n%s" % (channel, str(c))
