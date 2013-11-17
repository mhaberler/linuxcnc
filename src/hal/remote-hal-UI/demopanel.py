# -*- coding: utf-8 -*-

# Form implementation generated from reading ui file 'demopanel.ui'
#
# Created: Wed Nov 13 18:12:43 2013
#      by: PyQt4 UI code generator 4.10.3
#
# WARNING! All changes made in this file will be lost!

from PyQt4 import QtCore, QtGui

try:
    _fromUtf8 = QtCore.QString.fromUtf8
except AttributeError:
    def _fromUtf8(s):
        return s

try:
    _encoding = QtGui.QApplication.UnicodeUTF8
    def _translate(context, text, disambig):
        return QtGui.QApplication.translate(context, text, disambig, _encoding)
except AttributeError:
    def _translate(context, text, disambig):
        return QtGui.QApplication.translate(context, text, disambig)

class Ui_MainWindow(object):
    def setupUi(self, MainWindow):
        MainWindow.setObjectName(_fromUtf8("MainWindow"))
        MainWindow.resize(800, 600)
        sizePolicy = QtGui.QSizePolicy(QtGui.QSizePolicy.Preferred, QtGui.QSizePolicy.Preferred)
        sizePolicy.setHorizontalStretch(0)
        sizePolicy.setVerticalStretch(0)
        sizePolicy.setHeightForWidth(MainWindow.sizePolicy().hasHeightForWidth())
        MainWindow.setSizePolicy(sizePolicy)
        self.centralwidget = QtGui.QWidget(MainWindow)
        self.centralwidget.setObjectName(_fromUtf8("centralwidget"))
        self.layoutWidget = QtGui.QWidget(self.centralwidget)
        self.layoutWidget.setGeometry(QtCore.QRect(100, 50, 521, 451))
        self.layoutWidget.setObjectName(_fromUtf8("layoutWidget"))
        self.gridLayout = QtGui.QGridLayout(self.layoutWidget)
        self.gridLayout.setMargin(10)
        self.gridLayout.setSpacing(10)
        self.gridLayout.setObjectName(_fromUtf8("gridLayout"))
        self.dial = QtGui.QDial(self.layoutWidget)
        self.dial.setObjectName(_fromUtf8("dial"))
        self.gridLayout.addWidget(self.dial, 0, 0, 1, 1)
        self.enable = QtGui.QPushButton(self.layoutWidget)
        self.enable.setObjectName(_fromUtf8("enable"))
        self.gridLayout.addWidget(self.enable, 1, 0, 1, 1)
        self.speed = QtGui.QProgressBar(self.layoutWidget)
        self.speed.setProperty("value", 24)
        self.speed.setObjectName(_fromUtf8("speed"))
        self.gridLayout.addWidget(self.speed, 0, 1, 1, 1)
        self.status = QtGui.QLabel(self.layoutWidget)
        sizePolicy = QtGui.QSizePolicy(QtGui.QSizePolicy.Preferred, QtGui.QSizePolicy.Fixed)
        sizePolicy.setHorizontalStretch(0)
        sizePolicy.setVerticalStretch(0)
        sizePolicy.setHeightForWidth(self.status.sizePolicy().hasHeightForWidth())
        self.status.setSizePolicy(sizePolicy)
        self.status.setObjectName(_fromUtf8("status"))
        self.gridLayout.addWidget(self.status, 2, 0, 1, 1)
        self.led1 = QLed(self.layoutWidget)
        sizePolicy = QtGui.QSizePolicy(QtGui.QSizePolicy.Maximum, QtGui.QSizePolicy.Ignored)
        sizePolicy.setHorizontalStretch(0)
        sizePolicy.setVerticalStretch(0)
        sizePolicy.setHeightForWidth(self.led1.sizePolicy().hasHeightForWidth())
        self.led1.setSizePolicy(sizePolicy)
        self.led1.setMinimumSize(QtCore.QSize(64, 64))
        self.led1.setProperty("value", False)
        self.led1.setObjectName(_fromUtf8("led1"))
        self.gridLayout.addWidget(self.led1, 2, 1, 1, 1)
        MainWindow.setCentralWidget(self.centralwidget)
        self.menubar = QtGui.QMenuBar(MainWindow)
        self.menubar.setGeometry(QtCore.QRect(0, 0, 800, 24))
        self.menubar.setObjectName(_fromUtf8("menubar"))
        MainWindow.setMenuBar(self.menubar)
        self.statusbar = QtGui.QStatusBar(MainWindow)
        self.statusbar.setObjectName(_fromUtf8("statusbar"))
        MainWindow.setStatusBar(self.statusbar)

        self.retranslateUi(MainWindow)
        QtCore.QMetaObject.connectSlotsByName(MainWindow)

    def retranslateUi(self, MainWindow):
        MainWindow.setWindowTitle(_translate("MainWindow", "MainWindow", None))
        self.enable.setText(_translate("MainWindow", "PushButton", None))
        self.status.setText(_translate("MainWindow", "TextLabel", None))
        self.led1.setToolTip(_translate("MainWindow", "Binary Led", None))
        self.led1.setWhatsThis(_translate("MainWindow", "Led widget", None))

from qled import QLed
