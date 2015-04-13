from machinekit import hal
from machinekit import nanopb

mu = hal.Ring("mu")
while mu.ready():
    for frame in mu.read():
        print '[',frame.flags, frame.data.tobytes(),']',
    print
    mu.shift()
