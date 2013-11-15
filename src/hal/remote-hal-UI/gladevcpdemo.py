#!/usr/bin/env python

import os,sys
from gladevcp.hal_widgets import _HalWidgetBase
import gtk.gdk
import gtk
import glib
import gobject

import zmq
import time

from message_pb2 import Container
from types_pb2 import *

debug = 1


class HandlerClass:

    def on_hal_hscale1_value_changed(self,w,data=None):
        global debug
        if debug: print "on_hscale_value_changed()",w.name, w.hal_pin.get()


    def on_hal_togglebutton1_toggled(self,w,data=None):
        global debug
        if debug: print "on_toggle()",w.name,w.hal_pin.get()

    # # ------------------ private -----

    # def _update_readable(self,source,condition):
    #     while self.update.getsockopt(zmq.EVENTS) & zmq.POLLIN:
    #         try:
    #             (comp,msg) = self.update.recv_multipart()
    #             if msg:
    #                 self.rhal.status_update(comp, msg)
    #         except zmq.ZMQError as e:
    #             if e.errno != zmq.EAGAIN:
    #                 raise
    #     return gtk.TRUE

    # def _cmd_readable(self,source,condition):
    #     while self.cmd.getsockopt(zmq.EVENTS) & zmq.POLLIN:
    #         try:
    #             msg = self.cmd.recv(flags=zmq.NOBLOCK)
    #             if msg:
    #                 self.rhal.server_message(msg)
    #         except zmq.ZMQError as e:
    #             if e.errno != zmq.EAGAIN:
    #                 raise
    #     return gtk.TRUE

    # def _tick(self):
    #     self.rhal.timer_tick()
    #     return True # re-arm

    def pinlist(self):
        # fake UI widget information
        # returns tuple (compname, pinlist)
        wl = []
        for k,v in self.widgets.iteritems():
            wl.append((k,HAL_BIT, HAL_OUT))
        return wl

    def halchange(self,p):
        print "halchange", p.name, str(p)

    def report_error(self, text):
        pass

    def __init__(self, halcomp,builder,useropts,name):
        print "-----",halcomp
        self.halcomp = halcomp
        self.builder = builder
        self.name = name
        #self.widgets = dict()
        # for w in builder.get_objects():
        #     if not isinstance(w, gtk.Widget):
        #         continue
        #     name = gtk.Buildable.get_name(w) #gtk bug
        #     if isinstance(w, _HalWidgetBase): # and w.hal_pin.is_pin():
        #         print  name #, dir(w) #, w.hal_pin.get_name()
        #         self.widgets[name] = w


        # cmd_uri="tcp://127.0.0.1:4711"
        # update_uri="tcp://127.0.0.1:4712"

        # self.context = zmq.Context()
        # self.update = self.context.socket(zmq.SUB)
        # self.update.connect(update_uri)
        # self.update.setsockopt(zmq.SUBSCRIBE, '')

        # self.update_notifier = gtk.input_add(self.update.getsockopt(zmq.FD),
        #                                      gtk.gdk.INPUT_READ,
        #                                      self._update_readable)

        # self.cmd = self.context.socket(zmq.DEALER)
        # self.cmd.set(zmq.IDENTITY,"gladevcp-%d" % os.getpid())
        # self.cmd.connect(cmd_uri)
        # self.cmd_notifier = gtk.input_add(self.cmd.getsockopt(zmq.FD),
        #                                   gtk.gdk.INPUT_READ,
        #                                   self._cmd_readable)

        # self.rhal = RcompClient(retrieve_pinlist_cb=self.pinlist,
        #                         halchange_cb=self.halchange,
        #                         error_cb=self.report_error)

        # self.rhal.bind(self.name, self.pinlist())
        # self.rhal.value = 3.14

        # # dead peer detection
        # glib.timeout_add_seconds(1, self._tick)


def get_handlers(halcomp,builder,useropts,name):
    return [HandlerClass(halcomp,builder,useropts,name)]


#--- HALrcomp client side: protocol support

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

    def timer_tick(self):
        print "tick"
        pass

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
        self.pinsbyhandle = {}
        self.pinsbyname = {}

        pass

        # self.ctx = zmq.Context()
        # self.client_id = "rcomp-client%d" % os.getpid()

        # self.cmd = self.ctx.socket(zmq.DEALER)
        # self.cmd.set(zmq.IDENTITY,self.client_id)
        # self.cmd.connect(cmd_uri)

        # self.update = self.ctx.socket(zmq.XSUB)
        # self.update.connect(update_uri)

        # self.poller = zmq.Poller()
        # self.poller.register(self.cmd, zmq.POLLIN)
        # self.poller.register(self.update, zmq.POLLIN)



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
