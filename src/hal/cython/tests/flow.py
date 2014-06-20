#from nose import with_setup
import sys
from unittest import TestCase

# def setup_module():
# #    """ Setup module """
#     print >> sys.stderr, "Calling Setup module"
#     pass

# def teardown_module():
# #    """ Teardown module """
#     print >> sys.stderr, "Calling Teardown module"
#     pass

# def function_setup():
# #    """ Setup a function """
#     print >> sys.stderr, "Calling function_setup"
#     pass
#     #assert False

# def function_teardown():
# #    """ Teardown a function """
#     print >> sys.stderr, "Calling function_teardown"
#     pass

#-@-with_setup(function_setup, function_teardown)
# def test_a_function():
#     """ Test a function """
#     print >> sys.stderr, "Calling Test a function"
#     raise Exception, "Calling Test a function"

# #
#class TestClass(TestCase):
#     def setUp(self):
#         """ TestClass:setUp(..) """
# #        print >> sys.stderr, "Calling TestClass:setUp"
#         assert False

#     def test_something(self):
# #        """ A cool test """
#         assert 0 < len("Python is the shiznit")

#     def tearDown(self):
# #        """ TestClass:tearDown(..) """
#         print >> sys.stderr, "Calling TestClass:tearDown"
#         pass

# def test_evens():
#     print >> sys.stderr, "Calling test_evens"

#     for i in range(0, 5):
#         yield check_even, i, i*3

# def check_even(n, nn):
#     print >> sys.stderr, "check_even",n,nn

#     assert n % 2 == 0 or nn % 2 == 0
