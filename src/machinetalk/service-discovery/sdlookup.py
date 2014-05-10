#
#  service discovery example - UDP version
#
# broadcast the probe request
# collect and display directed answers
#
import time
import socket
import select
from optparse import OptionParser

from message_pb2 import Container
from types_pb2 import *

parser = OptionParser()
parser.add_option("-v", "--verbose", action="store_true", dest="verbose",
                  help="print answers as they come in")
(options, args) = parser.parse_args()

SERVICE_DISCOVERY_PORT  = 10042
BROADCAST_IP = "255.255.255.255"

# probe retry interval, msec
RETRY = 500

# look for MAX_WAIT seconds for all responders
MAX_WAIT = 2.0

# looking for instance 0 services
instance = 0

# services learned
known = dict()

# prepare the probe frame
tx = Container()
tx.type = MT_SERVICE_PROBE
tx.trace = True # make responders log this request
probe = tx.SerializeToString()

# socket to broadcast service discovery request on
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(sock, "SO_REUSEPORT"):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
# enable broadcast tx
# else 'permission denied'
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

poller = select.poll()
poller.register(sock.fileno(),select.POLLIN)

# initial probe request
sock.sendto(probe, (BROADCAST_IP, SERVICE_DISCOVERY_PORT))

start = time.time()
while True:
    if (time.time() - start) > MAX_WAIT:
        break

    ready = poller.poll(RETRY)
    if ready:
        for fd,event in ready:
            if event and select.POLLIN:
                content, source  = sock.recvfrom(1024)
                if source is None:
                    continue

                rx = Container()
                rx.ParseFromString(content)

                if rx.type == MT_SERVICE_PROBE:
                    # skip our requests
                    continue

                if options.verbose:
                    print "got reply=%s msg=%s" % (source, str(rx))

                if rx.type == MT_SERVICE_ANNOUNCEMENT:
                    for a in rx.service_announcement:
                        known[a.stype] = a # last responder wins

    else:
        if options.verbose: print "resending probe:"
        sock.sendto(probe, (BROADCAST_IP, SERVICE_DISCOVERY_PORT))

print "--------- services found:"
for k,v in known.iteritems():
    print "service %d : %s" % (k, str(v))
