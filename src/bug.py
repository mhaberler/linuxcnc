#!/usr/bin/env python

from machinekit import hal,rtapi
from machinekit.nosetests.rtapilog import Log

import os,time
import sys, traceback

l = Log(level=rtapi.MSG_INFO,tag="nosetest")

def test_component_creation():
    global c1, c2
    l.log()
    c1 = hal.Component("c1")
    c1.newpin("s32out", hal.HAL_S32, hal.HAL_OUT, init=42)
    c1.newpin("s32in", hal.HAL_S32, hal.HAL_IN)
    c1.newpin("s32io", hal.HAL_S32, hal.HAL_IO)
    c1.newpin("floatout", hal.HAL_FLOAT, hal.HAL_OUT, init=42)
    c1.newpin("floatin", hal.HAL_FLOAT, hal.HAL_IN)
    c1.newpin("floatio", hal.HAL_FLOAT, hal.HAL_IO)

    c1.ready()

    c2 = hal.Component("c2")
    c2.newpin("s32out", hal.HAL_S32, hal.HAL_OUT, init=4711)
    c2.newpin("s32in", hal.HAL_S32, hal.HAL_IN)
    c2.newpin("s32io", hal.HAL_S32, hal.HAL_IO)
    c2.newpin("floatout", hal.HAL_FLOAT, hal.HAL_OUT, init=4711)
    c2.newpin("floatin", hal.HAL_FLOAT, hal.HAL_IN)
    c2.newpin("floatio", hal.HAL_FLOAT, hal.HAL_IO)
    c2.ready()

    l.log()
    #os.system("halcmd show")

    assert hal.pins["c1.s32out"].linked is False
    assert hal.pins["c2.s32in"].linked is False
    assert "c2.s32in" in hal.pins
    assert "c1.s32out" in hal.pins
    # out to in is okay
    hal.net("c1.s32out", "c2.s32in")
    assert "c2.s32in" in hal.pins
    assert "c2.s32in" in hal.pins
    assert 'c1-s32out' in hal.signals
    assert hal.pins["c1.s32out"].linked is True
    assert hal.pins["c2.s32in"].linked is True
    l.log()

if __name__ == "__main__":
    try:
        test_component_creation()
    except Exception:
        exc_type, exc_value, exc_traceback = sys.exc_info()
        print repr(traceback.format_exception(exc_type, exc_value,
                                              exc_traceback))
    os.system("halcmd show")
    time.sleep(100000)

(lambda s=__import__('signal'):
    s.signal(s.SIGTERM, s.SIG_IGN))()
