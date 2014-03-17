import os,sys,time,uuid
import zmq
from message_pb2 import Container
from types_pb2 import *

class GetApp:

    def __init__(self, context, uri):
        self.socket = context.socket(zmq.DEALER)
        self.socket.identity = "fooclient"
        self.socket.connect(uri)
        self.socket.RCVTIMEO = 2000
        self.rx = Container()
        self.tx = Container()
        self.run()

    def request(self, type):
        self.tx.type = type
        self.socket.send(self.tx.SerializeToString())
        self.tx.Clear()

    def list_apps(self):
        self.request(MT_LIST_APPLICATIONS)
        reply = self.socket.recv()
        if reply:
             self.rx.ParseFromString(reply)
             print "list apps response", str(self.rx)
             return self.rx.app
        else:
            raise "reply timeout"

    def get_app(self, name):
        print "get_app", name
        a = self.tx.app.add()
        a.name = name
        self.request(MT_RETRIEVE_APPLICATION)
        reply = self.socket.recv()
        if reply:
             self.rx.ParseFromString(reply)
             print "get_app response", str(self.rx)
             return self.rx.app
        else:
            raise "reply timeout"

    def run(self):
        apps = self.list_apps()
        for a in apps:
            self.get_app(a.name)

context = zmq.Context()
context.linger = 0
uri = "tcp://127.0.0.1:5590"
getapp = GetApp(context, uri)
