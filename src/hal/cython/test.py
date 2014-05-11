#!/usr/bin/env python
# vim: sts=4 sw=4 et

import time
from machinekit import hal

c = hal.Component('zzz', userarg1=42, userarg2=4711)
p  = c.pin_s32("p0", hal.hal_pin_dir.Out)
o0 = c.pin("p1", hal.hal_type.S32, hal.hal_pin_dir.In)
o1 = c.pin("p2", hal.hal_type.S32, hal.hal_pin_dir.In)
s = hal.Signal("signal", hal.hal_type.S32)
p.link(s)
s.link(o0, o1)

c.ready()

p.set(10)
print(o0.get(), o1.get())
p.set(20)
print(o0.get(), o1.get())

print c.type, c.userarg1, c.userarg2
print c.changed()

# give some time to watch in halcmd
time.sleep(10)

# exiting here will remove the comp, and unlink the pins

