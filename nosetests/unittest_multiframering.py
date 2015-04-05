#!/usr/bin/env python

# create a multiframe ring
# assure record written can be read back
# exercise passing of the frame flag as either an int
# or the bitfields as defined in multiframe_flag.h

import os,time,sys

from nose import with_setup
from unittest import TestCase
from machinekit.nosetests.realtime import setup_module ,teardown_module
from machinekit import hal

class TestMultiframeRing(TestCase):
    def setUp(self):
        self.mfr = hal.Ring("mftest", size=16384, type=hal.RINGTYPE_MULTIPART)

    def test_flags(self):
        msgid = 123
        fmt = 1
        data = "foobar"
        for i in range(3):
            self.mfr.write(data, msgid = msgid + i, format = fmt+i )
        self.mfr.flush()
        assert self.mfr.ready() # there must be data in the ring
        count = 0
        for frame in self.mfr.read():
            assert frame.msgid == msgid + count
            assert frame.format == fmt + count
            assert frame.data == data
            count += 1
        assert count == 3
        self.mfr.shift()  # consume
        assert not self.mfr.ready() # ring must be empty

        data = "fasel"
        fmt = 2
        msgid = 1234
        more = True
        self.mfr.write(data, msgid=msgid, format=fmt, more=more)
        self.mfr.flush()
        assert self.mfr.ready()

        count = 0
        for frame in self.mfr.read():
            count += 1
            assert frame.msgid == msgid
            assert frame.format == fmt
            assert frame.more == more
            assert frame.data == data
        assert count == 1
        self.mfr.shift()  # consume
        assert not self.mfr.ready() # ring must be empty


(lambda s=__import__('signal'):
     s.signal(s.SIGTERM, s.SIG_IGN))()
