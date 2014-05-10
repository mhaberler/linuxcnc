#!/usr/bin/python

# examaple websockets command line client
# reads JSON-encoded log messages off the msgd Websocket server
#
# requires: https://pypi.python.org/pypi/websocket-client/

import websocket
import thread
import time

def on_message(ws, message):
    print message
    ws.send("{\"type\": 10, \"note\" : \"from client\"}")

def on_error(ws, error):
    print error

def on_close(ws):
    print "### closed ###"



if __name__ == "__main__":
    websocket.enableTrace(True)
    ws = websocket.WebSocketApp("ws://127.0.0.1:7681/?dest=tcp://127.0.0.1:5550&type=sub&topic=json",
    # ws = websocket.WebSocketApp("ws://127.0.0.1:7681/?dest=inproc://logsub&type=xsub&topic=json",
                                #ws = websocket.WebSocketApp("ws://127.0.0.1:7681/?dest=ipc://tmp/logsub&type=sub&topic=json",

    # ws = websocket.WebSocketApp("ws://127.0.0.1:7681/?dest=inproc://logsub&type=sub&topic=json",
                                header=["Sec-WebSocket-Protocol: zmqproxy"],
                                on_message = on_message,
                                on_error = on_error,
                                on_close = on_close)
    ws.run_forever()
