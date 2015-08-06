import zmq
import time
import sys,os

context = zmq.Context()

socket = context.socket(zmq.DEALER)
socket.identity = "client-pid%d" % os.getpid()
socket.connect("tcp://127.0.0.1:12345")

for i in range(100):
    socket.send_multipart(['', "foo","request %d" % i])
    print socket.recv()
    time.sleep(1)
