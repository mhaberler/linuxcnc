#!/usr/bin/env python
# vim: sts=4 sw=4 et

import _hal, hal, gobject
import linuxcnc
import sys
import os
import time
import zmq
import sdiscover
import uuid

import gtk.gdk
import gobject
import glib

from message_pb2 import Container
from types_pb2 import *

HAL_RCOMP_VERSION = 2

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
    __gsignals__ = {
        'value-changed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, ()),
        'hal-pin-changed': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE, (gobject.TYPE_OBJECT,))
        }

    def __init__(self, name, type, direction,comp):
        gobject.GObject.__init__(self)
        self.comp = comp
        self.name = name
        self.type = type
        self.direction = direction
        self.value = None
        self.handle = 0
        w = self.comp.builder.get_object(self.name)
        self.setter = None
        if isinstance(w, (gtk.Range, gtk.SpinButton)):
            self.setter = w.set_value
        if isinstance(w, (gtk.CheckButton,
                          gtk.ToggleButton,gtk.RadioButton,gtk.ComboBox)):
            self.setter = w.set_active
        self.widget = w

    def set(self, value):
        # print "set local",self.name,value
        self.value = value
        self.emit('value-changed')


    def set_remote(self, value):
        #print "set remote",self.name,value
        if self.setter:
            self.setter(value)

        self.value = value
        self.emit('value-changed')
        self.emit('hal-pin-changed',self)

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

def enum(*sequential, **named):
    enums = dict(zip(sequential, range(len(sequential))), **named)
    reverse = dict((value, key) for key, value in enums.iteritems())
    enums['reverse_mapping'] = reverse
    return type('Enum', (), enums)

states = enum('DISCOVER', 'DOWN', 'TRYING', 'UP')
# print states.DOWN, states.UP
# print states.reverse_mapping[1]

