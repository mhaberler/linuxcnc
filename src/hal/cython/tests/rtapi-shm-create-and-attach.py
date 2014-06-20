#!/usr/bin/env python

# example for using the RTAPI shm binding

import time
from machinekit import rtapi,hal
import sys,os

def os_key(key, instance):
    return (( key & 0x00ffffff) | ((instance << 24) & 0xff000000))

def instance_of(key):
    return (((key & 0xff000000) >> 24) & 0x000000ff)

print "HAL_KEY %x " % rtapi._HAL_KEY
print "GLOBAL_KEY %x " % rtapi._GLOBAL_KEY

# create a new RTAPI module
#
# if realtime not running, will raise (confusingly):
#     RuntimeError: Fail to create RTAPI module: Function not implemented

r = rtapi.RtapiModule("foobar")

# have it create a shared memory segment
shm = r.shmem(4711, 123)

# show properties
print shm,len(shm)

# attach the existing HAL shared memory segment

instance = 0
halmem =  r.shmem(os_key(rtapi._HAL_KEY, instance))
print halmem,len(halmem)

l = rtapi.RTAPILogger(tag="foo",level=rtapi.MSG_ERR)
#override tag
l.tag = "other"
print >> l, "other message",
l.tag = "foo"
print >> l, "foo message",

r = rtapi.RTAPIcommand(uuid="a42c8c6b-4025-4f83-ba28-dad21114744a")
r.newthread("servo-thread",1000000,use_fp=True)

ninst = 3
r.loadrt("or2","count=%d" % ninst)
for i in range(ninst):
    hal.addf("or2.%d" % i, "servo-thread")

hal.start_threads()

time.sleep(1)

hal.stop_threads()
r.delthread("servo-thread")
r.unloadrt("or2")


# the RTAPI module gets deallocated on exit
