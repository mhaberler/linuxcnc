#!/usr/bin/env python


# Plug API scaffold - finish!


# create a ring
# create read and write plugs, and operate on them

# import os,time,sys

# from nose import with_setup
# from machinekit.nosetests.realtime import setup_module
# from machinekit import hal

# size=4096

# def test_create_ring():
#     global s,r,m,sp,srp,swp
#     s = hal.Ring("ring1", size=size, type=hal.RINGTYPE_STREAM)
#     r = hal.Ring("ring2", size=size, type=hal.RINGTYPE_RECORD)
#     m = hal.Ring("ring3", size=size, type=hal.RINGTYPE_MULTIPART)

#     sp = hal.Plug(s)        # r and w methods
#     srp = hal.ReadPlug(s)   # r methods only
#     swp = hal.WritePlug(s)  # w methods only


# def test_ring_write_read():
#     nr = 0
#     for n in range(size):
#         if swp.write("X") < 1:
#             assert n == size - 1

#     m = swp.read()
#     assert len(m) == size -1


# (lambda s=__import__('signal'):
#      s.signal(s.SIGTERM, s.SIG_IGN))()
