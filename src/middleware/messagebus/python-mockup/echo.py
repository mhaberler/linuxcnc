import os
import zmq
from optparse import OptionParser

parser = OptionParser()


parser.add_option("-c", "--cmdin", dest="cmdinput", default="tcp://127.0.0.1:5571",
                  help="URI to fetch commands from")

parser.add_option("-R", "--responseout", dest="responseoutput",
                  default="tcp://127.0.0.1:5572",
                  help="URI to submit responses to")

parser.add_option("-n", "--name", dest="actor", default="pyecho",
                  help="use this as actor name")

parser.add_option("-v", "--verbose", action="store_true", dest="verbose",
                  help="print actions as they happen")

(options, args) = parser.parse_args()


me = options.actor

context = zmq.Context()

cmdin = context.socket(zmq.SUB)
cmdin.connect(options.cmdinput)
cmdin.setsockopt(zmq.SUBSCRIBE, me)

responseout = context.socket(zmq.DEALER)
responseout.setsockopt(zmq.IDENTITY, me)
responseout.connect(options.responseoutput)

i = 0
while True:
   i += 1

   msg = cmdin.recv_multipart()
   # asser(msg[0] == me)
   sender = msg[1]
   payload = str(msg[2:])
   if options.verbose:
      print "--- %s fetched: sender=%s payload=%s " % (me, sender, payload)

   response = [sender]
   response.append(payload)
   responseout.send_multipart(response)
