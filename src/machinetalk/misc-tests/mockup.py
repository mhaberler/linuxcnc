from machinekit import hal
from pprint import pprint

def wrap(name):
    r = hal.Ring(name)
    if r.type == hal.RINGTYPE_MULTIPART:
        return hal.MultiframeRing(r)
    if r.type == hal.RINGTYPE_STREAM:
        return hal.StreamRing(r)
    return r

for rname in hal.rings():
    r = wrap(rname)
    print type(r)
    r.write("foo")
    if isinstance(r, hal.MultiframeRing):
        r.flush()
    #print "flags:",r.flags
    print "name:",r.name
    print "avail:",r.available
    print "writer:",r.writer
    print "reader:",r.reader
    for p in dir(r):
        if p.startswith('__'):
            continue
        print r.name, p, getattr(r, p)
    #     #pprint (dir(r))
    # #pprint (vars(r.__dict__))
