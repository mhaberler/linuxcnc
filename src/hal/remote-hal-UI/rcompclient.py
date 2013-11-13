# example HALrcomp client
#
# interaction:
# setup: pass in socket URI's for command & status, ping interval
#
# callbacks:
#
#    retrieve pin list:
#       retrieve list of name/type/direction tuples during startup
#       use case:
#
#    HAL per-pin change callback: name, type, value
#       halserver reports pin change in HAL
#       use case: change UI widgets, mirrored pins
#
#    UI per-pin change method: name,type, value
#       use case: pass UI activity to HAL to set pins
#       NB: actual value will be set by remote HAL change callback (too)
#
#    status callback: halserver dead/live changes
#       use case: change status icon or pin
#
#    error: pass protocol error messages
#       use case: popups - typically a config or programming error
#
# TODO: protocol state tracking
#

import zmq
import time
import os

from message_pb2 import Container
from types_pb2 import *

class PinStatus:

    def update(self, pin):
        if pin.type == HAL_FLOAT:
            self.halfloat = pin.halfloat
        if pin.type == HAL_BIT:
            self.halbit = pin.halbit
        if pin.type == HAL_S32:
            self.hals32 = pin.hals32
        if pin.type == HAL_U32:
            self.halu32 = pin.hals32
        if self.callback:
            self.callback(self)

    def __init__(self, pin, callback=None):
        print "PinStatus pin=",str(pin)
        self.callback = callback
        self.name = pin.name
        self.type = pin.type
        self.dir = pin.dir
        self.handle = pin.handle
        if callback:
            self.update(pin)



class RcompClient:

    def bind(self, cname, pinlist):
        # This message MUST carry a Component submessage.
        # The Component submessage MUST carry the name field.
        # This message MAY carry repeated Pin messages.
        # Any Pin message present MUST carry the name, type and dir fields.

        c = Container()
        c.type = MT_HALRCOMP_BIND
        comp = c.comp
        comp.name = cname
        for (pname,ptype,pdir) in pinlist:
            pin = c.pin.add()
            pin.name = cname +  "." + pname
            pin.type = ptype
            pin.dir = pdir

        if self.debug: print "bind:", str(c)
        self.cmd.send(c.SerializeToString())

    def status_update(self, comp, msg):
        s = Container()
        s.ParseFromString(msg)

        if self.debug: print "status_update ", comp, str(s)

        if s.type == MT_HALRCOMP_STATUS:
            # full update
            for p in s.pin:
                ps = PinStatus(p, self.halchange_cb)
                self.pinsbyname[str(p.name)] = ps
                self.pinsbyhandle[p.handle] = ps
            return

        if s.type == MT_HALRCOMP_PIN_CHANGE:
            # incremental update
            for p in s.pin:
                if not self.pinsbyhandle.has_key(p.handle):
                    self.error_cb("status_update: no such handle: %d " % (p.handle))
                    return
                ps = self.pinsbyhandle[p.handle]
                ps.update(p) # reflect new pin value in UI
            return

        self.error_cb("status_update: unknown message type: %d " % (s.type))

    def server_message(self, msg):
        r = Container()
        r.ParseFromString(msg)
        if self.debug: print "server_message " + str(r)

        if r.type == MT_PING_ACKNOWLEDGE:
            pass
        if r.type == MT_HALRCOMP_BIND_CONFIRM:
            #self.update.setsockopt(zmq.SUBSCRIBE, self.compname)
            self.update.send("\001" + self.compname)
        if r.type == MT_HALRCOMP_BIND_REJECT:
            self.error_cb("bind rejected: %s" % (s.note))


    def __init__(self,
                 cmd_uri="tcp://127.0.0.1:4711",
                 update_uri="tcp://127.0.0.1:4712",
                 retrieve_pinlist_cb=None,
                 halchange_cb=None,
                 error_cb=None,
                 msec=2000,
                 useping=True,
                 debug=False):

        self.halchange_cb = halchange_cb
        self.error_cb = error_cb
        self.debug = debug


        self.ctx = zmq.Context()
        self.client_id = "rcomp-client%d" % os.getpid()

        self.cmd = self.ctx.socket(zmq.DEALER)
        self.cmd.set(zmq.IDENTITY,self.client_id)
        self.cmd.connect(cmd_uri)

        self.update = self.ctx.socket(zmq.XSUB)
        self.update.connect(update_uri)

        self.poller = zmq.Poller()
        self.poller.register(self.cmd, zmq.POLLIN)
        self.poller.register(self.update, zmq.POLLIN)

        self.pinsbyhandle = {}
        self.pinsbyname = {}

        (self.compname, pinlist) = retrieve_pinlist_cb()
        self.bind(self.compname,pinlist)
        self.value = 3.14

        done = False
        while not done:
            sockets = dict(self.poller.poll(timeout=msec))
            # reply on command socket received
            if self.cmd in sockets and sockets[self.cmd] == zmq.POLLIN:
                msg = self.cmd.recv()
                self.server_message(msg)

            # handle update messages
            if self.update in sockets and sockets[self.update] == zmq.POLLIN:
                (comp,msg) = self.update.recv_multipart()
                self.status_update(comp, msg)

            if not sockets:
                print "timer event"
                # fake a pin change
                self.pin_change("demo.spinbutton", self.value)
                self.value += 2.718



    def pin_change(self, name, value):
        pdesc = self.pinsbyname[name]
        print pdesc.name, pdesc.type
        c = Container()
        c.type = MT_HALRCOMP_SET_PINS

        # This message MUST carry a Pin message for each pin which has
        # changed value since the last message of this type.
        # Each Pin message MUST carry the handle field.
        # Each Pin message MAY carry the name field.
        # Each Pin message MUST - depending on pin type - carry a halbit,
        # halfloat, hals32, or halu32 field.
        pin = c.pin.add()

        pin.handle = pdesc.handle
        if pdesc.type == HAL_FLOAT:
            pin.halfloat = value
        if pdesc.type == HAL_BIT:
            pin.halbit = value
        if pdesc.type == HAL_S32:
            pin.hals32 = value
        if pdesc.type == HAL_U32:
            pin.halu32 = value

        self.cmd.send(c.SerializeToString())


def pinlist():
    # fake UI widget information
    # returns tuple (compname, pinlist)
    return  ("demo",[('button', HAL_BIT, HAL_OUT),
             ('spinbutton', HAL_FLOAT, HAL_OUT),
             ('led',    HAL_BIT, HAL_IN),
             ('speed',  HAL_FLOAT, HAL_IN)])

def halchange(p):
    print "halchange", p.name, str(p)

def report_error():
    pass

rc = RcompClient(retrieve_pinlist_cb=pinlist,
                 halchange_cb=halchange,
                 error_cb=report_error)
