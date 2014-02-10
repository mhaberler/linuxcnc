import zmq
import time

context = zmq.Context()
socket = context.socket(zmq.XPUB)
socket.setsockopt(zmq.XPUB_VERBOSE, 1)

socket.bind("tcp://127.0.0.1:6650")
i = 0

while True:
    socket.send_multipart(["topic1","message %d" % i])
    socket.send_multipart(["topic2","message %d" % i])
    time.sleep(0.5)
    i += 1
