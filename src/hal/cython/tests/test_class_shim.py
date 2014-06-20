from nose import with_setup
import sys
import inspect
from unittest import TestCase

def setup_module():
    """ Setup module """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 


def teardown_module():
    """ Teardown module """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 


def function_setup():
    """ Setup a function """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 


def function_teardown():
    """ Teardown a function """
    print >> sys.stderr, "Calling ",inspect.stack()[0][3] 

class TestClass(TestCase):
    def setUp(self):
        print >> sys.stderr, "Calling %s.%s" % (self.__class__.__name__,inspect.stack()[0][3])

    def test_something(self):
        print >> sys.stderr, "Calling %s.%s" % (self.__class__.__name__,inspect.stack()[0][3])
        # produce a failure - uncomment:
        #assert False

    def tearDown(self):
        print >> sys.stderr, "Calling %s.%s" % (self.__class__.__name__,inspect.stack()[0][3])

