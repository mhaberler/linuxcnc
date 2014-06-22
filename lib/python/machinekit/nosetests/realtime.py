import subprocess
import os
from subprocess import Popen, PIPE

def setup_module():
    subprocess.call("realtime restart", shell=True,stderr=subprocess.STDOUT,env=os.environ.copy())

def teardown_module():
    subprocess.call("realtime stop", shell=True,stderr=subprocess.STDOUT,env=os.environ.copy())
