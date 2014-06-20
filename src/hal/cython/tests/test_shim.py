from nose import with_setup
import sys
import inspect
from unittest import TestCase

def setup_module():
    """ Setup module """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 
    pass

def teardown_module():
    """ Teardown module """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 
    pass

def function_setup():
    """ Setup a function """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 
    pass


def function_teardown():
    """ Teardown a function """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 
    pass

# @with_setup(function_setup, function_teardown)
# def test_a_function():
#     """ Test a function """
#     print >> sys.stderr, "Calling ",inspect.stack()[0][3] 
 
#
class TestClass(TestCase):
    def setUp(self):
        print >> sys.stderr, "Calling ",inspect.stack()[0][3] 
        #assert False

    def test_something(self):
        #""" A cool test """
        print >> sys.stderr, "Calling ",inspect.stack()[0][3] 

        #assert 0 < len("Python is the shiznit")

    def tearDown(self):
        """ TestClass:tearDown(..) """
        print >> sys.stderr, "Calling ",inspect.stack()[0][3] 

        pass
