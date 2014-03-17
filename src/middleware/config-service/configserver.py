import os,sys,time,uuid
from stat import *
import zmq
import threading

import ConfigParser

from message_pb2 import Container
from config_pb2 import *
from types_pb2 import *

class ConfigServer(threading.Thread):

    def __init__(self, context, uri, inifile,  topdir="."):
        threading.Thread.__init__(self)
        self.inifile = inifile

        self.cfg = ConfigParser.ConfigParser()
        self.cfg.read(self.inifile)
        # print "apps:", self.cfg.sections()
        # for n in self.cfg.sections():
        #     print "comment:", self.cfg.get(n, 'comment')
        #     print "type:", self.cfg.getint(n, 'type')

        self.topdir = topdir
        self.context = context
        self.socket = context.socket(zmq.ROUTER)
        self.socket.bind(uri)
        self.dsname = self.socket.get_string(zmq.LAST_ENDPOINT, encoding='utf-8')
        print "dsname = ", self.dsname
        self.rx = Container()
        self.tx = Container()


    def send_msg(self,dest, type):
        self.tx.type = type
        buffer = self.tx.SerializeToString()
        print "send_msg", str(self.tx)
        self.tx.Clear()
        self.socket.send_multipart([dest, buffer])

    def list_apps(self, origin):
        for name in self.cfg.sections():
            app = self.tx.app.add()
            app.name = name
            app.description = self.cfg.get(name, 'description')
            app.type = self.cfg.getint(name, 'type')
        self.send_msg(origin, MT_DESCRIBE_APPLICATION)

    def add_files(self, dir, app):
        print "addfiles", dir
        for f in os.listdir(dir):
            pathname = os.path.join(dir, f)
            mode = os.stat(pathname).st_mode
            if S_ISREG(mode):
                print "add", pathname
                buffer = open(pathname, 'rU').read()
                f = app.file.add()
                f.name = str(f)
                f.encoding = CLEARTEXT
                f.blob = str(buffer)

    def retrieve_app(self, origin, name):
        print "retrieve app", name
        app = self.tx.app.add()
        app.name = name
        app.description = self.cfg.get(name, 'description')
        app.type = self.cfg.getint(name, 'type')
        self.add_files(self.cfg.get(name, 'files'), app)
        self.send_msg(origin, MT_APPLICATION_DETAIL)

    def process(self,s):
        print "process called"
        try:
            (origin, msg) = s.recv_multipart()
        except Exception, e:
            print "Exception",e
            return
        self.rx.ParseFromString(msg)

        if self.rx.type == MT_LIST_APPLICATIONS:
            self.list_apps(origin)
            return

        if self.rx.type == MT_RETRIEVE_APPLICATION:
            a = self.rx.app[0]
            self.retrieve_app(origin,a.name)
        return

        note = self.tx.note.add()
        note = "unsupported request type %d" % (self.rx.type)
        self.send_msg(origin,MT_ERROR)

    def run(self):
        print "run called"
        poll = zmq.Poller()
        poll.register(self.socket, zmq.POLLIN)
        while True:
            s = dict(poll.poll(1000))
            if self.socket in s:
                self.process(self.socket)


def main():
    context = zmq.Context()
    context.linger = 0

    uri = "tcp://eth0:*"
    uri = "tcp://127.0.0.1:5590"

    cfg = ConfigServer(context, uri, "apps.ini",topdir="." )
    cfg.setDaemon(True)
    cfg.start()

    time.sleep(300)


if __name__ == "__main__":
    main()
