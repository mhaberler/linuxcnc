import zmq
import time
import sys

context = zmq.Context()

socket = context.socket(zmq.ROUTER)
socket.bind("tcp://127.0.0.1:5700")

count = 0
while True:
    msg = socket.recv_multipart()
    sep		= msg.index( '' )
    cli		= msg[:sep]
    request	= msg[sep+1:]

    print "cli: ", cli
    print "request: ", request
    reply = "reply to %s" % (request)

    socket.send_multipart(cli +  [reply])
