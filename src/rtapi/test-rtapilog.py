import rtapi

# write to RTAPI log like to a file:

log = rtapi.RTAPILogger(rtapi.MSG_ERR,"testdest")
print >> log, "check the rtapi log"

# or directly
log.write("again")

print "instance=%d name=%s flavor=%d" % (rtapi.instance,
                                         rtapi.instance_name, rtapi.flavor_id)
print "build=%s flags=%d" % (rtapi.build_sys, rtapi.flavor_flags)
print "flavor_name=%s" % (rtapi.flavor_name)
print "ulapi git=%s" % (rtapi.git_tag)


print "rt %d user %d" % (rtapi.rtlevel,rtapi.userlevel)

rtapi.rtlevel = 2
rtapi.userlevel = 3
print "rt %d user %d" % (rtapi.rtlevel,rtapi.userlevel)
