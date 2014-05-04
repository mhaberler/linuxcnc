import os, time
import zmq
from message_pb2 import Container
from types_pb2 import *
from optparse import OptionParser

parser = OptionParser()

parser.add_option("-c", "--cmd", dest="cmduri", default="tcp://127.0.0.1:5571",
                  help="command URI")

parser.add_option("-r", "--response", dest="responseuri",
                  default="tcp://127.0.0.1:5573",
                  help="response URI")

parser.add_option("-n", "--name", dest="actor", default="task",
                  help="use this as actor name")

parser.add_option("-d", "--destination", dest="destination", default="component",
                  help="use this actor as command destination")

parser.add_option("-b", "--batch", dest="batch", default=1,type="int",
                  help="use this actor as command destination")

parser.add_option("-i", "--iterations", dest="iter", default=1,type="int",
                  help="to run main loop")

parser.add_option("-v", "--verbose", action="store_true", dest="verbose",
                  help="print actions as they happen")

parser.add_option("-F", "--fast", action="store_true", dest="fast",
                  help="do not sleep after an iteration")

(options, args) = parser.parse_args()

me = options.actor

context = zmq.Context()

cmd = context.socket(zmq.XSUB)
cmd.connect(options.cmduri)
# subscribe XSUB-style by sending a message  \001<topic>
cmd.send("\001%s" % (me))

response = context.socket(zmq.XSUB)
response.connect(options.responseuri)
response.send("\001%s" % (me))


i = 0
tx1 = Container()
tx1.type = MT_EMCMOT_SET_LINE

tx2 =  Container()
tx2.type = MT_RTMESSAGE
rtm = tx2.rtmessage.add()
rtm.type = MT_RTMESSAGE0
rtm.foo = 4711
rtm.dest.tran.x = 2.0
rtm.dest.tran.y = 1.0
rtm.dest.tran.x = 3.0
rtm = tx2.rtmessage.add()
rtm.type = MT_RTMESSAGE1
rtm.foo = 815
rtm.dest.tran.x = 3.14
rtm.dest.tran.y = 2.718
rtm.dest.tran.x = 1.1415

time.sleep(1) # let subscriptions stabilize
for j in range(options.iter):

    for n in range(options.batch):
        msg = "cmd %d" % i
        i += 1
        mp = [me, options.destination,msg,tx1.SerializeToString(),tx2.SerializeToString()]
        if options.verbose:
            print "---%s msg %s" % (me,mp)
        cmd.send_multipart(mp)

    for n in range(options.batch):
        msg = response.recv_multipart()
        if options.verbose:
            print "---%s receive response: %s" %(me, msg)
    if not options.fast:
        time.sleep(1)

time.sleep(1)
context.destroy(linger=0)
