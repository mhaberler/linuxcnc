#!/usr/bin/env python

# verify the cython inst bindings

from nose import with_setup
from machinekit.nosetests.realtime import setup_module ,teardown_module
from machinekit.nosetests.support import fnear
from unittest import TestCase
import time,os,ConfigParser

from machinekit import rtapi,hal

class TestIinst(TestCase):
    def setUp(self):
        self.cfg = ConfigParser.ConfigParser()
        self.cfg.read(os.getenv("MACHINEKIT_INI"))
        self.uuid = self.cfg.get("MACHINEKIT", "MKUUID")
        rt = rtapi.RTAPIcommand(uuid=self.uuid)

        rt.loadrt("icomp");
        rt.newinst("icomp","foo")
        assert len(instances) == 1
        rt.newinst("icomp","bar")
        assert len(instances) == 2
        rt.delinst("foo")
        assert len(instances) == 1
        c = hal.Component("icomp")
        for i in instances:
            assert c.id == i.owner_id
            assert c.name == i.owner().name
        assert "foo" in instances
        assert "bar" in instances
        assert instances["foo"].size > 0
        assert instances["bar"].size > 0
        try:
            x = instances["nonexistent"]
            raise "should not happen"
        except NameError:
            pass


# bonus usage example:
# for name in hal.instances():
#     i = hal.instances[name]
#     print str(i)
#     # common header fields
#     print "hdr=", i.strhdr
#     print i.name, i.type, i.strtype, i.id, i.owner_id,  i.refcnt , i.valid
#     print "size=",i.size,
#     print "owner=",i.owner.name
#     for p in i.pins:
#         print p.name, p.type, p.id, p.owner_id,  p.refcnt , p.valid, str(p)
#     print "blob size:",i.size
#     print "blob.tobytes()=",i.blob.tobytes()
#     print "blob.tolist()=",i.blob.tolist()


(lambda s=__import__('signal'):
     s.signal(s.SIGTERM, s.SIG_IGN))()
