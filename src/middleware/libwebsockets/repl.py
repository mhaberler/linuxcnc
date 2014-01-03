import os,sys
import time
import zmq

debug = 1
uri = "tcp://127.0.0.1:54327"
context = zmq.Context()
s = context.socket(zmq.REP)
s.bind(uri)
n = 1
while True:
    req = s.recv()
    print "req=",req
    s.send("msg %d" % (n))
    time.sleep(1.0)
    n += 1
