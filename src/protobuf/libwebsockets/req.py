import os,sys
import time
import zmq

debug = 1
uri = "tcp://127.0.0.1:54327"
context = zmq.Context()
s = context.socket(zmq.REQ)
s.connect(uri)
n = 1
while True:
    s.send("msg %d" % (n))
    reply = s.recv()
    print "reply=",reply
    time.sleep(1.0)
    n += 1
