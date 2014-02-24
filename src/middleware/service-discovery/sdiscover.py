#
#  service discovery module
#

import time
import socket
import select
from message_pb2 import Container
from types_pb2 import * #MT_SERVICE_PROBE,MT_SERVICE_ANNOUNCEMENT

class Service:
    def __init__(self):
        self.uri = None
        self.ipaddress = None
        self.port = -1
        self.version = 0
        self.api = None
        self.description = ""
        self.found = False

class ServiceDiscover:
    SERVICE_DISCOVERY_PORT = 10042
    BROADCAST_IP = "255.255.255.255"
    # probe retry interval, msec
    RETRY = 500
    # look for MAX_WAIT seconds for all responders
    MAX_WAIT = 2.0

    def __init__(self, port=SERVICE_DISCOVERY_PORT, instance=0,
                 retry=RETRY, maxwait=MAX_WAIT, trace=False):
        self.port = port
        self.instance = instance
        self.services = dict()
        self.replies = dict()
        self.wanted = 0
        self.retry = retry
        self.maxwait = maxwait
        self.trace = trace

        # prepare the probe frame
        tx = Container()
        tx.type = MT_SERVICE_PROBE
        tx.trace = trace  # make responders log this request
        self.probe = tx.SerializeToString()

        # socket to broadcast service discovery request on
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(self.sock, "SO_REUSEPORT"):
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)

        # enable broadcast tx
        # else 'permission denied'
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

        self.poller = select.poll()

    def add(self, service, minversion=0):
        s = Service()
        s.minversion = minversion
        self.services[service] = s
        self.wanted += 1

    def discover(self):
        self.poller.register(self.sock.fileno(),select.POLLIN)

        # initial probe request
        self.sock.sendto(self.probe, (ServiceDiscover.BROADCAST_IP, self.port))

        start = time.time()
        while self.wanted:
            if (time.time() - start) > self.maxwait:
                break
            ready = self.poller.poll(self.retry)
            if ready:
                for fd,event in ready:
                    if event and select.POLLIN:
                        content, source  = self.sock.recvfrom(8192)
                        if source is None:
                            continue

                        rx = Container()
                        rx.ParseFromString(content)

                        if rx.type == MT_SERVICE_PROBE:
                            # skip requests
                            continue

                        if self.trace:
                            print "got reply=%s msg=%s" % (source, str(rx))

                        if rx.type == MT_SERVICE_ANNOUNCEMENT:
                            for a in rx.service_announcement:
                                    s = Service()
                                    s.uri = a.uri
                                    s.ipaddress = a.ipaddress
                                    s.port = a.port
                                    s.version = a.version
                                    s.api = a.api
                                    s.description = a.description
                                    s.found = True
                                    self.replies[a.stype] = s # last responder wins
                                    if a.stype in self.services.keys():
                                        self.wanted -= 1
                                        self.services[a.stype] = s
                                    if self.wanted == 0:
                                        break

            else:
                if self.trace: print "resending probe:"
                self.sock.sendto(self.probe, (ServiceDiscover.BROADCAST_IP, self.port))
        if self.wanted == 0:
            return self.services
        else:
            return None

    def detail(self, verbose=False):
        if verbose:
            print "all services discovered:"
            for k,v in self.replies.iteritems():
                print "service", k, v.uri, v.description, v.api
        return self.replies

if __name__ == "__main__":
    # test query

    sd = ServiceDiscover(trace=True)
    sd.add(ST_RTAPI_COMMAND)
    sd.add(ST_WEBSOCKET)
    result = sd.discover()
    if result:
        print "result:"
        for k,v in result.iteritems():
            print "service", k, v.uri, v.description, v.api
    else:
        print "failed to discover all requested services"

    print
    sd.detail()
