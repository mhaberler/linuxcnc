from halext import *

import os

rcomp = HalComponent("remote",TYPE_REMOTE)
rcomp.ready()



halserver =  HalComponent("localusr", TYPE_USER)
halserver.ready()

print "pid=",os.getpid()
print "halserver=",halserver.pid
print "rcomp=",rcomp.pid

halserver.exit()
rcomp.exit()
