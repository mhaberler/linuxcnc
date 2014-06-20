#!/usr/bin/python
from machinekit import hal
import time
h = hal.Component("passthrough")
h.newpin("in", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

try:
    while 1:
        time.sleep(1)
        print h['in']
        h['out'] = h['in']
except KeyboardInterrupt:
    raise SystemExit
