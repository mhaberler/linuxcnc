#!/usr/bin/env python

import sys
try:
    import pygtk
    pygtk.require("2.0")
except:
    pass
try:
    import gtk
    import gtk.glade
except:
    sys.exit(1)

class HellowWorldGTK:
    """This is an Hello World GTK application"""

    def __init__(self):
        self.gladefile = "pygktexample.ui"
        try:
            self.wTree = gtk.Builder()
            self.wTree.add_from_file(self.gladefile)
        except:
            try:
                # try loading as a gtk.builder project
                self.wTree = gtk.glade.XML(self.gladefile)
                self.wTree = GladeBuilder(builder)

            except Exception,e:
                print >> sys.stderr, "cant load file: %s : %s" % (self.gladefile,e)
                sys.exit(0)

		#Create our dictionay and connect it
        dic = { "on_btnHelloWorld_clicked" : self.btnHelloWorld_clicked,
                "on_MainWindow_destroy" : gtk.main_quit }
        #self.wTree.signal_autoconnect(dic)
        self.window = self.wTree.get_object("window1")

    def btnHelloWorld_clicked(self, widget):
        print "Hello World!"


if __name__ == "__main__":
    hwg = HellowWorldGTK()
    hwg.window.show()
    gtk.main()
