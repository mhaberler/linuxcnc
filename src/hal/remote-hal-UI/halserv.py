from halext import *
import zmq
import time
import os
from message_pb2 import Container
from types_pb2 import *

def pindict(comp):
    # this needs to be fixed in halext so pin objects
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


    def client_command(self, client, message):
        print "rcomp"
        cmd = Container()
        cmd.ParseFromString(message)
        print client,str(cmd)
        r = Container()

        if cmd.type == MT_HALRCOMP_BIND:
            if cmd.comp.name in self.rcomp.keys():

                # component exists, validate pinlist
                ce = self.rcomp[cmd.comp.name]

                print "existing comp",ce.name, "validate.."
                nreq = len(cmd.pin)
                nexist = len(ce.pins())
                if nreq != nexist:
                    r.type = MT_HALRCOMP_BIND_REJECT
                    r.note =  "mismatched number of pins: %d/%d" % (nreq/nexist)
                    self.cmd.send_multipart([client,r.SerializeToString()])
                    return

                pd = pindict(ce)
                for p in cmd.pin:
                    try:
                        if not pd.has_key(str(p.name)):
                            raise KeyError, "pin " + p.name + "does not exist"
                        pin = pd[str(p.name)]


                        if p.type != pin.type:
                            raise KeyError, "pin " + p.name + "type mismatch: %d/%d" % (p.type,pin.type)
                        if p.dir != pin.dir:
                            raise KeyError, "pin " + p.name + "direction mismatch: %d/%d" % (p.dir,pin.dir)
                    except KeyError, e:
                        r.type = MT_HALRCOMP_BIND_REJECT
                        r.note =  str(e)
                        self.cmd.send_multipart([client,r.SerializeToString()])
                        return
                    else:

                        # if the comp was unbound, hal_bind() now
                        if ce.state == COMP_UNBOUND:
                            ce.bind()

                        r.type = MT_HALRCOMP_BIND_CONFIRM
                        # This message MUST carry a Component submessage.
                        # The Component submessage MUST carry the name field.
                        # The Component submessage MAY carry the scantimer and flags field.
                        # This message MUST carry a Pin message for each of pin.
                        # Each Pin message MUST carry the name, type and dir fields.
                        # A Pin message MAY carry the epsilon and flags fields.
                        cm = r.comp
                        cm.name = ce.name
                        # add scantimer, flags!
                        for p in ce.pins():
                            pin = r.pin.add()
                            pin.name = p.name
                            pin.type = p.type
                            pin.dir = p.dir
                            pin.epsilon = p.epsilon
                            pin.flags = p.flags

                        self.cmd.send_multipart([client,r.SerializeToString()])
                        return
            else:
                try:
                    # create as per pinlist
                    name = str(cmd.comp.name)
                    rcomp = HalComponent(name,TYPE_REMOTE)
                    for s in cmd.pin:
                        rcomp.newpin(str(s.name), s.type, s.dir)
                    rcomp.ready()
                    r.type = MT_HALRCOMP_BIND_CONFIRM
                    rcomp.acquire()
                    rcomp.bind()
                    # add to in-service dict
                    self.rcomp[name] = rcomp
                except Exception,s:
                    print "comp create fail", s
                    r.type = MT_HALRCOMP_BIND_REJECT
                    r.note = str(s)
                self.cmd.send_multipart([client,r.SerializeToString()])
            return

        if cmd.type == MT_PING:
            r = Container()
            r.type = MT_PING_ACKNOWLEDGE
            self.cmd.send_multipart([client,r.SerializeToString()])
            return


        if cmd.type == MT_HALRCOMP_SET_PINS:
            # This message MUST carry a Pin message for each pin
            # which has changed value since the last message of this type.
            # Each Pin message MUST carry the handle field.
            # Each Pin message MAY carry the name field.
            # Each Pin message MUST - depending on pin type - carry a
            # halbit, halfloat, hals32, or halu32 field.

            # update pins as per pinlist
            for p in cmd.pin:
                print "setpin ",str(p)

            # in case of error send MT_HALRCOMP_PIN_CHANGE_REJECT here
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
        print "report ",comp.name, str(r)
        self.update.send_multipart([comp.name,r.SerializeToString()])

    def timer_event(self):
        print "timer"
        for name,comp in self.rcomp.iteritems():
            self.report(comp)

    def __init__(self, cmd_uri="tcp://127.0.0.1:4711", update_uri="tcp://127.0.0.1:4712",msec=100):
        self.msec = msec
        self.ctx = zmq.Context()

        self.cmd = self.ctx.socket(zmq.ROUTER)
        self.cmd.bind(cmd_uri)

        self.update = self.ctx.socket(zmq.XPUB)
        self.update.set(zmq.XPUB_VERBOSE, 1)
        self.update.bind(update_uri)

        self.poller = zmq.Poller()
        self.poller.register(self.cmd, zmq.POLLIN)
        self.poller.register(self.update, zmq.POLLIN)

        # need to create a dummy HAL component to keep hal_lib happy
        self.comp =  HalComponent("halserv%d" % os.getpid(), TYPE_USER)
        self.comp.ready()
        self.rcomp = {}

        # collect unbound remote components
        for name, comp in components().iteritems():
            if comp.type == TYPE_REMOTE:
                if comp.pid == 0:
                    rcomp.acquire()
                    if comp.state == COMP_UNBOUND:
                        self.rcomp[name] = comp

        self.subscribers = {}
        done = False

        while not done:
            # command reception event
            sockets = dict(self.poller.poll(timeout=msec))
            if self.cmd in sockets and sockets[self.cmd] == zmq.POLLIN:
                (client,message) = self.cmd.recv_multipart()
                self.client_command(client, message)

            # handle subscribe/unsubscribe events
            if self.update in sockets and sockets[self.update] == zmq.POLLIN:
                notify = self.update.recv()
                tag = ord(notify[0])
                channel = notify[1:]
                if ord(notify[0]) == 0:
                    self.subscribers[channel] -= 1
                    if self.subscribers[channel] == 0:
                        print "last subscriber unsusbscribed, unbind: ", channel
                        self.rcomp[channel].unbind()
                elif ord(notify[0]) == 1:
                    self.subscribers[channel] = self.subscribers.get(channel,0) + 1
                    print "subscribe event:", channel,self.subscribers[channel]
                    if not channel in self.rcomp.keys():
                        print "error: comp %s doesnt exist " % channel
                    else:
                        self.full_update(self.rcomp[channel])

            # timer tick
            if not sockets:
                self.timer_event()

HalServer(msec=1000)
