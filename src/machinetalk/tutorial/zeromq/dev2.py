import sys
import zmq

context = zmq.Context()

s1 = context.socket(zmq.ROUTER)
s2 = context.socket(zmq.DEALER)
s1.setsockopt(zmq.IDENTITY, 'dev2-router')
s2.setsockopt(zmq.IDENTITY, 'dev2-dealer')
s1.bind('tcp://*:12345')
s2.connect('tcp://127.0.0.1:5700')
zmq.device(zmq.QUEUE, s1, s2)
