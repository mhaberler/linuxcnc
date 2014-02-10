import os, time
import zmq
from optparse import OptionParser
from   types_pb2 import *
from   motcmds_pb2 import *
from   message_pb2 import Container

import google.protobuf.text_format

parser = OptionParser()

parser.add_option("-C", "--cmdout", dest="cmdoutput", default="tcp://127.0.0.1:5570",
                  help="URI to submit commands to")

parser.add_option("-r", "--responsein", dest="responseinput",
                  default="tcp://127.0.0.1:5573",
                  help="URI to fetch responses from")

parser.add_option("-n", "--name", dest="actor", default="task",
                  help="use this as actor name")

parser.add_option("-d", "--destination", dest="destination", default="component",
                  help="use this actor as command destination")

parser.add_option("-b", "--batch", dest="batch", default=1,type="int",
                  help="use this actor as command destination")

parser.add_option("-i", "--iterations", dest="iter", default=10,type="int",
                  help="to run main loop")

parser.add_option("-v", "--verbose", action="store_true", dest="verbose",
                  help="print actions as they happen")

parser.add_option("-F", "--fast", action="store_true", dest="fast",
                  help="do not sleep after an iteration")

(options, args) = parser.parse_args()

me = options.actor

context = zmq.Context()

cmdout = context.socket(zmq.DEALER)
cmdout.setsockopt(zmq.IDENTITY, me)
cmdout.connect(options.cmdoutput)

responsein = context.socket(zmq.SUB)
responsein.connect(options.responseinput)
responsein.setsockopt(zmq.SUBSCRIBE, me)

i = 0

container = Container()

time.sleep(1) # let subscriptions stabilize
for j in range(options.iter):

    for n in range(options.batch):
        container.type = MT_MOTCMD
        motcmd = container.motcmd
        motcmd.command = EMCMOT_SET_LINE
        motcmd.commandNum = i
        pos = motcmd.pos
        t = pos.tran
        t.x = 1.0 + i
        t.y = 2.0 + i
        t.z = 3.0 + i
        pos.b =  3.14
        pbmsg = container.SerializeToString()

        i += 1
        if options.verbose:
            print "---%s send command to %s: %s" % (me,options.destination, pbmsg)
        cmdout.send_multipart([options.destination,pbmsg])

    for n in range(options.batch):
        response = responsein.recv_multipart()
        reply = Container()
        reply.ParseFromString(response[2])
        if options.verbose:
            print "---%s receive response: %s" %(me, str(reply))
    if not options.fast:
        time.sleep(1)

context.destroy(linger=0)
