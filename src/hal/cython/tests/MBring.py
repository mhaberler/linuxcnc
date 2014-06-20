#!/usr/bin/env python

import os,time,sys

from machinekit import hal

try:
    r1 = hal.Ring("ring1")
    print "attaching ring:",r1.name,r1.size
except Exception:
    print "creating ring"
    r1 = hal.Ring("ring1", size=4096)


try:
    r2 = hal.Ring("ring2")
    print "attaching ring:",r2.name,r2.size
except Exception:
    print "creating ring"
    r2 = hal.Ring("ring2", size=4096)

nr = 0
count = 100
for n in range(count):
    r1.write("record %d" % n)
    record = r1.read()
    if record is None:
        raise RuntimeError, "no record after write %d" % n
    nr += 1
    r1.shift()
assert nr == count
record = r1.read()
assert record is None # ring must be empty



for n in range(3):
    r1.write("record %d" % n)

# iterator usage : non-destructive read
i = hal.RingIter(r1)
for _ in range(3):
    print("iterate:", i.read().tobytes())
    i.shift()

# only now the frames are actually consumed:
while True:
    r = r1.read()
    if r is None:
        break
    print("consume:",r.tobytes())
    r1.shift()



mb = hal.BufRing(r2)

for n in range(3):
    mb.write("record %d" % n,n +4711)
    mb.flush()

i = hal.RingIter(r2)
for _ in range(3):
    print("iterate:", i.read().tobytes())
    i.shift()

for f in mb.read():
    print(f.data.tobytes(), f.flags)
    mb.shift()





# mb.write(b'xxx', 1)
# mb.write(b'yyy', 2)
# mb.write(b'zzz', 3)

# i = hal.RingIter(mb)

# for _ in range(3):
#     print(i.read().tobytes())
#     print(i.shift())

# i = hal.RingIter(mb)
# for _ in range(3):
#     print(i.read().tobytes())
#     print(i.shift())

# while 
# mport pyring as ring

# r = ring.Ring(1024)

# r.write(b'xxx') #, 3)
# r.write(b'yyy') #, 3)
# r.write(b'zzz') #, 3)

# i = ring.RingIter(r)
# for _ in range(3):
# 	print(i.read().tobytes())
# 	print(i.shift())

# for b in r:
# 	print(b.tobytes())

# i = ring.RingIter(r)
# assert(i.read().tobytes() == b'xxx')

# r = ring.Ring(1024)
# br = ring.BufRing(r)

# br.write(b'xxx', 1)
# br.write(b'yyy', 2)
# br.flush()

# print("First")
# for f in br.read():
# 	print(f.data.tobytes(), f.flags)
# br.shift()

# print("Second")
# for f in br.read():
# 	print(f.data.tobytes(), f.flags)
