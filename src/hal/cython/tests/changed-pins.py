#!/usr/bin/env python

# example of automatic pin change detection with using the
# HAL compiled componets API (internally in the Cython binding)

from os import getpid
from machinekit import hal

# permit reading out pins (default anyway)
hal.relaxed = True

c = hal.Component('changetest')
c.newpin("out1", hal.HAL_S32, hal.HAL_OUT,init=42)
c.newpin("out2", hal.HAL_S32, hal.HAL_OUT,init=4711)
c.newpin("in1", hal.HAL_S32, hal.HAL_IN)
c.newpin("in2", hal.HAL_S32, hal.HAL_IN)
c.ready()

# using introspection
assert c.type == hal.TYPE_USER
assert c.state == hal.COMP_READY
assert c.pid == getpid()

# Component.changed() takes an optional userdata=<callable|list> argument:
#
# userdata=<callable>:
#
# a callable must have signature (int, str, object)
# it is called at the start of the report with phase == hal.REPORT_BEGIN
# then for each changed pin with phase == hal.REPORT_PIN
# now name and value are contain the pin's name and current value
# the report is concluded with phase == hal.REPORT_END
#
# userdata=<list>:
# any changed pins are appended to the list as (name, value) tuples
#
# Component.changed() returns the number of changed pins or < 0 on error.

# example using a callable:

class ReportCallback:
    def report_callback(self, phase, name, value):
        if phase == hal.REPORT_BEGIN:
            self.pins = []
        elif phase == hal.REPORT_PIN:
            self.pins.append((name,value))
        elif phase == hal.REPORT_END:
            print "report complete: changed pin values=", self.pins
        else:
            raise RuntimeError("ReportCallback: invalid phase %d" % phase)
        return 0

cb = ReportCallback()

# this should report out1 and out2 as they do not default to 0:
assert c.changed(userdata=cb.report_callback) == 2

# this will not report any changes because now tracking:
assert  c.changed(userdata=cb.report_callback) == 0

# change pin values
c["out1"] += 1
c["out2"] += 1

# this again triggers callbacks:
assert  c.changed(userdata=cb.report_callback) == 2

# this doesnt - no change relative to previous call:
assert c.changed(userdata=cb.report_callback) == 0

# example using a list:

pinlist = []
assert c.changed(userdata=pinlist) == 0
assert pinlist == []

# change pin values
c["out1"] += 1
c["out2"] += 1

pinlist = []
assert c.changed(userdata=pinlist) == 2
assert len(pinlist) == 2
print pinlist

