import subprocess
import os
from subprocess import Popen, PIPE

# def setup_module():
#     p = Popen(["realtime", "restart"], stdout=PIPE)
#     output = p.communicate()[0]
#     if p.returncode:
#         raise Exception, "realtime restart failed: " + output

# def teardown_module():
#     p = Popen(["realtime", "stop"], stdout=PIPE)
#     output = p.communicate()[0]
#     if p.returncode:
#         raise Exception, "realtime stop failed: " + output


def setup_module():
    my_env = os.environ.copy()
    #my_env["DEBUG"] = "5"
    subprocess.call(["realtime", "restart"], env=my_env)

def teardown_module():
    subprocess.call(["realtime", "stop"])
