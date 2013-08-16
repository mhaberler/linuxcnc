# HAL ring example
#
# list existing rings
# attach to an existing ring and write a few messages
# create a new ring
# detach a ring

import os, time
import halext

# a ring is always attached to a component
# so create one here:
comp = halext.HalComponent("ringwrite%d" % os.getpid())
comp.ready()

# retrieve list of ring names
rings = halext.rings()
print "rings: ", rings


# print properties of an attached ring
def print_ring(r):
    print "name=%s size=%d reader=%d writer=%d scratchpad=%d" % (r.name,r.size,r.reader,r.writer,r.scratchpad_size),
    print "use_rmutex=%d use_wmutex=%d stream=%d" % (r.use_rmutex, r.use_wmutex,r.is_stream)

if "ring_0" in rings:

    # attach to existing ring
    w = comp.attach("ring_0")

    # see what we have
    print_ring(w)

    # set writer to our HAL module id
    w.writer = comp.id

    # push a few messages down the ring
    for i in range(10):
        msg = "message %d" % (i)
        w.write(msg,len(msg))
    # see what's left
    print "available: ", w.available()

    time.sleep(1)

    # investigate scratchpad region if one is defined
    if w.scratchpad_size:
        print "scratchpad:%d = '%s'" % (w.scratchpad_size, w.scratchpad)

    # since we're about to detach the ring, clear the writer id
    w.writer = 0

comp.exit()