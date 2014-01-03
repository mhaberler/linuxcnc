
import zmq
import time
from message_pb2 import Container
from types_pb2 import *

print "ZMQ=%s pyzmq=%s" % (zmq.zmq_version(), zmq.pyzmq_version())

context = zmq.Context()
socket = context.socket(zmq.XPUB)
socket.setsockopt(zmq.XPUB_VERBOSE, 1)

socket.bind("tcp://127.0.0.1:6650")

while True:
    msg = socket.recv()
    print msg

    # r = Container()
    # r.type = MT_HALUPDATE
    # buffer = r.SerializeToString()
    # socket.send_multipart(["foo",buffer])
    # time.sleep(1)
