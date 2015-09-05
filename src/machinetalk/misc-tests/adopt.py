from machinekit import hal
from pprint import pprint
import zmq
import time
import sys
import types_pb2

context = zmq.Context()

if not "foo" in hal.rings():
    hal.Ring("foo", size=16384, type=hal.RINGTYPE_STREAM)
if not "mu" in hal.rings():
    hal.Ring("mu", size=16384, type=hal.RINGTYPE_MULTIPART)
if not "bar" in hal.rings():
    hal.Ring("bar", size=16384, type=hal.RINGTYPE_RECORD)

foo = hal.Ring("foo")
mu = hal.Ring("mu")
bar = hal.Ring("bar")

foo.write("fasel")

for i in range(3):
    bar.write("fasel %d" % (i))

for j in range(2):
    for i in range(2):
        mu.write("fasel %d" % (i),msgid=j+13, format=i)
        mu.write("fasel %d" % (i*3),msgid=j+i*5, format=i)
    mu.flush()

# for j in range(2):
#     for f in mu.read():
#         print f.data.tobytes(),f.msgid,f.format
#     mu.shift()

try:
    hal.delete_ring("to-rt")
    hal.delete_ring("from-rt")
    hal.delete_ring("status")
except Exception:
    pass

tort = hal.Ring("to-rt", size=16384, type=hal.RINGTYPE_MULTIPART, adopt=True,
                announce=True, encodings=1, haltalk_writes=True, sockettype=6) # ROUTER
fromrt= hal.Ring("from-rt", size=16384, type=hal.RINGTYPE_MULTIPART)

status = hal.Ring("status", size=16384, type=hal.RINGTYPE_MULTIPART, adopt=True,
                announce=True, encodings=1, sockettype=1) # PUB

hal.pair_rings("to-rt", "from-rt")

adoptees = []
for rname in hal.rings():
    r = hal.Ring(rname)
    if r.adopt:
        print "adopting: ", r.name
        adoptees.append(r)
    else:
        print "skipping: ", r.name

class Channel:
    def __init__(self):
        self.inring = None
        self.outring = None
        self.socket = None
        self.uri = None
        self.name = None

poller = zmq.Poller()
timeout = 1000 #msec
channels = []
for r in adoptees:
    c = Channel()
    if r.haltalk_writes:
        c.name = r.name
        c.outring = r
        if r.paired_id:
            c.inring = r.paired_ring()
    else:
        c.name = r.name
        c.inring = r
        if r.paired_id:
            c.outring = r.paired_ring()

    if r.sockettype == zmq.PUB:
        c.socket = context.socket(zmq.PUB)
    elif r.sockettype == zmq.ROUTER:
        c.socket = context.socket(zmq.ROUTER)
        poller.register(c.socket, zmq.POLLIN)

    else:
        raise RuntimeError, "Socket type not supported: %d" % r.sockettype

    c.socket.bind("tcp://127.0.0.1:*")
    c.uri = c.socket.getsockopt(zmq.LAST_ENDPOINT)

    channels.append(c)

while True:
    socks = dict(poller.poll(timeout))
    for c in channels:
        if socks:
            if socks.get( c.socket ) == zmq.POLLIN:
                msg = c.input.recv_multipart()
                print "%s ready: [%s]" % (c.name, msg)
        else:
            if c.inring:
                msg = []
                mrframes = []
                if c.inring.type == hal.RINGTYPE_MULTIPART:
                    for frame in c.inring.read():
                        msg += frame.data
                        mrframes += frame
                    c.inring.shift()

                elif c.inring.type == hal.RINGTYPE_RECORD:
                    for i in c.inring:
                        msg += i.tobytes()
                        c.inring.shift()
                else:
                    # stream
                    msg = [c.inring.read()]
                if msg:
                    print c.name, msg
