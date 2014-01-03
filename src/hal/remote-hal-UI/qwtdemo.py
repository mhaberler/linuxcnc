# -*- coding: utf-8 -*-

# Form implementation generated from reading ui file 'qwtdemo.ui'
#
# Created: Wed Nov 13 17:51:48 2013
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
        self.centralwidget = QtGui.QWidget(MainWindow)
        self.centralwidget.setObjectName(_fromUtf8("centralwidget"))
        self.Slider = QwtSlider(self.centralwidget)
        self.Slider.setGeometry(QtCore.QRect(220, 210, 200, 60))
        self.Slider.setObjectName(_fromUtf8("Slider"))
        self.Thermo = QwtThermo(self.centralwidget)
        self.Thermo.setGeometry(QtCore.QRect(596, 55, 111, 321))
        self.Thermo.setFillColor(QtGui.QColor(170, 85, 255))
        self.Thermo.setMaxValue(10.0)
        self.Thermo.setObjectName(_fromUtf8("Thermo"))
        self.Wheel = QwtWheel(self.centralwidget)
        self.Wheel.setGeometry(QtCore.QRect(150, 430, 64, 24))
        self.Wheel.setObjectName(_fromUtf8("Wheel"))
        self.TextLabel = QwtTextLabel(self.centralwidget)
        self.TextLabel.setGeometry(QtCore.QRect(290, 330, 100, 20))
        self.TextLabel.setObjectName(_fromUtf8("TextLabel"))
        self.lcdNumber = QtGui.QLCDNumber(self.centralwidget)
        self.lcdNumber.setGeometry(QtCore.QRect(270, 70, 141, 71))
        self.lcdNumber.setProperty("intValue", 42)
        self.lcdNumber.setObjectName(_fromUtf8("lcdNumber"))
        self.label = QtGui.QLabel(self.centralwidget)
        self.label.setGeometry(QtCore.QRect(90, 90, 69, 51))
        self.label.setText(_fromUtf8(""))
        self.label.setPixmap(QtGui.QPixmap(_fromUtf8("../../../../src/qt4/led/resources/circle_green.svg")))
        self.label.setObjectName(_fromUtf8("label"))
        self.qwtPlot = QwtPlot(self.centralwidget)
        self.qwtPlot.setGeometry(QtCore.QRect(250, 350, 400, 200))
        self.qwtPlot.setObjectName(_fromUtf8("qwtPlot"))
        self.Counter = QwtCounter(self.centralwidget)
        self.Counter.setGeometry(QtCore.QRect(500, 60, 84, 30))
        self.Counter.setObjectName(_fromUtf8("Counter"))
        self.Knob = QwtKnob(self.centralwidget)
        self.Knob.setGeometry(QtCore.QRect(50, 180, 126, 126))
        self.Knob.setObjectName(_fromUtf8("Knob"))
        self.TextLabel_2 = QwtTextLabel(self.centralwidget)
        self.TextLabel_2.setGeometry(QtCore.QRect(80, 350, 100, 20))
        self.TextLabel_2.setObjectName(_fromUtf8("TextLabel_2"))
        self.kcolorbutton = KColorButton(self.centralwidget)
        self.kcolorbutton.setGeometry(QtCore.QRect(70, 480, 60, 27))
        self.kcolorbutton.setDefaultColor(QtGui.QColor(85, 170, 0))
        self.kcolorbutton.setObjectName(_fromUtf8("kcolorbutton"))
        MainWindow.setCentralWidget(self.centralwidget)
        self.menubar = QtGui.QMenuBar(MainWindow)
        self.menubar.setGeometry(QtCore.QRect(0, 0, 800, 24))
        self.menubar.setObjectName(_fromUtf8("menubar"))
        MainWindow.setMenuBar(self.menubar)
        self.statusbar = QtGui.QStatusBar(MainWindow)
        self.statusbar.setObjectName(_fromUtf8("statusbar"))
        MainWindow.setStatusBar(self.statusbar)

        self.retranslateUi(MainWindow)
        QtCore.QObject.connect(self.Slider, QtCore.SIGNAL(_fromUtf8("sliderMoved(double)")), self.Thermo.setValue)
        QtCore.QObject.connect(self.Knob, QtCore.SIGNAL(_fromUtf8("sliderMoved(double)")), self.Thermo.setValue)
        QtCore.QObject.connect(self.Slider, QtCore.SIGNAL(_fromUtf8("sliderMoved(double)")), self.lcdNumber.display)
        QtCore.QMetaObject.connectSlotsByName(MainWindow)

    def retranslateUi(self, MainWindow):
        MainWindow.setWindowTitle(_translate("MainWindow", "MainWindow", None))

from qwt_knob import QwtKnob
from qwt_wheel import QwtWheel
from qwt_thermo import QwtThermo
from qwt_slider import QwtSlider
from qwt_text_label import QwtTextLabel
from qwt_counter import QwtCounter
from kcolorbutton import KColorButton
from qwt_plot import QwtPlot
