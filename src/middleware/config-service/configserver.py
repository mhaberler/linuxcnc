import os,sys,time,uuid
from stat import *
import zmq
import threading
import pybonjour
import socket
import sdiscover
import netifaces

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
                 services={}, interface="", ipv4=""):
        threading.Thread.__init__(self)
        self.inifile = inifile
        self.services = services
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

        self.txtrec = pybonjour.TXTRecord({'dsname' : self.dsname })

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

        for k,v in self.services.iteritems():
            sa = self.tx.service_announcement.add()
            sa.instance = 0 # FIXME
            sa.stype = k
            sa.version = v.version
            sa.uri = v.uri
            sa.api = v.api
            sa.description = v.description

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
            self.sdref = pybonjour.DNSServiceRegister(regtype = '_machinekit._tcp',
                                                      name = self.name,
                                                      port = self.port,
                                                      #host = self.host,
                                                      txtRecord = self.txtrec,
                                                      callBack = register_callback)
            self.sd = self.sdref.fileno()
        except:
            print 'cannot register DNS service'
        else:
            poll.register(self.sd, zmq.POLLIN)

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

    # delete localhost type interfaces
    netifs = [i for i in interfaces if not i.startswith('lo')]

    # find a match in preference oder
    for p in pref:
        for i in netifs:
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

    prefs = ['wlan','eth','usb']

    iface = choose_ip(prefs)
    if not iface:
       print >> sys.stderr, "failed to determine preferred interface (preference = %s)" % prefs
       sys.exit(1)

    if debug:
        print "announcing configserver on ",iface

    sd = sdiscover.ServiceDiscover(trace=trace)
    sd.add(ST_STP_HALGROUP)
    sd.add(ST_STP_HALRCOMP)
    sd.add(ST_HAL_RCOMMAND)
    sd.add(ST_RTAPI_COMMAND)
    sd.add(ST_LOGGING)
    #sd.add(ST_WEBSOCKET)

    result = sd.discover()
    if not result:
        print >> sys.stderr,"failed to discover all requested services"
        sys.exit(1)
    else:
        if False:
            for k,v in result.iteritems():
                if debug:
                    print "service", k, v.uri, v.description, v.api

    context = zmq.Context()
    context.linger = 0

    uri = "tcp://" + iface[0]

    cfg = ConfigServer(context, uri, "apps.ini",
                       topdir=".", services=result,
                       interface = iface[0],
                       ipv4 = iface[1])
    cfg.setDaemon(True)
    cfg.start()

    time.sleep(300)


if __name__ == "__main__":
    main()
