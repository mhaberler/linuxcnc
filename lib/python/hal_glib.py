#!/usr/bin/env python
# vim: sts=4 sw=4 et

import _hal, hal, gobject
import linuxcnc
import os
import time
import zmq
import gtk.gdk
import gobject
import glib

from message_pb2 import Container
from types_pb2 import *

class GPin(gobject.GObject, hal.Pin):
    __gtype_name__ = 'GPin'
    __gsignals__ = {'value-changed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ())}

    REGISTRY = []
    UPDATE = False

    def __init__(self, *a, **kw):
        gobject.GObject.__init__(self)
        hal.Pin.__init__(self, *a, **kw)
        self._item_wrap(self._item)
        self._prev = None
        self.REGISTRY.append(self)
        self.update_start()

    def update(self):
        tmp = self.get()
        if tmp != self._prev:
            self.emit('value-changed')
        self._prev = tmp

    @classmethod
    def update_all(self):
        if not self.UPDATE:
            return
        kill = []
        for p in self.REGISTRY:
            try:
                p.update()
            except:
                kill.append(p)
                print "Error updating pin %s; Removing" % p
        for p in kill:
            self.REGISTRY.remove(p)
        return self.UPDATE

    @classmethod
    def update_start(self, timeout=100):
        if GPin.UPDATE:
            return
        GPin.UPDATE = True
        gobject.timeout_add(timeout, self.update_all)

    @classmethod
    def update_stop(self, timeout=100):
        GPin.UPDATE = False

class GComponent:
    def __init__(self, comp):
        if isinstance(comp, GComponent):
            comp = comp.comp
        self.comp = comp

    def newpin(self, *a, **kw): return GPin(_hal.component.newpin(self.comp, *a, **kw))
    def getpin(self, *a, **kw): return GPin(_hal.component.getpin(self.comp, *a, **kw))

    def exit(self, *a, **kw): return self.comp.exit(*a, **kw)

    def __getitem__(self, k): return self.comp[k]
    def __setitem__(self, k, v): self.comp[k] = v


# ------------------------------

class GRemotePin(gobject.GObject):
    __gtype_name__ = 'GRPin'
    __gsignals__ = {'value-changed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ())}

    def __init__(self, name, type, direction,change_cb):
        gobject.GObject.__init__(self)
        self.name = name
        self.type = type
        self.direction = direction
        self.value = None
        self.handle = 0
        self.change_cb = change_cb

    def set(self, value):
        print "set",self.name,value
        self.value = value
        self.emit('value-changed')
        self.change_cb(self)

    def set_remote(self, value):
        print "set remote",self.name,value
        self.value = value
        self.emit('value-changed')

    def get(self):
        return self.value

    def get_type(self):
        return self.type

    def get_dir(self):
        return self.direction

    def get_name(self):
        return self.name

    def is_pin(self):
        return True

class GRemoteComponent(gobject.GObject):
    __gtype_name__ = 'GRemoteComponent'
    __gsignals__ = {
        'hal-connected': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'hal-disconnected': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ())
        }

    CONTEXT = None
    UPDATE = None
    CMD = None
    COUNT = 0 # instance counter

    def __init__(self, name, cmd_uri, update_uri, period=3):
        gobject.GObject.__init__(self)
        self.debug = True
        self.period = period
        self.last_contact = 0
        self.ping_outstanding = False
        self.name = name
        self.pinsbyname = {}
        self.pinsbyhandle = {}
        self.prefix = ""
        if not GRemoteComponent.CONTEXT:
            ctx = zmq.Context()
            update = ctx.socket(zmq.SUB)
            update.connect(update_uri)

            cmd = ctx.socket(zmq.DEALER)
            cmd.setsockopt(zmq.IDENTITY,"%s-%d" % (name,os.getpid()))
            cmd.setsockopt(zmq.LINGER,0)
            cmd.connect(cmd_uri)
            # print "cmd_FD=",cmd.getsockopt(zmq.FD)
            # print "update_FD=",update.getsockopt(zmq.FD)
            # print type(update.getsockopt(zmq.FD))
            # print dir(update.getsockopt(zmq.FD))
            self.cmd_notify = gobject.io_add_watch(cmd.getsockopt(zmq.FD),
                                                   gobject.IO_IN,#|gobject.IO_ERR|gobject.IO_HUP,
                                                   self.zmq_readable, cmd, False)
            self.update_notify = gobject.io_add_watch(update.getsockopt(zmq.FD),
                                                      gobject.IO_IN, #|gobject.IO_ERR|gobject.IO_HUP,
                                                      self.zmq_readable, update, True)

            #GDK->gtk.gdk in GTK V 2.0
            #self.cmd_id = gtk.input_add(cmd.getsockopt(zmq.FD), gtk.gdk.INPUT_READ, self.cmd_readable)

            # self.poller = zmq.Poller()
            # self.poller.register(cmd, zmq.POLLIN)
            # self.poller.register(update, zmq.POLLIN)
            # gobject.timeout_add(100, self.pollme)
            #update.setsockopt(zmq.SUBSCRIBE, self.name)
#define ZMQ_POLLIN 1
#define ZMQ_POLLOUT 2
#define ZMQ_POLLERR 4

            GRemoteComponent.CONTEXT = ctx
            GRemoteComponent.UPDATE = update
            GRemoteComponent.CMD = cmd
            while gtk.events_pending():
                print "iter.................."
                gtk.main_iteration()
        GRemoteComponent.COUNT += 1

    def pollme(self):
        while True:
            cmd = GRemoteComponent.CMD
            update = GRemoteComponent.UPDATE
            sockets = dict(self.poller.poll(timeout=10))
            if not sockets:
                return True
            if update in sockets and sockets[update] == zmq.POLLIN:
                (comp,msg) = update.recv_multipart()
                self.status_update(comp, msg)
            if cmd in sockets and sockets[cmd] == zmq.POLLIN:
                msg = cmd.recv()
                self.server_message(msg)

    def zmq_readable(self, eventfd, condition, zsocket, is_update):
        #print "--------------------------- update", is_update
        while True:
            event = zsocket.getsockopt(zmq.EVENTS)
            #print "--------------------------- event=",event
            if event & zmq.POLLIN:

                try:
                    if is_update:
                        (comp,msg) = zsocket.recv_multipart()
                        print "--------------------------- update recv_multipart()"
                        if msg:
                            self.status_update(comp, msg)
                    else:
                        msg = zsocket.recv(flags=zmq.NOBLOCK)
                        print "--------------------------- command recv()"
                        if msg:
                            self.server_message(msg)
                except zmq.ZMQError as e:
                    print "--------------------------- update ZMQError", e
                    if e.errno != zmq.EAGAIN:
                        raise
            else:
                return True


    def _tick(self):
        self.timer_tick()
        return True # re-arm

    # --- HALrcomp protocol support ---
    def bind(self):
        c = Container()
        c.type = MT_HALRCOMP_BIND
        c.comp.name = self.name
        for pin_name,pin in self.pinsbyname.iteritems():
            p = c.pin.add()
            p.name = self.name +  "." + pin_name
            p.type = pin.get_type()
            p.dir = pin.get_dir()
        if self.debug: print "bind:" , str(c)
        GRemoteComponent.CMD.send(c.SerializeToString())

    def pin_update(self,rp,lp):
        if rp.HasField('halfloat'): lp.set_remote(rp.halfloat)
        if rp.HasField('halbit'):   lp.set_remote(rp.halbit)
        if rp.HasField('hals32'):   lp.set_remote(rp.hals32)
        if rp.HasField('halu32'):   lp.set_remote(rp.halu32)


    # process updates received on subscriber socket
    def status_update(self, comp, msg):
        print "--------------------------- message on update:"

        s = Container()
        s.ParseFromString(msg)
        if self.debug: print "status_update ", comp, str(s)

        if s.type == MT_HALRCOMP_PIN_CHANGE: # incremental update
            print "--------------------------- PIN CHANGE"

            for rp in s.pin:
                lp = self.pinsbyhandle[rp.handle]
                self.pin_update(rp,lp)
            self.emit('hal-connected')
            return

        if s.type == MT_HALRCOMP_STATUS: # full update
            print "--------------------------- FULL UPDATE"
            for rp in s.pin:
                lname = str(rp.name)
                if "." in lname: # strip comp prefix
                    cname,pname = lname.split(".",1)
                lp = self.pinsbyname[pname]
                lp.handle = rp.handle
                self.pinsbyhandle[rp.handle] = lp
                self.pin_update(rp,lp)
                self.emit('hal-connected')
            return

        print "status_update: unknown message type: %d " % (s.type)

    # process replies received on command socket
    def server_message(self, msg):
        r = Container()
        r.ParseFromString(msg)
        #if self.debug: print "server_message " + str(r)

        if r.type == MT_PING_ACKNOWLEDGE:
            self.emit('hal-connected')
            self.ping_outstanding = False
            return

        if r.type == MT_HALRCOMP_BIND_CONFIRM:
            print "--------------------------- BIND CONFIRM"
            GRemoteComponent.UPDATE.setsockopt(zmq.SUBSCRIBE, self.name)
            return

        if r.type == MT_HALRCOMP_BIND_REJECT:
            print "bind rejected: %s" % (r.note)
            return

        print "----------- UNKNOWN server message type ", r.type

    def timer_tick(self):
        if self.ping_outstanding:
            print "timeout"
            self.emit('hal-disconnected')
        c = Container()
        c.type = MT_PING
        GRemoteComponent.CMD.send(c.SerializeToString())
        self.ping_outstanding = True

    def pin_change(self, rpin):
        print "pinchange"
        c = Container()
        c.type = MT_HALRCOMP_SET_PINS

        # This message MUST carry a Pin message for each pin which has
        # changed value since the last message of this type.
        # Each Pin message MUST carry the handle field.
        # Each Pin message MAY carry the name field.
        # Each Pin message MUST - depending on pin type - carry a halbit,
        # halfloat, hals32, or halu32 field.
        pin = c.pin.add()

        pin.handle = rpin.handle
        pin.name = rpin.name
        if rpin.type == HAL_FLOAT:
            pin.halfloat = rpin.get()
        if rpin.type == HAL_BIT:
            pin.halbit = rpin.get()
        if rpin.type == HAL_S32:
            pin.hals32 = rpin.get()
        if rpin.type == HAL_U32:
            pin.halu32 = rpin.get()

        GRemoteComponent.CMD.send(c.SerializeToString())

    #---- HAL 'emulation' --

    def newpin(self, name, type, direction):
        print "-------newpin ",name
        p = GRemotePin(name,type, direction,self.pin_change)
        self.pinsbyname[name] = p
        return p

    def getpin(self, *a, **kw):
        return self.pinsbyname[name]

    def ready(self, *a, **kw):
        print self.name, "ready"
        glib.timeout_add_seconds(self.period, self._tick)
        self.bind()

    def exit(self, *a, **kw):
        GRemoteComponent.COUNT -= 1
        if GRemoteComponent.COUNT == 0:
            print "shutdown here"
            gobject.source_remove(self.cmd_notify)
            gobject.source_remove(self.update_notify)

    def __getitem__(self, k): return self.pinsbyname[k]
    def __setitem__(self, k, v): self.pinsbyname[k].set(v)



class _GStat(gobject.GObject):
    __gsignals__ = {
        'state-estop': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'state-estop-reset': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'state-on': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'state-off': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'homed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, (gobject.TYPE_STRING,)),
        'all-homed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'not-all-homed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, (gobject.TYPE_STRING,)),

        'mode-manual': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'mode-auto': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'mode-mdi': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),

        'interp-run': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),

        'interp-idle': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'interp-paused': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'interp-reading': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'interp-waiting': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),

        'file-loaded': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, (gobject.TYPE_STRING,)),
        'reload-display': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'line-changed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, (gobject.TYPE_INT,)),
        'tool-in-spindle-changed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, (gobject.TYPE_INT,)),
        }

    STATES = { linuxcnc.STATE_ESTOP:       'state-estop'
             , linuxcnc.STATE_ESTOP_RESET: 'state-estop-reset'
             , linuxcnc.STATE_ON:          'state-on'
             , linuxcnc.STATE_OFF:         'state-off'
             }

    MODES  = { linuxcnc.MODE_MANUAL: 'mode-manual'
             , linuxcnc.MODE_AUTO:   'mode-auto'
             , linuxcnc.MODE_MDI:    'mode-mdi'
             }

    INTERP = { linuxcnc.INTERP_WAITING: 'interp-waiting'
             , linuxcnc.INTERP_READING: 'interp-reading'
             , linuxcnc.INTERP_PAUSED: 'interp-paused'
             , linuxcnc.INTERP_IDLE: 'interp-idle'
             }

    def __init__(self, stat = None):
        gobject.GObject.__init__(self)
        self.stat = stat or linuxcnc.stat()
        self.old = {}
        try:
            self.stat.poll()
            self.merge()
        except:
            pass
        gobject.timeout_add(100, self.update)

    def merge(self):
        self.old['state'] = self.stat.task_state
        self.old['mode']  = self.stat.task_mode
        self.old['interp']= self.stat.interp_state
        self.old['file']  = self.stat.file
        self.old['line']  = self.stat.motion_line
        self.old['homed'] = self.stat.homed
        self.old['tool-in-spindle'] = self.stat.tool_in_spindle

    def update(self):
        try:
            self.stat.poll()
        except:
            # Reschedule
            return True
        old = dict(self.old)
        self.merge()

        state_old = old.get('state', 0)
        state_new = self.old['state']
        if not state_old:
            if state_new > linuxcnc.STATE_ESTOP:
                self.emit('state-estop-reset')
            else:
                self.emit('state-estop')
            self.emit('state-off')
            self.emit('interp-idle')

        if state_new != state_old:
            if state_old == linuxcnc.STATE_ON and state_new < linuxcnc.STATE_ON:
                self.emit('state-off')
            self.emit(self.STATES[state_new])
            if state_new == linuxcnc.STATE_ON:
                old['mode'] = 0
                old['interp'] = 0

        mode_old = old.get('mode', 0)
        mode_new = self.old['mode']
        if mode_new != mode_old:
            self.emit(self.MODES[mode_new])

        interp_old = old.get('interp', 0)
        interp_new = self.old['interp']
        if interp_new != interp_old:
            if not interp_old or interp_old == linuxcnc.INTERP_IDLE:
                print "Emit", "interp-run"
                self.emit('interp-run')
            self.emit(self.INTERP[interp_new])

        file_old = old.get('file', None)
        file_new = self.old['file']
        if file_new != file_old:
            self.emit('file-loaded', file_new)

        line_old = old.get('line', None)
        line_new = self.old['line']
        if line_new != line_old:
            self.emit('line-changed', line_new)

        tool_old = old.get('tool-in-spindle', None)
        tool_new = self.old['tool-in-spindle']
        if tool_new != tool_old:
            self.emit('tool-in-spindle-changed', tool_new)

        # if the homed status has changed
        # check number of homed axes against number of available axes
        # if they are equal send the all-homed signal
        # else not-all-homed (with a string of unhomed joint numbers)
        # if a joint is homed send 'homed' (with a string of homed joint numbers)
        homed_old = old.get('homed', None)
        homed_new = self.old['homed']
        if homed_new != homed_old:
            axis_count = count = 0
            unhomed = homed = ""
            for i,h in enumerate(homed_new):
                if h:
                    count +=1
                    homed += str(i)
                if self.stat.axis_mask & (1<<i) == 0: continue
                axis_count += 1
                if not h:
                    unhomed += str(i)
            if count:
                self.emit('homed',homed)
            if count == axis_count:
                self.emit('all-homed')
            else:
                self.emit('not-all-homed',unhomed)

        return True

class GStat(_GStat):
    _instance = None
    def __new__(cls, *args, **kwargs):
        if not cls._instance:
            cls._instance = _GStat.__new__(cls, *args, **kwargs)
        return cls._instance
