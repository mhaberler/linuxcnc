#!/usr/bin/env python

from machinekit import hal

c1 = hal.Component('test1')
p1 = c1.newpin("out1", hal.HAL_S32, hal.HAL_OUT,init=42)
p2 = c1.newpin("xxx", hal.HAL_FLOAT, hal.HAL_OUT,init=4711)
c1.ready()

c2 = hal.Component('test2')
p3 = c1.newpin("out1", hal.HAL_S32, hal.HAL_OUT,init=42)
p4 = c1.newpin("xxx", hal.HAL_FLOAT, hal.HAL_OUT,init=4711)
c1.ready()


print "p1.type", p1.type,hal.pins['test1.out1'].type,hal.pins["test1.out1"].get()
print "p2.type", p2.type,hal.pins['test1.xxx'].type,hal.pins['test1.xxx'].get()

sig = hal.new_sig("nfs1", hal.HAL_FLOAT)

print hal.signals["nfs1"]

print "sig.type",sig.type




# type mismatch
hal.net("nfs1", "test1.out1")

#p = pins['test1.out']
del hal.signals["nfs1"]
