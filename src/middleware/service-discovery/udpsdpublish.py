#
#  service announcement example
#
#  wait for probe broadcasts
#  directed reply with a list of service announcements
#


import socket
from message_pb2 import Container
from types_pb2 import *

BROADCAST_IP = "255.255.255.255"
SERVICE_DISCOVERY_PORT  = 10042

# some fake announcements
HAL_RCOMP_VERSION = 1
STP_VERSION  = 2

# assume ports already set post bind:
hal_rcomp_cmd_port = 62110
hal_rcomp_update_port = 62111
hal_status_update_port = 64123

instance = 1

rx = Container()
tx = Container()

# listen for broadcasts
rxsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# allow multiple listeners
rxsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(rxsock, "SO_REUSEPORT"):
    rxsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
rxsock.bind((BROADCAST_IP, SERVICE_DISCOVERY_PORT))

# unicast reply socket
txsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

while True:
    content, source = rxsock.recvfrom(1024)
    if source is None:
        # interrupted - ^C
        break

    if source and content:
        rx.ParseFromString(content)
        print "got source=%s msg=%s" % (source, str(rx))

        if rx.type == MT_SERVICE_PROBE:
            tx.Clear()

            # describe all services available
            tx.type = MT_SERVICE_ANNOUNCEMENT

            sa = tx.service_announcement.add()
            sa.stype = ST_HAL_RCOMP_COMMAND
            sa.instance = instance
            sa.version = HAL_RCOMP_VERSION
            sa.port = hal_rcomp_cmd_port

            sa.stype = ST_HAL_RCOMP_STATUS
            sa.instance = instance
            sa.version = HAL_RCOMP_VERSION
            sa.port = hal_rcomp_update_port

            sa = tx.service_announcement.add()
            sa.stype = ST_STP
            sa.update_port = hal_status_update_port
            sa.instance = instance
            sa.version = STP_VERSION

            pkt = tx.SerializeToString()

            # reply by unicast to originator
            print "reply to=%s size=%d %s" % (str(source), len(pkt), str(tx))
            txsock.sendto(pkt, (source[0], SERVICE_DISCOVERY_PORT))

        rx.Clear()
