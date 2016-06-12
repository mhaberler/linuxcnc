#!/usr/bin/python

# shows how notifications via fd can be integrated
# into fd-based event loops

# NB: no more polling or busy-waits


from machinekit import passfd
import zmq
import os
import struct

fdname = "fromhal.fd"
p = passfd.Passfd()
fd = p.fetch(fdname)


context = zmq.Context()
socket = context.socket(zmq.ROUTER)
socket.bind("tcp://127.0.0.1:5700")
poller = zmq.Poller()
poller.register(socket, zmq.POLLIN)
poller.register(fd, zmq.POLLIN)

while True:
    events = dict(poller.poll())
    if socket in events:
        print socket.recv_multipart()

    if fd in events:
        buf = os.read(fd,8)
        # eventfd read returns an uint64_t
        n = struct.unpack("=Q", buf)[0]
        print fdname, n
