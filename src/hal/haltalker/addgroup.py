import sys
group = sys.argv[1]
for line in sys.stdin:
    line = line.rstrip('\n')
    signame = line.replace(".","_")

    print "net %s %s" % (signame,line)

    print "newm %s %s" % (group,signame)
