#
#  service announcement example
#
#  wait for probe beacon broadcasts
#  reply with a list of service announcements
#


from pyczmq import zmq, zctx, zsocket, zstr, zbeacon, zframe
from message_pb2 import Container
from types_pb2 import *

BEACON_PORT = 10042;
HAL_RCOMP_VERSION = 1
STP_VERSION  = 2

ctx = zctx.new()

#  Create some services and bind to ephemeral ports
hal_rcomp_cmd  = zsocket.new(ctx, zmq.ROUTER)
hal_rcomp_cmd_port = zsocket.bind(hal_rcomp_cmd, "tcp://*:*")

hal_rcomp_update  = zsocket.new(ctx, zmq.XPUB)
hal_rcomp_update_port = zsocket.bind(hal_rcomp_update, "tcp://*:*")

hal_status_update  = zsocket.new(ctx, zmq.XPUB)
hal_status_update_port = zsocket.bind(hal_status_update, "tcp://*:*")


#  Create service discovery listener
sd_beacon = zbeacon.new (ctx, BEACON_PORT)

# receive any message (using broadcast receive only)
zbeacon.subscribe(sd_beacon, '')

beacon_socket = zbeacon.socket(sd_beacon)
rx = Container()
tx = Container()

while True:

    ipaddress = zstr.recv(beacon_socket)
    if ipaddress is None:
        # interrupted - ^C
        break
    content = zframe.recv(beacon_socket)

    if ipaddress and content:
        rx.ParseFromString(zframe.data(content))
        print "got ip=%s msg=%s" % (ipaddress, str(rx))

        if rx.type == MT_SERVICE_PROBE:
            tx.Clear()

            # describe all services available
            tx.type = MT_SERVICE_ANNOUNCEMENT

            a = tx.service_announcement.add()
            a.stype = ST_HAL_RCOMP
            a.instance = 0
            a.version = HAL_RCOMP_VERSION
            a.cmd_port = hal_rcomp_cmd_port
            a.update_port = hal_rcomp_update_port

            # b = tx.service_announcement.add()
            # b.stype = ST_STP
            # b.update_port = hal_status_update_port
            # b.instance = 0
            # b.version = STP_VERSION

            pkt = tx.SerializeToString()

            # reply by unicast to originator
            zbeacon.send(sd_beacon, ipaddress, pkt)
            print "reply: size=%d %s" % (len(pkt), str(tx))

        zframe.destroy(content)
        rx.Clear()

del sd_beacon
del ctx
