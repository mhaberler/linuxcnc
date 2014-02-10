# proxy for simple RT responder


import os
import time
import zmq
import halext
from optparse import OptionParser

parser = OptionParser()

parser.add_option("-C", "--cmdout", dest="cmdoutput", default="tcp://127.0.0.1:5570",
                  help="URI to submit commands to")

parser.add_option("-c", "--cmdin", dest="cmdinput", default="tcp://127.0.0.1:5571",
                  help="URI to fetch commands from")

parser.add_option("-r", "--responsein", dest="responseinput",
                  default="tcp://127.0.0.1:5573",
                  help="URI to fetch responses from")

parser.add_option("-R", "--responseout", dest="responseoutput",
                  default="tcp://127.0.0.1:5572",
                  help="URI to submit responses to")

parser.add_option("-n", "--name", dest="actor", default="actor",
                  help="use this as actor name")

parser.add_option("-s", "--subactor", dest="subactors", default=[],
                  action="append", help="invoke subactor(s) before completion")

parser.add_option("-v", "--verbose", action="store_true", dest="verbose",
                  help="print actions as they happen")

(options, args) = parser.parse_args()


me = options.actor

context = zmq.Context()

cmdin = context.socket(zmq.SUB)
cmdin.connect(options.cmdinput)
cmdin.setsockopt(zmq.SUBSCRIBE, me)

cmdout = context.socket(zmq.DEALER)
cmdout.setsockopt(zmq.IDENTITY, me)
cmdout.connect(options.cmdoutput)

responsein = context.socket(zmq.SUB)
responsein.connect(options.responseinput)
responsein.setsockopt(zmq.SUBSCRIBE, me)

responseout = context.socket(zmq.DEALER)
responseout.setsockopt(zmq.IDENTITY, me)
responseout.connect(options.responseoutput)

comp = halext.HalComponent(me + ".proxy")
comp.ready()

# attach $me.in, $me.out ringbuffers
# assumes rtcomp.py already running
try:
    # attach to existing ring
    to_rt = comp.attach(me + ".in")
    from_rt = comp.attach(me + ".out")
except NameError,e:
    print e

# TODO: check if reader/writer exists, issue warning message if not

i = 0
while True:

   msg = cmdin.recv_multipart()
   # asser(msg[0] == me)
   sender = msg[1]
   payload = str(msg[2:])
   if options.verbose:
      print "--- %s fetched: sender=%s payload=%s " % (me, sender, payload)

   # push payload down tx ringbuffer
   to_rt.write(payload, len(payload))

   # wait for tx ringbuffer non-empty and fetch result
   try:
      while True:
         response = from_rt.next_buffer()
         if response is None:
            time.sleep(0.01)
            continue
         from_rt.shift()
         i += 1
         print "--RT recv on %s.in: " % me, response
         response += " processed by %s.proxy count=%d" % (me, i)
         responseout.send_multipart([sender, response])
         break

   except KeyboardInterrupt:
      comp.exit()
