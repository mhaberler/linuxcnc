#!/usr/bin/env python

# validate lifetime check on signal wrappers

from machinekit import hal
import sys

try:
    s = hal.signals["signal"]

except KeyError:
    s = hal.Signal("signal", hal.hal_type.S32)

# signal must exist, and so this reference must succeed
s.name

# now delete underlying HAL signal
# could just as well be 'halcmd delsig signal'
s.delete()

# this must fail since the wrapper is now referring to a deleted signal
try:
    s.name
    print "reference to a name of a deleted signal must fail"
    sys.exit(1)
except RuntimeError:
    sys.exit(0)

