#
# interpret a g-code file, and generate preview commands
#


import sys
import getopt
import preview as gcode
import time

class Dummy:
    pass

def run(filename,canon, unitcode, initcode):
    result, last_sequence_number = gcode.parse(filename, canon, unitcode, initcode)

    # XXX mystery attributes
    # print "gcodes", gcode.linecode.gcodes
    # print "sequence_number", gcode.linecode.sequence_number

    if result > gcode.MIN_ERROR:
        print " gcode error, line %d: %s " % (canon.lineno, gcode.strerror(result))
        print " last_sequence_number ",last_sequence_number
    else:
        pass
        # # XXX: unclear how this is supposed to work
        # minxt,maxxt,min_t_xt,max_t_xt = gcode.calc_extents()
        # print "X extent: %.2f .. %.2f" % (minxt[0],maxxt[0])
        # print "Y extent: %.2f .. %.2f" % (minxt[1],maxxt[1])
        # print "Z extent: %.2f .. %.2f" % (minxt[0],maxxt[2])
        # print "X extent w/tool: %.2f .. %.2f" % (min_t_xt[0],max_t_xt[0])
        # print "Y extent w/tool: %.2f .. %.2f" % (min_t_xt[1],max_t_xt[1])
        # print "Z extent w/tool: %.2f .. %.2f" % (min_t_xt[0],max_t_xt[2])


def main():
    canon = Dummy()
    # canon expects a 'parameter_file' attribute
    #canon.parameter_file = "sim.var"
    # corresponds to Axis ini RS274NGC PARAMETER_FILE value
    canon.parameter_file = ""

    # XXX mystery parameter
    # executed before startupcode - to set machine units (G20,G21)?
    unitcode = ""
    # corresponds to Axis ini RS274NGC RS274NGC_STARTUP_CODE value
    initcode = "G17 G20 G40 G49 G54 G80 G90 G94"
    filename = "test.ngc"

    try:
        opts, args = getopt.getopt(sys.argv[1:], "s:i:f:")
    except getopt.error, msg:
        print msg
        sys.exit(2)

    for o, a in opts:
        if o in ("-h", "--help"):
            print __doc__
            sys.exit(0)
        if o in ("-s"):
            startupcode = a
        if o in ("-i"):
            initcode = a
        if o in ("-p"):
            canon.parameter_file = a


    for arg in args:
        run(arg,canon, unitcode, initcode)
    else:
        run(filename,canon, unitcode, initcode)

if __name__ == "__main__":
    main()
    time.sleep(2) # let sockets drain