import os,sys,time,uuid
from stat import *
import zmq
import threading
import pybonjour
import socket
import sdiscover
import netifaces
import uuid

import ConfigParser

from message_pb2 import Container
from config_pb2 import *
from types_pb2 import *

def register_callback(sdRef, flags, errorCode, name, regtype, domain):
    if errorCode == pybonjour.kDNSServiceErr_NoError:
        print 'Registered service:'
        print '  name    =', name
        print '  regtype =', regtype
        print '  domain  =', domain

class ConfigServer(threading.Thread):

    def __init__(self, context, uri, inifile,  topdir=".",
                 interface="", ipv4="", svc_uuid=None):
        threading.Thread.__init__(self)
        self.inifile = inifile
        self.interface = interface
        self.ipv4 = ipv4

        self.cfg = ConfigParser.ConfigParser()
        self.cfg.read(self.inifile)
        # print "apps:", self.cfg.sections()
        # for n in self.cfg.sections():
        #     print "comment:", self.cfg.get(n, 'comment')
        #     print "type:", self.cfg.getint(n, 'type')

        self.topdir = topdir
        self.context = context
        self.socket = context.socket(zmq.ROUTER)
        self.port = self.socket.bind_to_random_port(uri)

        self.dsname = self.socket.get_string(zmq.LAST_ENDPOINT, encoding='utf-8')
        print "dsname = ", self.dsname, "port =",self.port
        me = uuid.uuid1()
        self.txtrec = pybonjour.TXTRecord({'dsn' : self.dsname,
                                           'uuid': svc_uuid,
                                           'service' : 'config',
                                           'instance' : str(me) })
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
                file = app.file.add()
                file.name = str(f)
                file.encoding = CLEARTEXT
                file.blob = str(buffer)

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
        try:

            self.host = socket.gethostname()
            self.name = 'Machinekit on %s' % self.ipv4
            self.sdref = pybonjour.DNSServiceRegister(regtype = '_machinekit._tcp,_config',
                                                      name = self.name,
                                                      port = self.port,
                                                      #host = self.host,
                                                      txtRecord = self.txtrec,
                                                      callBack = register_callback)
            self.sd = self.sdref.fileno()
        except:
            print 'cannot register DNS service'

        try:
            while True:
                s = dict(poll.poll(1000))
                if self.socket in s:
                    self.process(self.socket)
                if self.sd in s:
                    pybonjour.DNSServiceProcessResult(self.sdref)
        except KeyboardInterrupt:
            self.sd.close()

def choose_ip(pref):
    '''
    given an interface preference list, return a tuple (interface, IPv4)
    or None if no match found
    If an interface has several IPv4 addresses, the first one is picked.
    pref is a list of interface names or prefixes:

    pref = ['eth0','usb3']
    or
    pref = ['wlan','eth', 'usb']
    '''

    # retrieve list of network interfaces
    interfaces = netifaces.interfaces()

    # find a match in preference oder
    for p in pref:
        for i in interfaces:
            if i.startswith(p):
                ifcfg = netifaces.ifaddresses(i)
                # we want the first IPv4 address
                try:
                    ip = ifcfg[netifaces.AF_INET][0]['addr']
                except KeyError:
                    continue
                return (i, ip)
    return None


def main():
    debug = True
    trace = False
    uuid = os.getenv("MKUUID")
    if uuid is None:
        print >> sys.stderr, "no MKUUID environemnt variable set"
        print >> sys.stderr, "run export MKUUID=`uuidgen` first"
        sys.exit(1)

    prefs = ['wlan','eth','usb']

    iface = choose_ip(prefs)
    if not iface:
       print >> sys.stderr, "failed to determine preferred interface (preference = %s)" % prefs
       sys.exit(1)

    if debug:
        print "announcing configserver on ",iface


    context = zmq.Context()
    context.linger = 0

    uri = "tcp://" + iface[0]

    cfg = ConfigServer(context, uri, "apps.ini",
                       svc_uuid=uuid,
                       topdir=".",
                       interface = iface[0],
                       ipv4 = iface[1])
    cfg.setDaemon(True)
    cfg.start()

    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
