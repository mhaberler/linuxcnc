import time
import zmq
import os

def main():
    url_server = "tcp://127.0.0.1:5555"

    # Prepare our context and sockets
    context = zmq.Context(1)
    server = context.socket(zmq.DEALER)
    server.setsockopt(zmq.IDENTITY, "client%d" % (os.getpid()) )
    server.connect(url_server)
    for i in range(5):
        server.send("Hello %d" % (os.getpid()))
        print str(server.recv_multipart())
        time.sleep(1)
    server.send("SHUTDOWN")
    # receive confirmation
    print str(server.recv_multipart())

    server.close()
    context.term()

if __name__ == "__main__":
    main()
