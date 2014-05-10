#!/usr/bin/env python
# vim: sts=4 sw=4 et

def os_key(key, instance):
    return (( key & 0x00ffffff) | ((instance << 24) & 0xff000000))

def instance_of(key):
    return (((key & 0xff000000) >> 24) & 0x000000ff)

import time
from machinekit import rtapi
print dir(rtapi)
print "HAL_KEY %x " % rtapi._HAL_KEY
print str(rtapi._HAL_KEY)
print dir(rtapi._HAL_KEY)

s = rtapi.RtapiModule("foobar")

shm = s.shmem(4711, 123)
print shm,len(shm)

h =  s.shmem(os_key( 0x00414C32,0))#rtapi.HAL_KEY, 0))
print h,len(h)


time.sleep(1)

# exiting here will remove the comp, and unlink the pins

