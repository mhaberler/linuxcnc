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
import sys, time
cname = "mptx"

class Timeout(Exception):
    pass

timeout = 50.0
interval = 0.1

comp = halext.HalComponent("tmp%d" % os.getpid())
comp.ready()

try:
    c = comp.attach("%s.0.in" % cname,halext.RINGTYPE_MULTIPART)
#    print c
#    r = comp.attach("%s.0.out" % cname,halext.RINGTYPE_MULTIPART)
except NameError,e:
    print e
    raise
#print "----"
#r.reader = comp.id
c.writer = comp.id
#c.commit()
c.append("blah",4,12312)
c.commit()

c.append("bar",3,4711)
c.append("foo",3,42)
c.append("xxx",3,42)
c.append("fas",3,42)
c.append("fas",3,42)
c.append("fxx",3,42)
c.commit()

c.append("foo",3,42)
c.append("bar",3,4711)
c.append("baz",3,1234)
c.append("blah",4,126)
c.commit()
sys.exit(0)


c.commit()
time.sleep(1)
sys.exit(0)
#print dir(c),c


print c
comp.exit()

# for i in range(10):

#     # construct a protobuf-encoded message
#     container = Container()
#     container.type = MT_MOTCMD
#     motcmd = container.motcmd
#     motcmd.command = EMCMOT_SET_LINE
#     motcmd.commandNum = i
#     pos = motcmd.pos
#     t = pos.tran
#     t.x = 1.0
#     t.y = 2.0
#     t.z = 3.0
#     pos.b =  3.14
#     msg = container.SerializeToString()

#     # send it off
#     c.write(msg,len(msg))

#     # wait for response from RT component
#     t = timeout
#     while t > 0:
#         n = r.next_size()
#         if n > -1:
#             b = r.next_buffer()
#             reply = Container()
#             reply.ParseFromString(b)

#             # protobuf text format
#             print "reply:", str(reply)

#             # automatic JSON conversion
#             jsonout = reply.SerializeToJSON()
#             print json.dumps(json.loads(jsonout), indent=4)

#             del reply
#             r.shift()
#             break
#         time.sleep(interval)
#         t -= interval
#         if t < 0:
#             raise Timeout
