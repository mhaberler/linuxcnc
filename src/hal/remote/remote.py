from halext import *
import time,os,sys

if not 'demofloat' in signals():
    newsig("demofloat", HAL_FLOAT)
    newsig("demobit", HAL_BIT)
    newsig("demos32", HAL_S32)
    newsig("demou32", HAL_U32)

loop = 20
comps = components()
print comps
for k,v in comps.iteritems():
    print k, v.name

name = "demo"
if not name in comps:
    rcomp = HalComponent(name,TYPE_REMOTE)
    rcomp.newpin(name + ".f",  HAL_FLOAT, HAL_IN);
    rcomp.newpin(name + ".s",  HAL_S32, HAL_IN);
    rcomp.newpin(name + ".in",  HAL_BIT, HAL_IN);
    rcomp.newpin(name + ".out", HAL_BIT, HAL_OUT);
    rcomp.newparam(name + ".speed", HAL_S32, HAL_RW);
    rcomp.ready()
else:
    rcomp = comps['demo']

rcomp['demo.speed'] = 4711

if 'myring' in rings():
    ring = rcomp.attach('myring')
else:
    ring = rcomp.create('myring', 4096, stream=True,scratchpad=1024)


print ring, ring.scratchpad_size, ring.size,ring.is_stream
ring.write("foo",3)


if 'rring' in rings():
    rring = rcomp.attach('rring')
else:
    rring = rcomp.create('rring', 4096, stream=False,scratchpad=1024)
if 'sring' in rings():
    sring = rcomp.attach('sring')
else:
    sring = rcomp.create('sring', 4096, stream=True,scratchpad=1024)

for i in range(5):
    msg = "record #%d" % (i)
    rring.write(msg,len(msg))
    sring.write(msg,len(msg))

print rring.flush(),sring.flush()

iter = RingIter(rring)
for _ in xrange(6):
    print iter.read(), iter.shift()

# iter2 = RingIter(rring)
# assert(str(iter2.read()) == 'record #0')

for p in rcomp.params():
    print p.name, p.value, p.dir, p.type,p.owner.name

for name,s  in signals().iteritems():
    print name, s.value, s.type, s.readers,s.writers, s.bidirs

# the halcmd 'net' command in Python:
net("demofloat", "demo.f")
net("demos32", "demo.s")
net("demobit", "demo.in")

if rcomp.pid:
    raise RuntimeError, "component %s already held by pid %d" % (rcomp.name, rcomp.pid)
rcomp.acquire()

if rcomp.state != COMP_UNBOUND:
    raise RuntimeError, "component %s not unbound: state %d" % (rcomp.name, rcomp.state)
rcomp.bind()

rcomp['demo.f'] = 2.718
def pinreport(pinlist):
    for p in pinlist:
        print "\t", p.name,p.value,p.dir,p.type,p.flags,p.epsilon

print rcomp['demo.in'],rcomp['demo.f'],rcomp['demo.s']


pinreport(rcomp.pins())
while True:
    try:
        pinreport(rcomp.changed_pins())
        time.sleep(1)
    except KeyboardInterrupt:
        print "^C"
        rcomp.unbind()
        rcomp.release()
        rcomp.exit()
        break
