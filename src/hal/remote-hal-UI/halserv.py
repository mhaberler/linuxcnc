from halext import *
import zmq
import time
import os
import sys

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
        u = Container()
        u.type = MT_HALRCOMP_STATUS
        for pin in comp.pins():
            p = u.pin.add()
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
        print "full_update " + comp.name, str(u)
        self.update.send_multipart([comp.name,u.SerializeToString()])


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

    def client_command(self, client, message):
        c = Container()
        c.ParseFromString(message)
        #print "--- client_command:", client,str(c)

        r = Container()
        if c.type == MT_HALRCOMP_BIND:
           print "--- client_command BIND", client

           try:
              # raises KeyError if component non-existent:
              ce = self.rcomp[c.comp.name]
              # component exists, validate pinlist
              self.validate(ce, c.pin)

           except ValidateError,m:
              print "--- client_command: ValidateError", client

              # component existed, but pinlist mismatched
              r.type = MT_HALRCOMP_BIND_REJECT
              r.note = m
              self.cmd.send_multipart([client,r.SerializeToString()])
              return

           except KeyError:
              # remote component doesnt exist
              try:
                 # create as per pinlist
                 name = str(c.comp.name)
                 rcomp = HalComponent(name,TYPE_REMOTE)
                 for s in c.pin:
                    rcomp.newpin(str(s.name), s.type, s.dir)
                 rcomp.ready()
                 rcomp.acquire()
                 rcomp.bind()
                 # add to in-service dict
                 self.rcomp[name] = rcomp
                 r.type = MT_HALRCOMP_BIND_CONFIRM
              except Exception,s:
                 print "comp create fail", s
                 r.type = MT_HALRCOMP_BIND_REJECT
                 r.note = str(s)
              self.cmd.send_multipart([client,r.SerializeToString()])
              return

           else:
              print "--- client_command: existed and validated OK", client

              # component existed and validated OK
              r.type = MT_HALRCOMP_BIND_CONFIRM
              # This message MUST carry a Component submessage.
              # The Component submessage MUST carry the name field.
              # The Component submessage MAY carry the scantimer and flags field.
              # This message MUST carry a Pin message for each of pin.
              # Each Pin message MUST carry the name, type and dir fields.
              # A Pin message MAY carry the epsilon and flags fields.
              # cm = r.comp
              # cm.name = ce.name
              r.comp.name = ce.name
              # FIXME add scantimer, flags
              for p in ce.pins():
                 pin = r.pin.add()
                 pin.name = p.name
                 pin.type = p.type
                 pin.dir = p.dir
                 pin.epsilon = p.epsilon
                 pin.flags = p.flags
              self.cmd.send_multipart([client,r.SerializeToString()])
              return

        if c.type == MT_PING:
           #print "--- client_command: MT_PING"

           r = Container()
           r.type = MT_PING_ACKNOWLEDGE
           self.cmd.send_multipart([client,r.SerializeToString()])
           return


        if c.type == MT_HALRCOMP_SET_PINS:
            # This message MUST carry a Pin message for each pin
            # which has changed value since the last message of this type.
            # Each Pin message MUST carry the handle field.
            # Each Pin message MAY carry the name field.
            # Each Pin message MUST - depending on pin type - carry a
            # halbit, halfloat, hals32, or halu32 field.

            # update pins as per pinlist
            for p in c.pin:
                try:
                    lpin = self.pinsbyhandle[p.handle]
                    if lpin.type == HAL_FLOAT:
                        lpin.value = p.halfloat
                    if lpin.type == HAL_BIT:
                        lpin.value = p.halbit
                    if lpin.type == HAL_S32:
                        lpinp.value = p.hals32
                    if lpin.type == HAL_U32:
                        lpinp.value = p.halu32
                except Exception,e:
                    r = Container()
                    r.type = MT_HALRCOMP_PIN_CHANGE_REJECT
                    r.note = "pin handle %d: %s" % (p.handle,e)
                    self.cmd.send_multipart([client,r.SerializeToString()])
                if lpin:
                   self.rcomp[lpin.owner].last_update = int(time.time())
            return

        print "error: unknown message type: %d " % c.type

    def report(self, comp):
        pinlist = comp.changed_pins()
        if not pinlist:
            return # no change, no update
        r = Container()
        r.type =  MT_HALRCOMP_PIN_CHANGE
        for p in pinlist:
            pin = r.pin.add()
            pin.name = p.name
            pin.handle = p.handle
            pin.linked = p.linked
            if p.type == HAL_FLOAT:
                pin.halfloat = p.value
                print "floatval = ",p.value
            if p.type == HAL_BIT:
                pin.halbit = p.value
            if p.type == HAL_S32:
                pin.hals32 = p.value
            if p.type == HAL_U32:
                pin.halu32 = p.value
        print "report ",comp.name, str(r)
        self.update.send_multipart([comp.name,r.SerializeToString()])

    def timer_event(self):
        #print "timer"
        for name,comp in self.rcomp.iteritems():
            self.report(comp)

    def unwind(self):
        # unbind and orphan any remote comps we owned
        for name,comp in self.rcomp.iteritems():
            if comp.state == COMP_BOUND:
                comp.unbind()
            if comp.pid == os.getpid():
                comp.release()
        # exit halserver comp
        self.halserver.exit()

    def __init__(self, cmd_uri="tcp://127.0.0.1:4711", update_uri="tcp://127.0.0.1:4712",msec=100):
        self.msec = msec
        self.ctx = zmq.Context()

        self.cmd = self.ctx.socket(zmq.ROUTER)
        #self.cmd.setsockopt(zmq.ROUTER_MANDATORY, 1)
        self.cmd.bind(cmd_uri)
        self.cmd.setsockopt(zmq.LINGER,0)


        self.update = self.ctx.socket(zmq.XPUB)
        self.update.set(zmq.XPUB_VERBOSE, 1)
        self.update.setsockopt(zmq.LINGER,0)

        self.update.bind(update_uri)

        self.poller = zmq.Poller()
        self.poller.register(self.cmd, zmq.POLLIN)
        self.poller.register(self.update, zmq.POLLIN)

        # need to create a dummy HAL component to keep hal_lib happy
        self.halserver =  HalComponent("halserv%d" % os.getpid(), TYPE_USER)
        self.halserver.ready()

        # components in service
        self.rcomp = {}

        self.pinsbyhandle = {}

        # collect orphan & unbound remote components only
        for name, comp in components().iteritems():
            if comp.type == TYPE_REMOTE and comp.pid == 0 and comp.state == COMP_UNBOUND:
                print "acquiring:", name
                comp.acquire()
                self.rcomp[name] = comp

        done = False

        try:
            while not done:
                # command reception event
                sockets = dict(self.poller.poll(timeout=msec))
                if self.cmd in sockets and sockets[self.cmd] == zmq.POLLIN:
                    (client,message) = self.cmd.recv_multipart()
                    self.client_command(client, message)

                    # msgs = self.cmd.recv_multipart()
                    # print len(msgs),msgs
                    # self.client_command(client, message)

                # subscribe/unsubscribe events
                if self.update in sockets and sockets[self.update] == zmq.POLLIN:
                    notify = self.update.recv()
                    tag = ord(notify[0])
                    topic = notify[1:]
                    if tag == 0:
                       # an unsubscribe is sent only on last subscriber
                       # going away
                       try:
                          self.rcomp[topic].unbind()
                       except Exception:
                          sys.exc_clear()

                    elif tag == 1:
                        print "----- subscribe", topic
                        if not topic in self.rcomp.keys():
                            print "error: comp %s doesnt exist " % topic
                        else:
                            if self.rcomp[topic].state == COMP_UNBOUND:
                                self.rcomp[topic].bind() # once only
                            for p in self.rcomp[topic].pins():
                                self.pinsbyhandle[p.handle] = p
                            self.full_update(self.rcomp[topic])
                    else:
                        # normal message on XPUB - not used
                        pass

                # timer tick
                if not sockets:
                    self.timer_event()

        except KeyboardInterrupt:
            print "unwind"
            self.unwind()

halserver = HalServer(msec=1000)


