#!/usr/bin/env python
import os,time,sys
import zmq
import time
import sys
import ConfigParser
from machinekit import rtapi,hal

cname  = "command"
rname  = "response"
timeout = 100

def multiframe_ring(name):
    try:
        r = hal.Ring(name)
    except RuntimeError:
        r = hal.Ring(name, size=16384, flags=hal.RINGTYPE_MULTIPART)
    return (r,hal.MultiframeRing(r))

def rtapi_connect():
    cfg = ConfigParser.ConfigParser()
    cfg.read(os.getenv("MACHINEKIT_INI"))
    uuid = cfg.get("MACHINEKIT", "MKUUID")
    return rtapi.RTAPIcommand(uuid=uuid)

def zmq_setup():
    context = zmq.Context()
    socket = context.socket(zmq.ROUTER)
    socket.bind("tcp://127.0.0.1:5700")
    poller = zmq.Poller()
    poller.register(socket, zmq.POLLIN)
    return (context,socket,poller)

(context,socket,poller) = zmq_setup()

# allocate the rings
(command, mfcommand)   = multiframe_ring(cname)
(response, mfresponse) = multiframe_ring(rname)

command.writer  = os.getpid()
response.reader = os.getpid()

rt = rtapi_connect()
rt.loadrt("micromot", "command=%s" % cname, "response=%s" % rname)
rt.newthread("fast",  1000000, use_fp=True)

hal.addf("micromot","fast")
hal.start_threads()

try:
    while True:
        events = dict(poller.poll(timeout))
        if socket in events and events[socket] == zmq.POLLIN:
            request = socket.recv_multipart()
            for frame in request:
                mfcommand.write(frame, 0)
            mfcommand.flush()

        # poll response ring
        if mfresponse.ready():
            reply = []
            for frame in mfresponse.read():
                #print "got:",(frame.data.tobytes(), frame.flags)
                reply.append(frame.data)
            mfresponse.shift()

            if reply:
                socket.send_multipart(reply)

except (KeyboardInterrupt,zmq.error.ZMQError) as e:

    hal.stop_threads()
    command.writer  = 0
    response.reader = 0

    rt.delthread("fast")
    rt.unloadrt("micromot")
