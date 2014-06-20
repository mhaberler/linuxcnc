#!/usr/bin/env python

# test pin creation - impossible after calling c.ready()

from machinekit import hal


c1 = hal.Component('test1',userarg1=123, userarg2=456)
c1.newpin("out1", hal.HAL_S32, hal.HAL_OUT,init=42)
c1.newpin("xxx", hal.HAL_S32, hal.HAL_OUT,init=4711)
c1.ready()

print 'test1' in hal.components
print 'foo' in hal.components
