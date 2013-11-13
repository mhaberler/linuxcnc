

import sys, zmq,os

from message_pb2 import Container
from types_pb2 import *

from PyQt4 import QtGui, QtCore
from PyQt4.QtCore import QSocketNotifier, SIGNAL

class Example(QtGui.QWidget):

    def __init__(self):
        super(Example, self).__init__()
        self.initUI()

        cmd_uri="tcp://127.0.0.1:4711"
        update_uri="tcp://127.0.0.1:4712"

        context = zmq.Context()
        self.update = context.socket(zmq.SUB)
        self.update.connect(update_uri)
        self.update.setsockopt(zmq.SUBSCRIBE, 'demo')
        self.update_notifier = QSocketNotifier(self.update.getsockopt(zmq.FD),
                                               QSocketNotifier.Read)
        self.connect(self.update_notifier, SIGNAL('activated(int)'), self.update_readable)

        self.cmd = context.socket(zmq.DEALER)
        self.cmd.set(zmq.IDENTITY,"qtzmq-%d" % os.getpid())
        self.cmd.connect(cmd_uri)
        self.cmd_notifier = QSocketNotifier(self.cmd.getsockopt(zmq.FD),
                                       QSocketNotifier.Read)
        self.connect(self.cmd_notifier, SIGNAL('activated(int)'), self.cmd_readable)

        # halserver ping timer
        self.timer = QtCore.QBasicTimer()
        self.timer.start(2000, self)

    def update_readable(self,fd):
        while self.update.getsockopt(zmq.EVENTS) & zmq.POLLIN:
            try:
                topic, data = self.update.recv_multipart(flags=zmq.NOBLOCK)
                u = Container()
                u.ParseFromString(data)

                print "publisher: ",topic,str(u)
            except zmq.ZMQError as e:
                if e.errno != zmq.EAGAIN:
                    raise

    def cmd_readable(self,fd):
        while self.cmd.getsockopt(zmq.EVENTS) & zmq.POLLIN:
            try:
                data = self.cmd.recv(flags=zmq.NOBLOCK)
                print "cmd: ",data
            except zmq.ZMQError as e:
                if e.errno != zmq.EAGAIN:
                    raise

    def timerEvent(self,e):
        print "timer tick"

    def initUI(self):

        steeringInstruction = QtGui.QSlider(QtCore.Qt.Horizontal, self)
        steeringInstruction.setTickPosition(QtGui.QSlider.TicksBothSides)
        steeringInstruction.setTickInterval(10)
        steeringInstruction.setFocusPolicy(QtCore.Qt.NoFocus)
        steeringInstruction.setRange(-100, 100)
        steeringInstruction.setGeometry(10, 40, 480, 30)
        steeringInstruction.valueChanged[int].connect(self.sendSteerInstruction)

        self.currentSteer = QtGui.QSlider(QtCore.Qt.Horizontal, self)
        self.currentSteer.setFocusPolicy(QtCore.Qt.NoFocus)
        self.currentSteer.setRange(-100, 100)
        self.currentSteer.setGeometry(10, 90, 480, 30)

        self.label = QtGui.QLabel('0', self)
        self.label.move(245, 5)
        self.label.setGeometry(245, 5, 30, 30)

        self.setGeometry(300, 300, 500, 150)
        self.setWindowTitle('Steering')
        self.show()

    def sendSteerInstruction(self, value):
        instructionObj = infoPack_pb2.steeringInstruction()
        instructionObj.steeringInstruction = value
        steeringPb = instructionObj.SerializeToString()
        self.publisher.send(steeringPb)
        self.label.setText(str(value))
        print(value)

    def currentSteerUpdate(self, message):
        currentSteerObj = infoPack_pb2.currentSteer()
        currentSteerObj.ParseFromString(message)
        self.currentSteer.setValue(currentSteerObj.currentSteer)


def main():
    app = QtGui.QApplication(sys.argv)
    ex = Example()
    sys.exit(app.exec_())

if __name__ == '__main__':
    main()
