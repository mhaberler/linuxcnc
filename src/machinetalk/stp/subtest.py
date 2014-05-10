import zmq
from message_pb2 import Container
import binascii

print "ZMQ=%s pyzmq=%s" % (zmq.zmq_version(), zmq.pyzmq_version())

context = zmq.Context()
socket = context.socket(zmq.SUB)
socket.connect("tcp://127.0.0.1:6042")
socket.setsockopt(zmq.SUBSCRIBE, "group1")
socket.setsockopt(zmq.SUBSCRIBE, "group2")

# non-existent group - provoke error message
socket.setsockopt(zmq.SUBSCRIBE, "xyzzy")


while True:
    c = Container()
    (channel, msg) = socket.recv_multipart()
    c.ParseFromString(msg)
    print "topic=%s message:\n%s" % (channel, str(c))
#    print "encoded:", binascii.hexlify(c.SerializeToString())
