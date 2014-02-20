import zmq
import sys
import threading
import time

from optparse import OptionParser

parser = OptionParser()
parser.add_option("-v","--verbose", action="store_true", dest="verbose",
                  help="print actions as they happen")

(options, args) = parser.parse_args()

me = "msgbus"

# XPUB only

# the command bus:
cmduri   = 'tcp://127.0.0.1:5571'

# the response bus:
responseuri = 'tcp://127.0.0.1:5573'


class MsgbusTask(threading.Thread):
    def __init__(self):
        threading.Thread.__init__ (self)
        self.kill_received = False

    def run(self):
        print('Msbus started')

        cmdsubs = dict()
        responsesubs = dict()
        context = zmq.Context()

        cmd = context.socket(zmq.XPUB)
        cmd.set(zmq.XPUB_VERBOSE,1)
        cmd.bind(cmduri)

        response = context.socket(zmq.XPUB)
        response.set(zmq.XPUB_VERBOSE,1)
        response.bind(responseuri)

        poll = zmq.Poller()
        poll.register(cmd,     zmq.POLLIN)
        poll.register(response,zmq.POLLIN)


        while not self.kill_received:
            s = dict(poll.poll(1000))
            if cmd in s:
                msg = cmd.recv_multipart()
                if options.verbose: print "---%s cmd recv: %s" % (me,msg)
                if len(msg) == 1:
                    frame = msg[0]
                    sub = ord(frame[0])
                    topic = frame[1:]

                    if sub:
                        cmdsubs[topic] = True
                        if options.verbose: print "--- %s command subscribe: %s" % (me,topic)
                    else:
                        if options.verbose: print "--- %s command unsubscribe: %s" % (me,topic)
                        del cmdsubs[topic]
                else:
                    # assert(len(msg) == 3)
                    dest = msg[1]
                    if dest in cmdsubs:
                        msg[0], msg[1] = msg[1], msg[0]
                        cmd.send_multipart(msg)
                    else:
                        response.send_multipart([msg[0], "--- no destination: " + dest])
                        if options.verbose: print "no command destination:", dest

            if response in s:
                msg = response.recv_multipart()
                if options.verbose: print "--- %s response recv: %s" % (me, msg)
                if len(msg) == 1:
                    frame = msg[0]
                    sub = ord(frame[0])
                    topic = frame[1:]

                    if sub:
                        responsesubs[topic] = True
                        if options.verbose: print "--- %s response subscribe: %s" % (me,topic)
                    else:
                        if options.verbose: print "--- %s response unsubscribe: %s" % (me, topic)
                        del responsesubs[topic]
                else:
                    dest = msg[1]
                    if dest in responsesubs:
                        msg[0], msg[1] = msg[1], msg[0]
                        response.send_multipart(msg)
                    else:
                        response.send_multipart([msg[0], "no destination: " + dest])
                        if options.verbose: print "no destination:", dest

        context.destroy(linger=0)
        print('Msbus exited')

def main():
    try:
        bus = MsgbusTask()
        bus.start()
        while True: time.sleep(100)
    except (KeyboardInterrupt, SystemExit):
        bus.kill_received = True
        bus.join()

if __name__ == "__main__":
    main()
