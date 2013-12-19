from halext import *
import rtapi
from pyczmq import ffi, zmq, zctx, zsocket, zmsg, zframe, zbeacon, zloop, zstr
from pyczmq._cffi import ptop

import time
import os
import sys

BEACON_PORT = 10042
HAL_RCOMP_VERSION = 1

from message_pb2 import Container
from types_pb2 import *

class ValidateError(RuntimeError):
   def __init__(self, arg):
      self.args = arg

def pindict(comp):
    # this should be fixed in halext so pin objects
    # of a comp are accessible as a dict, not just
    # its values
    d = {}
    for p in comp.pins():
        d[p.name] = p
    return d


class HalServer:

    def full_update(self, comp):
        # This message MUST carry a Pin message for each of pin of the component.
        # Each Pin message MUST carry the name and handle fields.
        # Each Pin message MUST - depending on pin type - carry a halbit, halfloat, hals32, or halu32 field.
        # A Pin message SHOULD carry the linked field.
        self.tx.type = MT_HALRCOMP_STATUS
        for pin in comp.pins():
            p = self.tx.pin.add()
            p.name = pin.name
            p.handle = pin.handle
            p.type = pin.type   # redundant FIXME
            p.linked = pin.linked
            if pin.type == HAL_FLOAT:
                p.halfloat = pin.value
            if pin.type == HAL_BIT:
                p.halbit = pin.value
            if pin.type == HAL_S32:
                p.hals32 = pin.value
            if pin.type == HAL_U32:
                p.halu32 = pin.value
        if self.debug: print "full_update " + comp.name, str(self.tx)
        m = zmsg.new()
        zmsg.pushstr(m,comp.name)
        zmsg.append(m,zframe.new(self.tx.SerializeToString()))
        zmsg.send(m, self.update)
        self.tx.Clear()


    def validate(self, comp, pins):
        ''' validate pins of an existing remote component against
        requested pinlist '''

        np_exist = len(comp.pins())
        np_requested = len(pins)
        if np_exist != np_requested:
            raise ValidateError, "pin count mismatch: requested=%d have=%d" % (np_exist,np_requested)
        pd = pindict(comp)
        for p in pins:
            if not pd.has_key(str(p.name)):
                raise ValidateError, "pin " + p.name + "does not exist"

            pin = pd[str(p.name)]
            if p.type != pin.type:
                raise ValidateError, "pin %s type mismatch: %d/%d" % (p.name, p.type,pin.type)
            if p.dir != pin.dir:
                raise ValidateError, "pin %s direction mismatch: %d/%d" % (p.name, p.dir,pin.dir)
        # all is well

    @ffi.callback('zloop_fn')
    def client_command(loop, item, arg):
       self = ffi.from_handle(arg)
       m = zmsg.recv(self.cmd)
       client = zmsg.popstr(m)
       self.rx.ParseFromString(zframe.data(zmsg.pop(m)))

       if self.rx.type == MT_HALRCOMP_BIND:
          try:
             # raises KeyError if component non-existent:
             ce = self.rcomp[self.rx.comp.name]
             # component exists, validate pinlist
             self.validate(ce, self.rx.pin)

          except ValidateError,e:
             print >> self.rtapi, "--- client_command: ValidateError", client,str(e)

             # component existed, but pinlist mismatched
             self.rx.type = MT_HALRCOMP_BIND_REJECT
             self.tx.note = e

             msg = zmsg.new()
             zmsg.pushstr(msg, client)
             zmsg.append(msg, zframe.new(self.tx.SerializeToString()))
             zmsg.send(msg, self.cmd)
             self.tx.Clear()
             return 0

          except KeyError:
             # remote component doesnt exist
             try:
                # create as per pinlist
                name = str(self.rx.comp.name)
                rcomp = HalComponent(name,TYPE_REMOTE)
                for s in self.rx.pin:
                   rcomp.newpin(str(s.name), s.type, s.dir)
                rcomp.ready()
                rcomp.acquire()
                rcomp.bind()
                print >> self.rtapi, "%s created remote comp: %s" % (client,name)

                # add to in-service dict
                self.rcomp[name] = rcomp
                self.tx.type = MT_HALRCOMP_BIND_CONFIRM
             except Exception,s:
                print >> self.rtapi, "%s: %s create failed: %s" % (client,str(self.rx.comp.name), str(s))
                self.tx.type = MT_HALRCOMP_BIND_REJECT
                self.tx.note = str(s)

             msg = zmsg.new()
             zmsg.pushstr(msg, client)
             zmsg.append(msg, zframe.new(self.tx.SerializeToString()))
             zmsg.send(msg, self.cmd)
             self.tx.Clear()
             return 0

          else:
             print >> self.rtapi, "%s: %s existed and validated OK" % (client,str(self.rx.comp.name))

             # component existed and validated OK
             self.tx.type = MT_HALRCOMP_BIND_CONFIRM
             # This message MUST carry a Component submessage.
             # The Component submessage MUST carry the name field.
             # The Component submessage MAY carry the scantimer and flags field.
             # This message MUST carry a Pin message for each of pin.
             # Each Pin message MUST carry the name, type and dir fields.
             # A Pin message MAY carry the epsilon and flags fields.
             # cm = r.comp
             # cm.name = ce.name
             self.tx.comp.name = ce.name
             # FIXME add scantimer, flags
             for p in ce.pins():
                pin = self.tx.pin.add()
                pin.name = p.name
                pin.type = p.type
                pin.dir = p.dir
                pin.epsilon = p.epsilon
                pin.flags = p.flags
             msg = zmsg.new()
             zmsg.pushstr(msg, client)
             zmsg.append(msg, zframe.new(self.tx.SerializeToString()))
             zmsg.send(msg, self.cmd)
             self.tx.Clear()
             print >> self.rtapi, "%s: bound to %s" % (client, ce.name)

             return 0

       if self.rx.type == MT_PING:
          self.tx.type = MT_PING_ACKNOWLEDGE
          msg = zmsg.new()
          zmsg.pushstr(msg, client)
          zmsg.append(msg, zframe.new(self.tx.SerializeToString()))
          zmsg.send(msg, self.cmd)
          self.tx.Clear()
          return 0


       if self.rx.type == MT_HALRCOMP_SET_PINS:
            # This message MUST carry a Pin message for each pin
            # which has changed value since the last message of this type.
            # Each Pin message MUST carry the handle field.
            # Each Pin message MAY carry the name field.
            # Each Pin message MUST - depending on pin type - carry a
            # halbit, halfloat, hals32, or halu32 field.

            # update pins as per pinlist
          for p in self.rx.pin:
             try:
                lpin = self.pinsbyhandle[p.handle]
                if lpin.type == HAL_FLOAT:
                   lpin.value = p.halfloat
                if lpin.type == HAL_BIT:
                   lpin.value = p.halbit
                if lpin.type == HAL_S32:
                   lpin.value = p.hals32
                if lpin.type == HAL_U32:
                   lpin.value = p.halu32
             except Exception,e:
                self.tx.type = MT_HALRCOMP_SET_PINS_REJECT
                self.tx.note = "pin handle %d: %s" % (p.handle,e)
                msg = zmsg.new()
                zmsg.pushstr(msg, client)
                zmsg.append(msg, zframe.new(self.tx.SerializeToString()))
                zmsg.send(msg, self.cmd)
                self.tx.Clear()
                # if lpin:
                #    self.rcomp[lpin.owner].last_update = int(time.time())
             return 0

       print >> self.rtapi, "client %s: unknown message type: %d " % (client, self.rx.type)
       return 0

    def report(self, comp):
        pinlist = comp.changed_pins()
        if not pinlist:
            return # no change, no update
        self.tx.type =  MT_HALRCOMP_PIN_CHANGE
        for p in pinlist:
            pin = self.tx.pin.add()
            pin.name = p.name
            pin.handle = p.handle
            pin.linked = p.linked
            if p.type == HAL_FLOAT:
                pin.halfloat = p.value
            if p.type == HAL_BIT:
                pin.halbit = p.value
            if p.type == HAL_S32:
                pin.hals32 = p.value
            if p.type == HAL_U32:
                pin.halu32 = p.value
        if self.debug: print "report ",comp.name, str(r)
        msg = zmsg.new()
        zmsg.pushstr(msg, comp.name)
        zmsg.append(msg,  zframe.new(self.tx.SerializeToString()))
        zmsg.send(msg, self.update)
        self.tx.Clear()



    def unwind(self):
        # unbind and orphan any remote comps we owned
        for name,comp in self.rcomp.iteritems():
           if comp.state == COMP_BOUND:
              print >> self.rtapi, "unbind %s" % (name)
              comp.unbind()
           if comp.pid == os.getpid():
              comp.release()
              print >> self.rtapi, "release %s" % (name)

        # exit halserver comp
        self.halserver.exit()

    @ffi.callback('zloop_fn')
    def update_msg(loop, item, arg):
       # subscribe/unsubscribe events
       self = ffi.from_handle(arg)
       notify = zframe.data(zframe.recv(self.update))
       tag = ord(notify[0])
       topic = notify[1:]
       if tag == 0:
          # an unsubscribe is sent only on last subscriber
          # going away
          try:
             self.rcomp[topic].unbind()
             print >> self.rtapi, "unbind: %s" % (topic)
          except Exception:
             sys.exc_clear()

       elif tag == 1:
          if not topic in self.rcomp.keys():
             note = "subscribe: comp %s doesnt exist " % topic
             print >> self.rtapi, note
             self.tx.type = MT_HALRCOMP_SUBSCRIBE_ERROR
             self.tx.note = note
             m = zmsg.new()
             zmsg.pushstr(m,topic)
             zmsg.append(m,zframe.new(self.tx.SerializeToString()))
             zmsg.send(m, self.update)
             self.tx.Clear()
          else:
             print >> self.rtapi, "subscribe to: %s" % (topic)
             if self.rcomp[topic].state == COMP_UNBOUND:
                self.rcomp[topic].bind() # once only
                print >> self.rtapi, "bind: %s" % (topic)

             for p in self.rcomp[topic].pins():
                self.pinsbyhandle[p.handle] = p
             self.full_update(self.rcomp[topic])
       else:
          # normal message on XPUB - not used
          print "-- normal message on XPUB ?"
       return 0

    @ffi.callback('zloop_fn')
    def timer_event(loop, item, arg):
       self = ffi.from_handle(arg)
       for name,comp in self.rcomp.iteritems():
          self.report(comp)
       return 0

    def beacon_announce(self):
       print "--- beacon announce:"
       pass

    @ffi.callback('zloop_fn')
    def beacon_request(loop, item, arg):
       self = ffi.from_handle(arg)
       ipaddress = zstr.recv(self.beacon)
       self.rx.ParseFromString(zframe.data(zframe.recv(self.beacon)))
       print "--- beacon request: ip=%s msg=%s" % (ipaddress, str(self.rx)),

       if self.rx.type == MT_SERVICE_PROBE:
          self.tx.Clear()
          self.tx.type = MT_SERVICE_ANNOUNCEMENT
          sa = self.tx.service_announcement.add()
          sa.instance = 0
          sa.stype = ST_HAL_RCOMP
          sa.version = HAL_RCOMP_VERSION
          sa.cmd_port = self.cmd_port
          sa.update_port = self.update_port

          # UDP unicast reply
          zbeacon.send(self.service_beacon, ipaddress,
                       self.tx.SerializeToString())
       return 0


    def __init__(self, cmd_uri=None, update_uri=None,msec=100,debug=False):

        # need to create a dummy HAL component to keep hal_lib happy
        self.halserver =  HalComponent("halserv%d" % os.getpid(), TYPE_USER)
        self.halserver.ready()

        # RTAPILogger will write to the shared log:
        self.rtapi = rtapi.RTAPILogger(rtapi.MSG_ERR,"halserver")

        self.msec = msec
        self.debug = debug
        # it's more efficient to reuse a protobuf Message
        self.rx = Container()
        self.tx = Container()

        # components in service
        self.rcomp = {}

        self.pinsbyhandle = {}

        # collect orphan & unbound remote components only
        for name, comp in components().iteritems():
            if comp.type == TYPE_REMOTE and comp.pid == 0 and comp.state == COMP_UNBOUND:
                print >> self.rtapi, "acquire: %s" % (name)
                comp.acquire()
                self.rcomp[name] = comp

        # zmq plumbing
        self.ctx = zctx.new()

        self.cmd = zsocket.new(self.ctx, zmq.ROUTER)
        if cmd_uri:
           self.cmd_port = zsocket.bind(self.cmd, cmd_uri)
        else:
           # bind to ephemeral port
           self.cmd_port = zsocket.bind(self.cmd, "tcp://*:*")

        self.update = zsocket.new(self.ctx, zmq.XPUB)
        zsocket.set_xpub_verbose(self.update, 1)
        zsocket.set_linger(self.update, 0)
        if update_uri:
           self.update_port = zsocket.bind(self.update, update_uri)
        else:
           self.update_port = zsocket.bind(self.update,  "tcp://*:*")

        zsocket.set_linger(self.cmd, 0)

        # service beacon responder
        self.service_beacon = zbeacon.new(self.ctx, BEACON_PORT)
        zbeacon.noecho(self.service_beacon)
        zbeacon.subscribe(self.service_beacon, '')
        self.beacon = zbeacon.socket(self.service_beacon)

        self.loop = zloop.new()
        zloop.set_verbose(self.loop, self.debug)

        zloop.timer(self.loop, msec, 0, self.timer_event, self)

        update_input = zmq.pollitem(socket=self.update, events=zmq.POLLIN)
        zloop.poller(self.loop, update_input, self.update_msg, self)

        cmd_input = zmq.pollitem(socket=self.cmd, events=zmq.POLLIN)
        zloop.poller(self.loop, cmd_input, self.client_command, self)

        beacon_input = zmq.pollitem(socket=self.beacon, events=zmq.POLLIN)
        zloop.poller(self.loop, beacon_input, self.beacon_request, self)

        zloop.start(self.loop)

        if self.debug: print "--- exiting"
        self.unwind()
        zmq.ctx_shutdown(self.ctx)
        sys.exit(0)




halserver = HalServer(msec=20,debug=False)
# ,
#                       cmd_uri="tcp://127.0.0.1:4711",
#                       update_uri="tcp://127.0.0.1:4712")