class GRemoteComponent(gobject.GObject):
    __gtype_name__ = 'GRemoteComponent'
    __gsignals__ = {
        'protocol-error' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE,
                              (gobject.TYPE_STRING,)),
        'protocol-status' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE,
                              (gobject.TYPE_INT,gobject.TYPE_INT,)),
        'server-instance' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE,
                              (gobject.TYPE_STRING,))
        }

    CONTEXT = None
    UPDATE = None
    CMD = None
    COUNT = 0 # instance counter

    def discover(self,servicelist, trace=False):
        sd = sdiscover.ServiceDiscover(trace=trace)
        for s in servicelist:
            sd.add(s)
        services = sd.discover()
        if not services:
            print "failed to discover all services, found only:"
            sd.detail(True)
        return services

    def set_uuid(self, u):
        if self.uuid != u:
            self.uuid = u
            self.emit('server-instance', str(uuid.UUID(bytes=self.uuid)))
            print "haltalk uuid:",uuid.UUID(bytes=self.uuid)

    def __init__(self, name, builder,cmd_uri=None, update_uri=None, instance=0,
                 period=3,debug=False):
        gobject.GObject.__init__(self)
        self.builder = builder
        self.debug = debug
        self.period = period
        self.cstate = states.DOWN
        self.sstate = states.DOWN
        self.uuid = -1

        self.ping_outstanding = False
        self.name = name
        self.pinsbyname = {}
        self.pinsbyhandle = {}
        self.synced = False
        # more efficient to reuse a protobuf Message
        self.rx = Container()
        self.tx = Container()

        if not GRemoteComponent.CONTEXT:
            ctx = zmq.Context()
            ctx.linger = 0
            update = ctx.socket(zmq.XSUB)
            cmd = ctx.socket(zmq.DEALER)
            cmd.identity = "%s-%d" % (name,os.getpid())

            if not (cmd_uri and update_uri):
                self.services = self.discover([ST_STP_HALRCOMP,ST_HAL_RCOMMAND])
                if not self.services:
                    raise RuntimeError, "Service Discovery failed"
                print "discovered HALrcomp uri %s" % (self.services[ST_STP_HALRCOMP].uri)
                update.connect(self.services[ST_STP_HALRCOMP].uri)

                print "discovered HALrcommand uri %s" % (self.services[ST_HAL_RCOMMAND].uri)
                cmd.connect(self.services[ST_HAL_RCOMMAND].uri)
            else:
                update.connect(update_uri)
                cmd.connect(cmd_uri)

            self.cmd_notify = gobject.io_add_watch(cmd.get(zmq.FD),
                                                   gobject.IO_IN,
                                                   self.zmq_readable, cmd,
                                                   self.cmd_readable)
            self.update_notify = gobject.io_add_watch(update.get(zmq.FD),
                                                      gobject.IO_IN,
                                                      self.zmq_readable, update,
                                                      self.update_readable)

            # kick GTK event loop, or the zmq_readable callback gets stuck
            while gtk.events_pending():
                gtk.main_iteration()

            GRemoteComponent.CONTEXT = ctx
            GRemoteComponent.UPDATE = update
            GRemoteComponent.CMD = cmd
        GRemoteComponent.COUNT += 1


    # activity on one of the zmq sockets:
    def zmq_readable(self, eventfd, condition, socket, callback):
        while socket.get(zmq.EVENTS)  & zmq.POLLIN:
            callback(socket)
        return True

    def _tick(self):
        self.timer_tick()
        return True # re-arm

    # --- HALrcomp protocol support ---
    def bind(self):

        self.tx.type = MT_HALRCOMP_BIND
        c = self.tx.comp.add()
        c.name = self.name
        for pin_name,pin in self.pinsbyname.iteritems():
            p = c.pin.add()
            p.name = self.name +  "." + pin_name
            p.type = pin.get_type()
            p.dir = pin.get_dir()
        if self.debug: print "bind:" , str(self.tx)
        GRemoteComponent.CMD.send(self.tx.SerializeToString())
        self.tx.Clear()

    def pin_update(self,rp,lp):
        if rp.HasField('halfloat'): lp.set_remote(rp.halfloat)
        if rp.HasField('halbit'):   lp.set_remote(rp.halbit)
        if rp.HasField('hals32'):   lp.set_remote(rp.hals32)
        if rp.HasField('halu32'):   lp.set_remote(rp.halu32)

    # process updates received on subscriber socket
    def update_readable(self, socket):
        m = socket.recv_multipart()
        topic = m[0]
        self.rx.ParseFromString(m[1])

        if self.debug: print "status_update ", topic, str(self.rx)

        if self.rx.type == MT_HALRCOMP_INCREMENTAL_UPDATE: # incremental update
            for rp in self.rx.pin:
                lp = self.pinsbyhandle[rp.handle]
                self.pin_update(rp,lp)

            if self.sstate != states.UP:
                self.emit('protocol-status', self.cstate,states.UP)
            self.sstate = states.UP
            return

        if self.rx.type == MT_HALRCOMP_FULL_UPDATE: # full update
            if self.rx.uuid:
                self.set_uuid(self.rx.uuid)
            for c in self.rx.comp:
                for rp in c.pin:
                    lname = str(rp.name)
                    if "." in lname: # strip comp prefix
                        cname,pname = lname.split(".",1)
                    lp = self.pinsbyname[pname]
                    lp.handle = rp.handle
                    self.pinsbyhandle[rp.handle] = lp
                    self.pin_update(rp,lp)
                self.synced = True
                if self.sstate != states.UP:
                    self.emit('protocol-status', self.cstate,states.UP)
                self.sstate = states.UP
            return

        if self.rx.type == MT_HALRCOMP_ERROR:
            self.sstate = states.DOWN
            print "proto error on subscribe: ",str(self.rx.note)
            self.emit('protocol-error', str(self.rx.note))
            self.emit('protocol-status', self.cstate,self.sstate)

        print "status_update: unknown message type: ",str(self.rx)

    # process replies received on command socket
    def cmd_readable(self, socket):
        f = socket.recv()
        self.rx.ParseFromString(f)

        if self.debug: print "server_message " + str(self.rx)

        if self.rx.type == MT_PING_ACKNOWLEDGE:

            self.cstate = states.UP
            self.emit('protocol-status', self.cstate,self.sstate)
            #self.emit('hal-connected')
            self.ping_outstanding = False
            if self.rx.uuid:
                self.set_uuid(self.rx.uuid)
            return

        if self.rx.type == MT_HALRCOMP_BIND_CONFIRM:
            if self.debug: print "bind confirmed "
            self.cstate = states.UP
            self.sstate = states.TRYING
            self.emit('protocol-status', self.cstate,self.sstate)
            GRemoteComponent.UPDATE.send("\001" + self.name)
            return

        if self.rx.type == MT_HALRCOMP_BIND_REJECT:
            self.cstate = states.DOWN
            self.emit('protocol-status', self.cstate,self.sstate)
            print "bind rejected: %s" % (str(self.rx.note))
            return

        if self.rx.type == MT_HALRCOMP_SET_REJECT:
            self.cstate = states.DOWN
            self.emit('protocol-status', self.cstate,self.sstate)
            #protocol error - emit string signal
            print "------ pin change rejected: %s" % (str(self.rx.note))
            return

        print "----------- UNKNOWN server message type ", str(self.rx)

    def timer_tick(self):
        if self.ping_outstanding:
            self.cstate = states.TRYING
            self.emit('protocol-status', self.cstate,self.sstate)
            print "timeout"
        self.tx.type = MT_PING
        GRemoteComponent.CMD.send(self.tx.SerializeToString())
        self.tx.Clear()
        self.ping_outstanding = True

    def pin_change(self, rpin):
        if self.debug: print "pinchange", self.synced
        if not self.synced: return
        if rpin.direction == HAL_IN: return
        self.tx.type = MT_HALRCOMP_SET

        # This message MUST carry a Pin message for each pin which has
        # changed value since the last message of this type.
        # Each Pin message MUST carry the handle field.
        # Each Pin message MAY carry the name field.
        # Each Pin message MUST - depending on pin type - carry a halbit,
        # halfloat, hals32, or halu32 field.
        pin = self.tx.pin.add()

        pin.handle = rpin.handle
        pin.name = rpin.name
        pin.type = rpin.type
        if rpin.type == HAL_FLOAT:
            pin.halfloat = rpin.get()
        if rpin.type == HAL_BIT:
            pin.halbit = rpin.get()
        if rpin.type == HAL_S32:
            pin.hals32 = rpin.get()
        if rpin.type == HAL_U32:
            pin.halu32 = rpin.get()
        GRemoteComponent.CMD.send(self.tx.SerializeToString())
        self.tx.Clear()

    #---- HAL 'emulation' --

    def newpin(self, name, type, direction):
        if self.debug: print "-------newpin ",name
        p = GRemotePin(name,type, direction,self)
        p.connect('value-changed', self.pin_change)
        self.pinsbyname[name] = p
        return p

    def getpin(self, *a, **kw):
        return self.pinsbyname[name]

    def ready(self, *a, **kw):
        if self.debug: print self.name, "ready"
        glib.timeout_add_seconds(self.period, self._tick)
        self.cstate = states.TRYING
        self.emit('protocol-status', self.cstate,self.sstate)
        self.bind()

    def exit(self, *a, **kw):
        GRemoteComponent.COUNT -= 1
        if GRemoteComponent.COUNT == 0:
            print "shutdown here"
            gobject.source_remove(self.cmd_notify)
            gobject.source_remove(self.update_notify)

    def __getitem__(self, k): return self.pinsbyname[k]
    def __setitem__(self, k, v): self.pinsbyname[k].set(v)

#gobject.type_register(GRemoteComponent)

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
