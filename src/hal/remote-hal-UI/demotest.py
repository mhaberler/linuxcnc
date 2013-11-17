# This is the contents of the MyGui.py file
# It assumes that you have created ui_MyGui.ui with QtDesigner
# You have to execute >/pyuic4 ui_MyGui.ui > ui_MyGui.py to generate the file to import

from PyQt4 import QtGui,QtCore
from demopanel import *
import sys

class MyGUI(QtGui.QMainWindow):
    def __init__(self):
        QtGui.QMainWindow.__init__(self)
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)
        #self.connect(self.ui.push_button,QtCore.SIGNAL('clicked(bool)'),self.DoAction)

    def DoAction(self,arg = None):
        print 'What a %s action.' % ('wonderful' if arg else 'sad')

app = QtGui.QApplication(sys.argv[0:1])
gui = MyGUI()
gui.show()
sys.exit(app.exec_())
