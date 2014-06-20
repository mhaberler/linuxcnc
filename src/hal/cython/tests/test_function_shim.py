# example for test functions
# with module and per-function setup+teardown

from nose import with_setup
import sys
import inspect
from unittest import TestCase

def setup_module():
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 

def teardown_module():
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 

def function_setup():
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 

def function_teardown():
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 

@with_setup(function_setup, function_teardown)
def test_a_function():
    """ Test a function """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 
#    assert False
