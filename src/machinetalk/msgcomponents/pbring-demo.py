# HAL ring command/response example
#
# send a protobuf-encoded message to HAL RT component
# receive a reply message from same

import halext
import time,os
from   types_pb2 import *
from   motcmds_pb2 import *
from   message_pb2 import Container

import google.protobuf.text_format

import pb2json
import json  # pretty printer


class Timeout(Exception):
    pass

timeout = 50.0
interval = 0.1

comp = halext.HalComponent("tmp%d" % os.getpid())
comp.ready()

try:
    c = comp.attach("command")
    r = comp.attach("response")
except NameError,e:
    print e

r.reader = c.writer = comp.id

for i in range(10):

    # construct a protobuf-encoded message
    container = Container()
    container.type = MT_MOTCMD
    motcmd = container.motcmd
    motcmd.command = EMCMOT_SET_LINE
    motcmd.commandNum = i
    pos = motcmd.pos
    t = pos.tran
    t.x = 1.0
    t.y = 2.0
    t.z = 3.0
    pos.b =  3.14
    msg = container.SerializeToString()

    # send it off
    c.write(msg,len(msg))

    # wait for response from RT component
    t = timeout
    while t > 0:
        n = r.next_size()
        if n > -1:
            b = r.next_buffer()
            reply = Container()
            reply.ParseFromString(b)

            # protobuf text format
            print "reply:", str(reply)

            # automatic JSON conversion
            jsonout = reply.SerializeToJSON()
            print json.dumps(json.loads(jsonout), indent=4)

            del reply
            r.shift()
            break
        time.sleep(interval)
        t -= interval
        if t < 0:
            raise Timeout
