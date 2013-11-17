#!/usr/bin/env python

import os,sys
from gladevcp.hal_widgets import _HalWidgetBase
import gtk.gdk
import gtk
import glib
import gobject
import hal_glib
import hal  # for HAL_BIT etc
import time

debug = 0


class HandlerClass:

    def on_hal_hscale1_value_changed(self,w,data=None):
        global debug
        if debug: print "on_hscale_value_changed()",w.name, w.hal_pin.get()


    def on_hal_togglebutton1_toggled(self,w,data=None):
        global debug
        if debug: print "on_toggle()",w.name,w.hal_pin.get()


        # note '_' - this method will not be visible to the widget tree
    def _on_example_trigger_change(self,pin,userdata=None):
        print "pin %s changed to: %s" % (pin.name,pin.get())

    def _hal_disconnected(self, widget):
        self.status.set_text("Disconnected")
        self.statusbox.modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("red"))

    def _hal_connected(self, widget):
        self.status.set_text("Connected")
        self.statusbox.modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("#6FFF00"))

    def _protocol_error(self, gobject,userdata=None):
        print "proto error:",userdata,gobject


    def _protocol_status(self, gobject,cstate,sstate):
        print "proto status:",cstate,sstate,gobject

    def __init__(self, halcomp,builder,useropts,name):
        print "-----",halcomp
        self.halcomp = halcomp
        self.builder = builder
        #  self.name = name
        self.example_trigger = halcomp.newpin('example-trigger', hal.HAL_BIT, hal.HAL_IN)
        #self.example_trigger.connect('value-changed', self._on_example_trigger_change)
        self.example_trigger.connect('hal-pin-changed', self._on_example_trigger_change)

        self.status = self.builder.get_object('status')
        #print dir(self.status) #self.status.signal_names()

        self.statusbox = self.builder.get_object('statusbox')
        halcomp.connect('hal-connected',self._hal_connected)
        halcomp.connect('hal-disconnected',self._hal_disconnected)
        halcomp.connect('protocol-error',self._protocol_error)
        halcomp.connect('protocol-status',self._protocol_status)


def get_handlers(halcomp,builder,useropts,name):
    return [HandlerClass(halcomp,builder,useropts,name)]


