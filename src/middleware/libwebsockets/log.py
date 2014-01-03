import os,sys
import time
import zmq

uri = "tcp://127.0.0.1:54328"
channel = "foo"

context = zmq.Context()
s = context.socket(zmq.PUB)
s.bind(uri)
n = 1
while True:
    s.send_multipart([channel, "log %d" % (n)])
    time.sleep(1.0)
    n += 1
