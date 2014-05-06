import hal
from struct import *

key = 4711
size= 0

# this is just for the hal_init() side effect
c = hal.component("notused")

# either this
sm = hal.shm(c,key)
# or this
# sm = hal.shm(c,key,0)
# will attach an existing shared memory segment and
# set the buffer's size to the segment's size

b = sm.getbuffer()
print "shm size: ", len(b)

(nsamples,nx,ny) = unpack('iii',b[0:12])
print "nsamples=%d nx=%d ny=%d \n" % (nsamples,nx,ny)

base = 16
for i in range(10):
    start = base + i *8
    (sample,) = unpack('d', b[start:start+8])
    print sample
