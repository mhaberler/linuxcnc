import zmq
import threading
import time
import sys
import os
import binascii
import google.protobuf.text_format
import pb2json # automagic pb2/json/pb2 conversion
import json    # pretty printer
import halext

from types_pb2 import *
from object_pb2 import *
from message_pb2 import *
from halupdate_pb2 import *

class HalUpdate(threading.Thread):
    """HalUpdate per-peer protocol handler"""
    def __init__(self, context, identity,state=0):
        self.identity = identity
        self.state = state
        self.context = context
        self.lastheard = time.time()
        threading.Thread.__init__ (self)

    def run(self):
        proto = self.context.socket(zmq.DEALER)
        proto.setsockopt(zmq.IDENTITY,  self.identity )
        proto.connect('inproc://backend')
        print 'Peer started', self.identity
        while True:
            _id = proto.recv()
            msg = proto.recv()
            print 'Proto:%s received %s from %s' % (self.identity,msg, _id)
            if msg == "SHUTDOWN":
                proto.send_multipart( [_id, "SHUTDOWN CONFIRM %d" % self.state])
                break
            else:
                proto.send_multipart( [_id, "state:%d" % self.state])
            self.state += 1
            del msg
        print "proto: peer ", self.identity, "exiting"
        proto.close()

class ServerTask(threading.Thread):
    """ServerTask"""
    def __init__(self):
        threading.Thread.__init__ (self)

    def run(self):
        context = zmq.Context()
        frontend = context.socket(zmq.ROUTER)
        frontend.bind('tcp://*:5555')

        backend = context.socket(zmq.DEALER)
        backend.bind('inproc://backend')

        peers = {}

        poll = zmq.Poller()
        poll.register(frontend, zmq.POLLIN)
        poll.register(backend,  zmq.POLLIN)

        while True:
            sockets = dict(poll.poll())
            if frontend in sockets:
                if sockets[frontend] == zmq.POLLIN:
                    peerid = frontend.recv()
                    msg = frontend.recv()
                    # print 'Server received %s id %s\n' % (msg, peerid)
                    if not peerid in peers:
                        proto = HalUpdate(context, peerid)
                        proto.start()
                        peers[peerid] = proto
                        print "new peer:", peerid

                    backend.send(peerid, zmq.SNDMORE)
                    backend.send(msg)
            if backend in sockets:
                if sockets[backend] == zmq.POLLIN:
                    peerid = backend.recv()
                    msg = backend.recv()
                    # print 'Sending to frontend %s id %s\n' % (msg, _id)
                    frontend.send(peerid, zmq.SNDMORE)
                    frontend.send(msg)

        frontend.close()
        backend.close()
        context.term()


def main():
    """main function"""
    server = ServerTask()
    server.start()
    server.join()


if __name__ == "__main__":
    main()
