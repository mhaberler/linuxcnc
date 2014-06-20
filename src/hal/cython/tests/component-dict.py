#!/usr/bin/env python

# test pin creation - impossible after calling c.ready()

from machinekit import hal
import sys

def printcomp(c):
    print c, c.name, c.type, c.state, c.id, c.userarg1,c.userarg2
    print "pins: ", c.pins()

def printpin(p):
    print p, p.name, p.type, p.dir, p.eps, p.handle,p.get()


c1 = hal.Component('test1',userarg1=123, userarg2=456)
c1.newpin("out1", hal.HAL_S32, hal.HAL_OUT,init=42)
c1.newpin("xxx", hal.HAL_S32, hal.HAL_OUT,init=4711)
c1.ready()

c2 = hal.Component('test2')
c2.newpin("out1", hal.HAL_S32, hal.HAL_OUT,init=4711)
c2.ready()

printcomp(c1)
printcomp(c2)

printcomp(hal.components['test1'])
printcomp(hal.components['test2'])


print "# of comps: ", len(hal.components)
print "comp names: ", hal.components()

print "# of pins: ", len(hal.pins)
print "pins names: ", hal.pins()
for n in hal.pins():
    p = hal.pins[n]
    printpin(p)

xx = hal.components['test2']

try:
    xx.exit()
except Exception,e:
    print "Exception:", e

print xx

# try:
#     c.newpin("in2", hal.HAL_S32, hal.HAL_IN)
#     print "could create pin after ready() ?"
#     sys.exit(1)
# except RuntimeError:
#     sys.exit(0)
