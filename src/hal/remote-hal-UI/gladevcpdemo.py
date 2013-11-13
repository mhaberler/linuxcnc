#!/usr/bin/env python

import os,sys
from gladevcp.hal_widgets import _HalWidgetBase
import gtk.gdk
import gtk
import glib
import gobject

import zmq
import threading
import time

from message_pb2 import Container
from types_pb2 import *

debug = 1

class HandlerClass:

    def on_hscale_value_changed(self,widget,data=None):
        global debug
        if debug: print "on_hscale_value_changed()",widget.name

    def on_spinbutton_value_changed(self,widget,data=None):
        global debug
        if debug: print "on_spinbutton_value_changed()",widget.name

    def on_toggle(self,widget,data=None):
        global debug
        if debug: print "on_toggle()",widget.name

    # ------------------ private -----

    def _update_readable(self,source,condition):
        while self.update.getsockopt(zmq.EVENTS) & zmq.POLLIN:
            try:
                topic, data = self.update.recv_multipart(flags=zmq.NOBLOCK)
                u = Container()
                u.ParseFromString(data)

                print "publisher: ",topic,str(u)
            except zmq.ZMQError as e:
                if e.errno != zmq.EAGAIN:
                    raise
        return gtk.TRUE

    def _cmd_readable(self,source,condition):
        while self.cmd.getsockopt(zmq.EVENTS) & zmq.POLLIN:
            try:
                data = self.cmd.recv(flags=zmq.NOBLOCK)
                print "cmd: ",data
            except zmq.ZMQError as e:
                if e.errno != zmq.EAGAIN:
                    raise
        return gtk.TRUE

    def _tick(self):
        print "tick"
        return True # re-arm

    def __init__(self, halcomp,builder,useropts):
        self.builder = builder

        self.widgets = dict()
        for w in builder.get_objects():
            if not isinstance(w, gtk.Widget):
                continue
            name = gtk.Buildable.get_name(w) #gtk bug
            if isinstance(w, _HalWidgetBase): # and w.hal_pin.is_pin():
                print  name #, w.hal_pin.get_name()


        cmd_uri="tcp://127.0.0.1:4711"
        update_uri="tcp://127.0.0.1:4712"

        self.context = zmq.Context()
        self.update = self.context.socket(zmq.SUB)
        self.update.connect(update_uri)
        self.update.setsockopt(zmq.SUBSCRIBE, '')

        self.update_notifier = gtk.input_add(self.update.getsockopt(zmq.FD),
                                             gtk.gdk.INPUT_READ,
                                             self._update_readable)

        self.cmd = self.context.socket(zmq.DEALER)
        self.cmd.set(zmq.IDENTITY,"gladevcp-%d" % os.getpid())
        self.cmd.connect(cmd_uri)
        self.cmd_notifier = gtk.input_add(self.cmd.getsockopt(zmq.FD),
                                          gtk.gdk.INPUT_READ,
                                          self._cmd_readable)

        # dead peer detection
        glib.timeout_add_seconds(1, self._tick)


def get_handlers(halcomp,builder,useropts):
    return [HandlerClass(halcomp,builder,useropts)]
