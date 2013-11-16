import rtapi

# write to RTAPI log like to a file:

rtapi = rtapi.RTAPILogger(rtapi.MSG_ERR,"testdest")
print >> rtapi, "check the rtapi log"

# or directly
rtapi.write("again")
