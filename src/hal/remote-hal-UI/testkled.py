#!/usr/bin/env python

import sys

from PyQt4.QtGui import QVBoxLayout, QWidget
from PyKDE4.kdecore import ki18n, KAboutData, KCmdLineArgs
from PyKDE4.kdeui import KApplication, KLed

if __name__ == "__main__":

    about = KAboutData("LED", "", ki18n("LED"), "1.0", ki18n("LED example"))
    args = KCmdLineArgs.init(sys.argv, about)
    app = KApplication()

    led = KLed()
    w = QWidget()
    l = QVBoxLayout(w)
    l.addWidget(led)
    w.show()

    sys.exit(app.exec_())
