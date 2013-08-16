# ring peek iterator demo

import os, time
import halext

# a ring is always attached to a component
# so create one here:
comp = halext.HalComponent("tmp%d" % os.getpid())
comp.ready()

name = "ring_0"
try:
    # attach to existing ring
    ring = comp.attach(name)
except NameError,e:
    print e
else:
    while True:
        # peek at the ring for a while
        for i in range(3):
            for record in ring:
                print "peek record: ", record
            time.sleep(1)

        # then consume them all:
        while True:
            record = ring.next_buffer()
            if record is None:
                break
            print "consume record: ", record
            ring.shift()
        time.sleep(1)


comp.exit()
