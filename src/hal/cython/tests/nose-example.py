#!/usr/bin/env python
from nose import with_setup

import subprocess
import os

def setup_module():
    print "setup_module"
    subprocess.call(["realtime", "restart"])

def teardown_module():
    print "teardown_module"
    subprocess.call(["realtime", "stop"])


def rt_setup():
    print "rt_setup"

def rt_teardown():
    print "rt_teardown"

@with_setup(rt_setup, rt_teardown)
def test_func_setup_and_teardown():
    assert 1 == 1


