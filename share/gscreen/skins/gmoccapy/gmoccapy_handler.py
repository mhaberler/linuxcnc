#!/usr/bin/env python
# -*- coding:UTF-8 -*-
"""
    A try of a new GUI for LinuxCNC based on gladevcp and Python
    Based on the design of moccagui from Tom
    and based on gscreen from Chris Morley
    and with the help from Michael Haberler
    and Chris Morley

    Copyright 2012 Norbert Schechner
    nieson@web.de

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

"""
import hal               # base hal class to react to hal signals
import hal_glib          # needed to make our own hal pins
import gtk               # base for pygtk widgets and constants
import os                # needed to get the paths and directorys
import pango             # needed for font settings and changing
import gladevcp.makepins # needed for the dialog"s calulator widget
import locale            # for translations
import subprocess        # to launch onboard and other proceses
import tempfile          # needed only if the user click new in edit mode to open a new empty file
import linuxcnc          # to get our own error sytsem

# standard handler call
def get_handlers(halcomp,builder,useropts,gscreen):
     return [HandlerClass(halcomp,builder,useropts,gscreen)]

# this is for hiding the pointer when using a touch screen
pixmap = gtk.gdk.Pixmap(None, 1, 1, 1)
color = gtk.gdk.Color()
INVISABLE = gtk.gdk.Cursor(pixmap, pixmap, color, color, 0, 0)

# constants
_RELEASE = "0.9.9"
_IMPERIAL = 0           # Imperial Units are active
_MM = 1                 # metric units are active
_MANUAL = 1             # Check for the mode Manual
_AUTO = 2               # Check for the mode Auto
_MDI = 3                # Check for the mode MDI
_RUN = 1                # needed to check if the interpreter is running
_IDLE = 0               # needed to check if the interpreter is idle
_TEMPDIR = tempfile.gettempdir() # Now we know where the tempdir is, usualy /tmp

# all this stuff is only needed, because we want to show icon in Notification 
import sys
BASE = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), ".."))
SKINPATH = os.path.join(BASE, "share","gscreen","skins","gmoccapy")
IMAGEDIR = os.path.join(SKINPATH,"icon")
CONFIGPATH = os.environ['CONFIG_DIR']

# the ICONS should be in gmoccapy / icon, or we should rename the folder to images
ALERT_ICON = os.path.join(IMAGEDIR,"applet-critical.png")
INFO_ICON = os.path.join(IMAGEDIR,"std_info.gif")

if os.path.exists(os.path.join(SKINPATH,"python")):
    print ("\n\n*** GMOCCAPY INFO: ***\n found python subfolder in \n%s"%SKINPATH)
    print ("added to python path\n*** GMOCCAPY INFO: *** \n\n")
    sys.path.append(os.path.join(SKINPATH,"python"))
    print sys.path
else:
    print ("\n*** GMOCCAPY INFO: *** no additional python path found")

try:
    import notification      # this is the module we use for our error handling
except:
    print("Error trying to import notification")
    sys.exit()

# This is a handler file for using Gscreen"s infrastructure
# to load a completely custom glade screen
# The only things that really matters is that it's saved as a GTK builder project,
# the toplevel window is called window1 (The default name) and you connect a destroy
# window signal else you can"t close down linuxcnc 
class HandlerClass:

    # This will be pretty standard to gain access to everything
    # emc is for control and status of linuxcnc
    # data is important data from gscreen and linuxcnc
    # widgets is all the widgets from the glade files
    # gscreen is for access to gscreens methods
    def __init__(self, halcomp,builder,useropts,gscreen):
        self.emc = gscreen.emc
        self.data = gscreen.data
        self.widgets = gscreen.widgets
        self.gscreen = gscreen
        self.emcstat = linuxcnc.stat()
        self.emcerror = linuxcnc.error_channel()

        self.distance = 0         # This global will hold the jog distance
        self.interpreter = _IDLE  # This hold the interpreter state, so we could check if actions are allowed
        self.wait_tool_change = False # this is needed to get back to manual mode after a tool change
        self.macrobuttons =[]     # The list of all macrios defined in the INI file
        self.log = False          # decide if the actions should be loged
        self.fo_counts = 0        # need to calculate diference in counts to change the feed override slider
        self.so_counts = 0        # need to calculate diference in counts to change the spindle override slider
        self.jv_counts = 0        # need to calculate diference in counts to change the jog_vel slider
        self.mv_counts = 0        # need to calculate diference in counts to change the max_speed slider
        self.incr_rbt_list= []    # we use this list to add hal pin to the button later
        self.no_increments = 0    # number of increments from INI File, because of space we allow a max of 10
        self.unlock = False       # this value will be set using the hal pin unlock settings
        self.system_list = (0,"G54","G55","G56","G57","G58","G59","G59.1","G59.2","G59.3") # needed to display the labels
        self.axisnumber_four = ""   # we use this to get the number of the 4-th axis
        self.axisletter_four = None # we use this to get the letter of the 4-th axis
        self.notification = notification.Notification() # Our own message system

    def initialize_preferences(self):
        self.data.theme_name = self.gscreen.prefs.getpref("gtk_theme", "Follow System Theme", str)
        self.gscreen.init_general_pref()
        self._init_axis_four()

    def _init_axis_four(self):
        if len(self.data.axis_list) < 4:
            self.widgets.Combi_DRO_4.hide()
            return
        axis_four = list(set(self.data.axis_list)-set(("x","y","z")))
        if len(axis_four) > 1:
            message = _("gmoccapy can only handle 4 axis,\nbut you have given %d through your INI file\n"%len(self.data.axis_list))
            message += _("gmoccapy will not start\n\n")
            print(message)
            self.widgets.window1.destroy()
        self.axisletter_four = axis_four[0]
        self.axisnumber_four = "xyzabcuvw".index(self.axisletter_four)
        self.widgets.Combi_DRO_4.set_property("joint_number", self.axisnumber_four)
        self.widgets.Combi_DRO_4.change_axisletter(self.axisletter_four.upper())
        if self.axisletter_four in "abc":
            self.widgets.Combi_DRO_4.set_property("mm_text_template","%11.2f")
            self.widgets.Combi_DRO_4.set_property("imperial_text_template","%11.2f")
        image = self.widgets["img_home_%s"%self.axisletter_four]
        self.widgets.btn_home_4.set_image(image)
        # We have to change the size of the DRO, to make 4 DRO fit the space we got
        for axis in self.data.axis_list:
            if axis == self.axisletter_four:
                axis = 4
            self.widgets["Combi_DRO_%s"%axis].set_property("font_size", 20)

    # We don't want Gscreen to initialize it's regular widgets because this custom
    # screen doesn"t have most of them. So we add this function call.
    # Since this custom screen uses gladeVCP magic for its interaction with linuxcnc
    # We don"t add much to this function, but we do want to be able to change the theme so:
    # We change the GTK theme to what"s in gscreen"s preference file.
    # gscreen.change_theme() is a method in gscreen that changes the GTK theme of window1
    # gscreen.data.theme_name is the name of the theme from the preference file 
    # To truely be friendly, we should add a way to change the theme directly in the custom screen.
    # we also set up the statusbar and add a ready-to-home message
    def initialize_widgets(self):
        # These are gscreen widgets included in this screen
        self.gscreen.init_statusbar()
        self.gscreen.init_tooleditor()
        self.gscreen.init_embeded_terminal()
        self.gscreen.init_themes() # load all avaiable themes in the combo box
        self.gscreen.init_audio()
        
        # and now our own ones
        self.init_gremlin()
        self.init_hide_cursor()
        self.init_keyboard()
        self.init_offsetpage()
        self.init_IconFileSelection()

        # now we initialize the file to load widget
        self.init_file_to_load()

        self._show_offset_tab(False)
        self._show_tooledit_tab(False)
        self._show_iconview_tab(False)

        # check if NO_FORCE_HOMING is used in ini
        self.no_force_homing = self.gscreen.inifile.find("TRAJ", "NO_FORCE_HOMING")

        # and we want to set the default path
        default_path = self.gscreen.inifile.find("DISPLAY", "PROGRAM_PREFIX")
        if not os.path.exists(os.path.expanduser(default_path)):
            print(_("Path %s from DISPLAY , PROGRAM_PREFIX does not exist")%default_path)
            print(_("Trying default path..."))
            default_path = "~/linuxcnc/nc_files/"
            if not os.path.exists(os.path.expanduser(default_path)):
                print(_("Default path to ~/linuxcnc/nc_files does not exist"))
                print(_("setting now home as path"))
                default_path = os.path.expanduser("~/")
        self.widgets.hal_action_open.currentfolder = os.path.expanduser(default_path)
        self.widgets.IconFileSelection1.set_property("start_dir",default_path)

        # set the slider limmits
        self.widgets.adj_max_vel.configure(self.data._maxvelocity * 60, self.data._maxvelocity * 0.1,
                                           self.data._maxvelocity * 60 + 1, 1, 1, 1)
        self.widgets.adj_jog_vel.configure(self.data.jog_rate, 0, 
                                           self.gscreen.data.jog_rate_max + 1, 1, 1, 1)
        self.widgets.adj_spindle.configure(100, self.data.spindle_override_min * 100, 
                                           self.data.spindle_override_max * 100 + 1, 1, 1, 1)
        self.widgets.adj_feed.configure(100, 0, self.data.feed_override_max * 100 + 1, 1, 1, 1)

        # the scale to apply to the count of the hardware mpg wheel, to avoid to much turning
        default = (self.data._maxvelocity * 60 - self.data._maxvelocity * 0.1)/100
        self.scale_max_vel = self.gscreen.prefs.getpref("scale_max_vel", default, float)
        self.widgets.adj_scale_max_vel.set_value(self.scale_max_vel)
        default = (self.data.jog_rate_max / 100)
        self.scale_jog_vel = self.gscreen.prefs.getpref("scale_jog_vel", default, float)
        self.widgets.adj_scale_jog_vel.set_value(self.scale_jog_vel)
        self.scale_spindle_override = self.gscreen.prefs.getpref("scale_spindle_override", 1, float)
        self.widgets.adj_scale_spindle_override.set_value(self.scale_spindle_override)
        self.scale_feed_override = self.gscreen.prefs.getpref("scale_feed_override", 1, float)
        self.widgets.adj_scale_feed_override.set_value(self.scale_feed_override)

        # set the title of the window, to show the release
        self.widgets.window1.set_title("gmoccapy for linuxcnc %s"%_RELEASE)

        # the velocity settings
        self.min_spindle_rev = self.gscreen.prefs.getpref("spindle_bar_min", 0.0, float)
        self.max_spindle_rev = self.gscreen.prefs.getpref("spindle_bar_max", 6000.0, float)
        self.widgets.adj_spindle_bar_min.set_value((self.min_spindle_rev))
        self.widgets.adj_spindle_bar_max.set_value((self.max_spindle_rev))
        self.widgets.hal_hbar_spindle_feedback.set_property("min",float(self.min_spindle_rev))
        self.widgets.hal_hbar_spindle_feedback.set_property("max",float(self.max_spindle_rev))

        # Window position and size
        self.widgets.adj_x_pos.set_value(self.gscreen.prefs.getpref("x_pos", 10, float))
        self.widgets.adj_y_pos.set_value(self.gscreen.prefs.getpref("y_pos", 25, float))
        self.widgets.adj_width.set_value(self.gscreen.prefs.getpref("width", 979, float))
        self.widgets.adj_height.set_value(self.gscreen.prefs.getpref("height", 750, float))

        # Get the type of error handling
        self.data.error_style = self.gscreen.prefs.getpref("error_style", "gmoccapy", str)
        # and set the corresponding button active
        self.widgets["rbt_use_%s"%self.data.error_style].set_active(True)
        # unsensitize the gmoccastyle adjustments if gscreen shall be usede
        if self.data.error_style == "gscreen":
            self.widgets.frm_message_position.set_sensitive(False)
        
        # Popup Messages position and size
        self.widgets.adj_x_pos_popup.set_value(self.gscreen.prefs.getpref("x_pos_popup", 15, float))
        self.widgets.adj_y_pos_popup.set_value(self.gscreen.prefs.getpref("y_pos_popup", 55, float))
        self.widgets.adj_width_popup.set_value(self.gscreen.prefs.getpref("width_popup", 250, float))
        self.widgets.adj_max_messages.set_value(self.gscreen.prefs.getpref("max_messages", 10, float))
        self.widgets.fontbutton_popup.set_font_name(self.gscreen.prefs.getpref("message_font", "sans 10", str))
        self.widgets.chk_use_frames.set_active(self.gscreen.prefs.getpref("use_frames", True, bool))

# Only used if the DRO buttons will remain in gmoccapy
        self.widgets.chk_show_dro_btn.set_active(self.gscreen.prefs.getpref("show_dro_btn", False, bool))
        self.widgets.chk_auto_units.set_active(self.gscreen.prefs.getpref("use_auto_units", True, bool))
        self.on_chk_show_dro_btn_toggled(None)
        self.on_chk_auto_units_toggled(None)
        print self.data.dro_units
        if self.widgets.Combi_DRO_x.machine_units == 0:
            self.widgets.tbtn_units.set_active(True)

        self.widgets.tbtn_rel.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_dtg.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_units.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
# end of the button usage

        # Use desktop notify ?
        self.data.desktop_notify = self.gscreen.prefs.getpref("desktop_notify", False, bool)
        self.widgets.chk_use_desktop_notify.set_active(self.data.desktop_notify)

        # this sets the background colors of several buttons
        # the colors are different for the states of the button
        self.widgets.tbtn_on.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_estop.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FF0000"))
        self.widgets.tbtn_estop.modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("#00FF00"))
        self.widgets.rbt_manual.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.rbt_mdi.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.rbt_auto.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_setup.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.rbt_forward.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#00FF00"))
        self.widgets.rbt_reverse.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#00FF00"))
        self.widgets.rbt_stop.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.rbt_view_p.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.rbt_view_x.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.rbt_view_y.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.rbt_view_y2.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.rbt_view_z.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_flood.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#00FF00"))
        self.widgets.tbtn_fullsize_preview.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_fullsize_preview1.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_log_actions.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_mist.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#00FF00"))
        self.widgets.tbtn_optional_blocks.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_optional_stops.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_user_tabs.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_view_dimension.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_view_tool_path.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))
        self.widgets.tbtn_edit_offsets.modify_bg(gtk.STATE_ACTIVE, gtk.gdk.color_parse("#FFFF00"))

        # Now we will build the option buttons to select the Jog-rates
        # We do this dynamicly, because users are able to set them in INI File
        # because of space on the screen only 10 items are allowed
        # jogging increments

        # We get the increments from INI File with self._check_len_increments

        # The first radio button is created to get a radio button group
        # The group is called according the name off  the first button
        # We use the pressed signal, not the toggled, otherwise two signals will be emitted
        # One from the released button and one from the pressed button
        # we make a list of the buttons to later add the hardware pins to them
        
        label = _("Continuous")
        rbt0 = gtk.RadioButton(None, label)
        rbt0.connect("pressed", self.on_increment_changed,0)
        self.widgets.vbuttonbox2.pack_start(rbt0,True,True,0)
        rbt0.set_property("draw_indicator",False)
        rbt0.show()
        self.incr_rbt_list.append(rbt0)
        # the rest of the buttons are now added to the group
        # self.no_increments is set while setting the hal pins with self._check_len_increments
        for increment in range(1,self.no_increments):
            rbt = "rbt%d"%(increment)
            rbt = gtk.RadioButton(rbt0, self.data.jog_increments[increment])
            rbt.connect("pressed", self.on_increment_changed,self.data.jog_increments[increment])
            self.widgets.vbuttonbox2.pack_start(rbt,True,True,0)
            rbt.set_property("draw_indicator",False)
            rbt.show()
            self.incr_rbt_list.append(rbt)

        # This is needed only because we connect all the horizontal button
        # to hal pins, so the user can conect them to hardware buttons
        self.h_tabs = []
        tab_main = [(0,"btn_homing"), (1,"btn_touch"),(3,"btn_tool"), 
                    (7,"tbtn_fullsize_preview"), (9,"btn_exit")
                   ]
        self.h_tabs.append(tab_main)

        tab_mdi = [(9,"btn_show_kbd")]
        self.h_tabs.append(tab_mdi)

        tab_auto = [(0,"btn_load"),(1,"btn_run"),(2,"btn_stop"),(3,"tbtn_pause"),
                    (4,"btn_step"),(5,"btn_from_line"),(6,"tbtn_optional_blocks"),
                    (7,"tbtn_optional_stops"),(8,"tbtn_fullsize_preview1"),(9,"btn_edit")
                   ]
        self.h_tabs.append(tab_auto)

        tab_ref = [(1,"btn_home_all"),(3,"btn_home_x"),
                   (5,"btn_home_z"),(7,"btn_unhome_all"),(9,"btn_back_ref")
                  ]
        if not self.data.lathe_mode:
            tab_ref.append((4,"btn_home_y"))
        if len(self.data.axis_list) == 4:
            tab_ref.append((6,"btn_home_4"))
        self.h_tabs.append(tab_ref)

        tab_touch = [(0,"tbtn_edit_offsets"),(1,"btn_zero_x"),(3,"btn_zero_z"),(4,"btn_zero_g92"),
                     (5,"btn_set_value_x"),(7,"btn_set_value_z"),(8,"btn_set_selected"),(9,"btn_back_zero")
                    ]
        if not self.data.lathe_mode:
            tab_touch.append((2,"btn_zero_y"))
            tab_touch.append((6,"btn_set_value_y"))
        self.h_tabs.append(tab_touch)

        tab_setup = [(0,"btn_delete"),(4,"btn_classicladder"),(5,"btn_hal_scope"),(6,"btn_status"),
                     (7,"btn_hal_meter"),(8,"btn_calibration"),(9,"btn_show_hal")
                    ]
        self.h_tabs.append(tab_setup)

        tab_edit = [(0,"btn_open_edit"),(2,"btn_save"),(3,"btn_save_as"),(4,"btn_save_and_run"),
                    (6,"btn_new"),(8,"btn_keyb"),(9,"btn_back_edit")
                   ]
        self.h_tabs.append(tab_edit)

        tab_tool = [(0,"btn_delete_tool"),(1,"btn_add_tool"),(2,"btn_reload_tooltable"),
                    (3,"btn_apply_tool_changes"),(4,"btn_select_tool_by_no"),(5,"btn_index_tool"),
                    (6,"btn_change_tool"),(8,"btn_tool_touchoff_z"),(9,"btn_back_tool")
                   ]
        if self.data.lathe_mode:
            tab_tool.append((7,"btn_tool_touchoff_x"))
        self.h_tabs.append(tab_tool)

        tab_file = [(0,"btn_home"),(1,"btn_dir_up"),(3,"btn_sel_prev"),(4,"btn_sel_next"),
                    (5,"btn_jump_to"),(7,"btn_select"),(9,"btn_back_file_load")
                   ]
        self.h_tabs.append(tab_file)

        self.v_tabs = [(0,"tbtn_estop"),(1,"tbtn_on"),(2,"rbt_manual"),(3,"rbt_mdi"),
                       (4,"rbt_auto"),(5,"tbtn_setup"),(6,"tbtn_user_tabs")
                      ]

        self.widgets.rbt_manual.set_active(True)
        self.widgets.ntb_jog.set_current_page(0)
        self.widgets.tbtn_optional_blocks.set_active(self.gscreen.prefs.getpref("blockdel", False))
        self.emc.blockdel(self.widgets.tbtn_optional_blocks.get_active())
        self.widgets.tbtn_optional_stops.set_active(not self.gscreen.prefs.getpref("opstop", False))
        self.emc.opstop(self.widgets.tbtn_optional_stops.get_active())
        self.log = self.gscreen.prefs.getpref("log_actions", False, bool)
        self.widgets.tbtn_log_actions.set_active(self.log)
        self.widgets.chk_show_dro.set_active(self.gscreen.prefs.getpref("enable_dro", False))
        self.widgets.chk_show_offsets.set_active(self.gscreen.prefs.getpref("show_offsets", False))
        self.widgets.chk_show_dtg.set_active(self.gscreen.prefs.getpref("show_dtg", False))
        self.widgets.chk_show_offsets.set_sensitive(self.widgets.chk_show_dro.get_active())
        self.widgets.chk_show_dtg.set_sensitive(self.widgets.chk_show_dro.get_active())
        self.widgets.tbtn_view_tool_path.set_active(self.gscreen.prefs.getpref("view_tool_path",True))
        self.widgets.tbtn_view_dimension.set_active(self.gscreen.prefs.getpref("view_dimension",True))
        view = self.gscreen.prefs.getpref("gremlin_view","rbt_view_p",str)
        self.widgets[view].set_active(True)

        if "ntb_preview" in self.gscreen.inifile.findall("DISPLAY", "EMBED_TAB_LOCATION"):
            self.widgets.ntb_preview.set_property("show-tabs", True)

        # get if run from line should be used
        rfl = self.gscreen.prefs.getpref("run_from_line", "no_run", str)
        # and set the corresponding button active
        self.widgets["rbtn_%s_from_line"%rfl].set_active(True)
        if rfl == "no_run":
            self.widgets.btn_from_line.set_sensitive(False)
        else:
            self.widgets.btn_from_line.set_sensitive(True)

        # get the way to unlock the settings
        unlock = self.gscreen.prefs.getpref("unlock_way", "use", str)
        # and set the corresponding button active
        self.widgets["rbt_%s_unlock"%unlock].set_active(True)
        # if Hal pin should be used, only set the button active, if the pin is high
        if unlock == "hal" and not self.gscreen.halcomp["unlock-settings"]:
            self.widgets.tbtn_setup.set_sensitive(False)
        self.unlock_code = self.gscreen.prefs.getpref("unlock_code", "123", str) # get unlock code

        # get when the keyboard should be shown
        # and set the corresponding button active
        self.widgets.chk_use_kb_on_offset.set_active(self.gscreen.prefs.getpref("show_keyboard_on_offset", 
                                                                             True, bool))
        self.widgets.chk_use_kb_on_tooledit.set_active(self.gscreen.prefs.getpref("show_keyboard_on_tooledit", 
                                                                             False, bool))
        self.widgets.chk_use_kb_on_edit.set_active(self.gscreen.prefs.getpref("show_keyboard_on_edit", 
                                                                              True, bool))
        self.widgets.chk_use_kb_on_mdi.set_active(self.gscreen.prefs.getpref("show_keyboard_on_mdi", 
                                                                             True, bool))
        self.widgets.chk_use_kb_on_file_selection.set_active(self.gscreen.prefs.getpref("show_keyboard_on_file_selection", 
                                                                             False, bool))

        # check if the user want to display preview window insteadt of offsetpage widget
        state = self.gscreen.prefs.getpref("show_preview_on_offset", False, bool)
        if state:
            self.widgets.rbtn_show_preview.set_active(True)
        else:
            self.widgets.rbtn_show_offsets.set_active(True)

        # check if keyboard shortcuts should be used and set the chkbox widget
        self.widgets.chk_use_kb_shortcuts.set_active(self.gscreen.prefs.getpref("use_keyboard_shortcuts", 
                                                                                False, bool))

# TODO: check in settings if the user like the highlighting or not
#        self.widgets.gcode_view.buf.set_language(None)
# TODO: End

        # set the user colors of the DRO
        self.data.abs_color = self.gscreen.prefs.getpref("abs_color", "blue", str)
        self.data.rel_color = self.gscreen.prefs.getpref("rel_color", "black", str)
        self.data.dtg_color = self.gscreen.prefs.getpref("dtg_color", "yellow", str)
        self.widgets.abs_colorbutton.set_color(gtk.gdk.color_parse(self.data.abs_color))
        self.widgets.rel_colorbutton.set_color(gtk.gdk.color_parse(self.data.rel_color))
        self.widgets.dtg_colorbutton.set_color(gtk.gdk.color_parse(self.data.dtg_color))

        for axis in self.data.axis_list:
            if axis == self.axisletter_four:
                axis = 4
            self.widgets["Combi_DRO_%s"%axis].set_property("abs_color", gtk.gdk.color_parse(self.data.abs_color))
            self.widgets["Combi_DRO_%s"%axis].set_property("rel_color", gtk.gdk.color_parse(self.data.rel_color))
            self.widgets["Combi_DRO_%s"%axis].set_property("dtg_color", gtk.gdk.color_parse(self.data.dtg_color))

        self.widgets.adj_start_spindle_RPM.set_value(self.data.spindle_start_rpm)
        self.widgets.gcode_view.set_sensitive(False)
        self.tooledit_btn_delete_tool = self.widgets.tooledit1.wTree.get_object("delete")
        self.tooledit_btn_add_tool = self.widgets.tooledit1.wTree.get_object("add")
        self.tooledit_btn_reload_tool = self.widgets.tooledit1.wTree.get_object("reload")
        self.tooledit_btn_apply_tool = self.widgets.tooledit1.wTree.get_object("apply")
        self.widgets.tooledit1.hide_buttonbox(True)
        self.widgets.ntb_user_tabs.remove_page(0)
        self.add_macro_button()
        if not self.gscreen.inifile.find("DISPLAY", "EMBED_TAB_COMMAND"):
            self.widgets.tbtn_user_tabs.set_sensitive(False)

        # we click the clear button from statusbar, because else ready for homing will be displayed.
        # but this is not true, because the machine is not in On state
        self.widgets.btn_clear_statusbar.emit("clicked") 

        # call the function to change the button status
        # so every thing is ready to start
        widgetlist = ["rbt_manual", "rbt_mdi", "rbt_auto", "btn_homing", "btn_touch", "btn_tool",
                      "ntb_jog", "scl_feed", "btn_feed_100", "rbt_forward", "btn_index_tool",
                      "rbt_reverse", "rbt_stop", "tbtn_flood", "tbtn_mist", "btn_change_tool","btn_select_tool_by_no",
                      "btn_spindle_100", "scl_max_vel", "scl_spindle", "rbt_manual", 
                      "btn_tool_touchoff_x", "btn_tool_touchoff_z"
                     ]
        self.gscreen.sensitize_widgets(widgetlist,False)

        # Do we control a lathe?
        if self.data.lathe_mode:
            # is this a backtool lathe?
            temp = self.gscreen.inifile.find("DISPLAY", "BACK_TOOL_LATHE")
            self.backtool_lathe = bool(temp == "1" or temp == "True" or temp == "true")
            print("backtool = ",self.backtool_lathe)

            # we first hide the Y button to home and touch off 
            self.widgets.btn_home_y.hide()
            self.widgets.btn_zero_y.hide()
            self.widgets.btn_set_value_y.hide()
            self.widgets.lbl_replace_y.show()
            self.widgets.lbl_replace_zero_y.show()
            self.widgets.lbl_replace_set_value_y.show()
            self.widgets.btn_tool_touchoff_x.show()
            self.widgets.lbl_hide_tto_x.hide()

            # we have to re-arrange the jog buttons, so first remove all button
            self.widgets.tbl_jog_btn.remove(self.widgets.btn_y_minus)
            self.widgets.tbl_jog_btn.remove(self.widgets.btn_y_plus)
            self.widgets.tbl_jog_btn.remove(self.widgets.btn_x_minus)
            self.widgets.tbl_jog_btn.remove(self.widgets.btn_x_plus)
            self.widgets.tbl_jog_btn.remove(self.widgets.btn_z_minus)
            self.widgets.tbl_jog_btn.remove(self.widgets.btn_z_plus)

            # now we place them in a different order
            if self.backtool_lathe:
                self.widgets.tbl_jog_btn.attach(self.widgets.btn_x_plus,1,2,0,1,gtk.SHRINK, gtk.SHRINK)
                self.widgets.tbl_jog_btn.attach(self.widgets.btn_x_minus,1,2,2,3,gtk.SHRINK, gtk.SHRINK)
            else:
                self.widgets.tbl_jog_btn.attach(self.widgets.btn_x_plus,1,2,2,3,gtk.SHRINK, gtk.SHRINK)
                self.widgets.tbl_jog_btn.attach(self.widgets.btn_x_minus,1,2,0,1,gtk.SHRINK, gtk.SHRINK)
            self.widgets.tbl_jog_btn.attach(self.widgets.btn_z_plus,2,3,1,2,gtk.SHRINK, gtk.SHRINK)
            self.widgets.tbl_jog_btn.attach(self.widgets.btn_z_minus,0,1,1,2,gtk.SHRINK, gtk.SHRINK)
            
            # The Y DRO we make to a second X DRO to indicate the diameter
            self.widgets.Combi_DRO_y.set_to_diameter(True)
            self.widgets.Combi_DRO_y.set_property("joint_number",0)

            # we change the axis letters of the DRO's
            self.widgets.Combi_DRO_x.change_axisletter("R")
            self.widgets.Combi_DRO_y.change_axisletter("D")

            # For gremlin we don"t need the following button
            if self.backtool_lathe:
                self.widgets.rbt_view_y2.set_active(True)
            else:
                self.widgets.rbt_view_y.set_active(True)
            self.widgets.rbt_view_p.hide()
            self.widgets.rbt_view_x.hide()
            self.widgets.rbt_view_z.hide()
            
            # check if G7 or G8 is active
            # this is set on purpose wrong, because we want the periodic 
            # to update the state correctly
            if "G7" in self.data.active_gcodes:
                self.data.diameter_mode = False
            else:
                self.data.diameter_mode = True

        else:
            # the Y2 view is not needed on a mill
            self.widgets.rbt_view_y2.hide()
            # X Offset is not necesary on a mill
            self.widgets.lbl_tool_offset_x.hide()
            self.widgets.lbl_offset_x.hide()
            self.widgets.btn_tool_touchoff_x.hide()
            self.widgets.lbl_hide_tto_x.show()
            # is there a 4_th axis? 
            # We need to show the corresponding widgets
            # and change some sizes
            if len(self.data.axis_list) > 3:
                #let us find out wich axis is the 4-th one
                # and set things accordingly

                self.widgets.btn_4_plus.set_label("%s+"%self.axisletter_four.upper())
                self.widgets.btn_4_minus.set_label("%s-"%self.axisletter_four.upper())
                self.widgets.btn_4_plus.show()
                self.widgets.btn_4_minus.show()
                self.widgets.lbl_replace_4.hide()
                self.widgets.btn_home_4.show()

                # we have to re-arrange the jog buttons, so first remove all button
                self.widgets.tbl_jog_btn.remove(self.widgets.btn_z_minus)
                self.widgets.tbl_jog_btn.remove(self.widgets.btn_z_plus)
                self.widgets.tbl_jog_btn.remove(self.widgets.btn_4_minus)
                self.widgets.tbl_jog_btn.remove(self.widgets.btn_4_plus)

                # now we place them in a different order
                self.widgets.tbl_jog_btn.attach(self.widgets.btn_z_plus,2,3,0,1,gtk.SHRINK, gtk.SHRINK)
                self.widgets.tbl_jog_btn.attach(self.widgets.btn_z_minus,2,3,2,3,gtk.SHRINK, gtk.SHRINK)
                self.widgets.tbl_jog_btn.attach(self.widgets.btn_4_plus,3,4,0,1,gtk.SHRINK, gtk.SHRINK)
                self.widgets.tbl_jog_btn.attach(self.widgets.btn_4_minus,3,4,2,3,gtk.SHRINK, gtk.SHRINK)

        # this must be done last, otherwise we will get wrong values
        # because the window is not fully realized
        self.init_notification()

    # shows "Onboard" virtual keyboard if available
    # else error message
    def init_keyboard(self,args="",x="",y=""):
#         result = subprocess.call("setxkbmap -layout de",shell=True)
#         if result<> 0:
#             print("error",result)
        try:
            self.data.ob = subprocess.Popen(["onboard","--xid",args,x,y],
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   close_fds=True)
            sid = self.data.ob.stdout.readline()
            print"keyboard", sid # skip header line
            socket = gtk.Socket()
            socket.show()
            self.widgets.key_box.add(socket)
            socket.add_id(long(sid))
        except:
            print _("Error with launching 'Onboard' on-screen keyboard program, is onboard installed?")

    def kill_keyboard(self):
        try:
            self.widgets.key_box.hide()
            self.data.ob.kill()
            self.data.ob.terminate()
            self.data.ob = None
        except:
            try:
                self.data.ob.kill()
                self.data.ob.terminate()
                self.data.ob = None
            except:
                pass

    def init_offsetpage(self):
        temp = "xyzabcuvw"
        self.widgets.offsetpage1.set_col_visible(temp,False)
        temp =""
        for axis in self.data.axis_list:
            temp=temp+axis
        self.widgets.offsetpage1.set_col_visible(temp,True)
        CONFIGPATH = os.environ["CONFIG_DIR"]
        path = os.path.join(CONFIGPATH,self.data.varfile)
        self.widgets.offsetpage1.set_filename(path)
        self.widgets.offsetpage1.hide_buttonbox(True)
        self.widgets.offsetpage1.set_row_visible("1",False)
        self.widgets.offsetpage1.set_font("sans 12")
        self.widgets.offsetpage1.set_foreground_color("#28D0D9")
        self.widgets.offsetpage1.selection_mask = ("Tool","G5x","Rot")
        systemlist = ["Tool", "G5x", "Rot", "G92", "G54", "G55", "G56", "G57", "G58", "G59", "G59.1",
                   "G59.2", "G59.3"]
        names = []
        for system in systemlist:
            system_name = "system_name_%s"%system
            name = self.gscreen.prefs.getpref(system_name, system, str)
            names.append([system,name])
        self.widgets.offsetpage1.set_names(names)

    # Notification stuff.
    def init_notification(self):
        start_as = "rbtn_" + self.gscreen.prefs.getpref("screen1", "window",str)
        xpos,ypos = self.widgets.window1.window.get_origin()
        self.notification.set_property('x_pos' , self.widgets.adj_x_pos_popup.get_value())
        self.notification.set_property('y_pos' , self.widgets.adj_y_pos_popup.get_value())
        self.notification.set_property('message_width' , self.widgets.adj_width_popup.get_value())
        self.notification.set_property('max_messages', self.widgets.adj_max_messages.get_value())
        self.notification.set_property('use_frames', self.widgets.chk_use_frames.get_active())
        self.notification.set_property('font', self.widgets.fontbutton_popup.get_font_name())
# TODO:
        # this ones are not finished yet in notifications, we do add them at 
        # a later development state, just to show they are there
        self.notification.set_property('icon_size' , 1)
        self.notification.set_property('top_to_bottom', True)
# TODO: End

    def on_Combi_DRO_clicked(self, widget, joint_number, order):
        for axis in self.data.axis_list:
            if axis == self.axisletter_four:
                axis = 4
            self.widgets["Combi_DRO_%s"%axis].set_order(order)
        if self.data.lathe_mode:
            self.widgets.Combi_DRO_y.set_order(order)
        self._offset_changed(None,None)
# from here only needed, if the DRO button will remain in gmoccapy
        if order[0] == "Abs" and self.widgets.tbtn_rel.get_label() != "Abs":
            self.widgets.tbtn_rel.set_active(False)
        if order[0] == "Rel" and self.widgets.tbtn_rel.get_label() != self.widgets.Combi_DRO_x.system:
            self.widgets.tbtn_rel.set_active(True)
        if order[0] == "DTG":
            self.widgets.tbtn_dtg.set_active(True)
        else:
            self.widgets.tbtn_dtg.set_active(False)
# to here only needed, if the DRO button will remain in gmoccapy

    def on_Combi_DRO_units_changed(self, widget, metric_units):
# from here only needed, if the DRO button will remain in gmoccapy
        if not self.widgets.chk_auto_units.get_active():
            return
# to here only needed, if the DRO button will remain in gmoccapy

        # This is needed to set, because otherwise we will get the speed values
        # only in machine units, now they are converted according active gcode
        # gscreen.status also changes the value of self.data.dro_units
        if metric_units:
            self.gscreen.status.dro_mm(1)
        else:
            self.gscreen.status.dro_inch(1)

# from here only needed, if the DRO button will remain in gmoccapy
    def on_Combi_DRO_system_changed(self, widget, system):
        if self.widgets.tbtn_rel.get_active():
            self.widgets.tbtn_rel.set_label(system)
        else:
            self.widgets.tbtn_rel.set_label("Abs")

    def on_tbtn_rel_toggled(self, widget, data = None):
        if self.widgets.tbtn_dtg.get_active():
            self.widgets.tbtn_dtg.set_active(False)
        if widget.get_active():
            widget.set_label(self.widgets.Combi_DRO_x.system)
            order = ["Rel", "Abs", "DTG"]
        else:
            widget.set_label("Abs")
            order = ["Abs", "DTG", "Rel"]
        self.on_Combi_DRO_clicked(None, None, order)

    def on_tbtn_dtg_toggled(self, widget, data = None):
        if widget.get_active():
            widget.set_label("GTD")
            order = ["DTG", "Rel", "Abs"]
        else:
            widget.set_label("DTG")
            if self.widgets.tbtn_rel.get_active():
                order = ["Rel", "Abs", "DTG"]
            else:
                order = ["Abs", "DTG", "Rel"]
        self.on_Combi_DRO_clicked(None, None, order)

    def on_tbtn_units_toggled(self, widget, data = None):
        if widget.get_active():
            widget.set_label("inch")
            metric_units = False
            self.gscreen.status.dro_inch(1)
        else:
            widget.set_label("mm")
            metric_units = True
            self.gscreen.status.dro_mm(1)
        for axis in self.data.axis_list:
            if axis == self.axisletter_four:
                axis = 4
            self.widgets["Combi_DRO_%s"%axis].set_to_inch(not metric_units)
        if self.data.lathe_mode:
            self.widgets.Combi_DRO_y.set_to_inch(not metric_units)

    def on_chk_auto_units_toggled(self, widget, data=None):
        for axis in self.data.axis_list:
            if axis == self.axisletter_four:
                axis = 4
            self.widgets["Combi_DRO_%s"%axis].set_auto_units(self.widgets.chk_auto_units.get_active())
        if self.data.lathe_mode:
            self.widgets.Combi_DRO_y.set_auto_units(self.widgets.chk_auto_units.get_active())
        self.gscreen.prefs.putpref("use_auto_units", self.widgets.chk_auto_units.get_active(), bool)

    def on_chk_show_dro_btn_toggled(self, widget, data=None):
        if self.widgets.chk_show_dro_btn.get_active():
            self.widgets.tbl_dro_button.show()
            self.widgets.chk_auto_units.set_active(False)
            self.widgets.chk_auto_units.set_sensitive(False)
        else:
            self.widgets.tbl_dro_button.hide()
            self.widgets.chk_auto_units.set_active(True)
            self.widgets.chk_auto_units.set_sensitive(True)
        self.gscreen.prefs.putpref("show_dro_btn", self.widgets.chk_show_dro_btn.get_active(), bool)
# to here only needed, if the DRO button will remain in gmoccapy

    def on_adj_x_pos_popup_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("x_pos_popup", widget.get_value(),float)
        self.init_notification()

    def on_adj_y_pos_popup_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("y_pos_popup", widget.get_value(),float)
        print("ypos = ",widget.get_value())
        self.init_notification()

    def on_adj_width_popup_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("width_popup", widget.get_value(),float)
        self.init_notification()

    def on_adj_max_messages_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("max_messages", widget.get_value(),float)
        self.init_notification()

    def on_chk_use_frames_toggled(self, widget, data=None):
        self.gscreen.prefs.putpref("use_frames", widget.get_active())
        self.init_notification()

    def on_fontbutton_popup_font_set(self,font):
        self.gscreen.prefs.putpref("message_font", self.widgets.fontbutton_popup.get_font_name() , str)
        self.init_notification()

    def on_btn_launch_test_message_pressed(self, widget = None, data = None):
        index = len(self.notification.messages)
        text = _("Halo, welcome to the test message %d")%index
        self.notification.add_message(text , INFO_ICON)

    # Icon file selection stuff
    def init_IconFileSelection(self):
        # self.widgets.IconFileSelection1.set_property("start_dir",startdir)
        # is set in init with the selection of the NC_FILES path from INI
        iconsize= 48
        self.widgets.IconFileSelection1.set_property("icon_size",iconsize)

        file_ext = self._get_file_ext()
        filetypes =""
        for ext in file_ext:
            filetypes += ext.replace("*.","") + ","
        self.widgets.IconFileSelection1.set_property("filetypes",filetypes)
        
        jump_to_dir = self.gscreen.prefs.getpref("jump_to_dir", os.path.expanduser("~"),str)
        self.widgets.jump_to_dir_chooser.set_current_folder(jump_to_dir)
        self.widgets.IconFileSelection1.set_property("jump_to_dir", jump_to_dir)
        
        self.widgets.IconFileSelection1.show_buttonbox(False)
        self.widgets.IconFileSelection1.show_filelabel(False)

    # init the preview
    def init_gremlin(self):
        self.widgets.grid_size.set_value(self.data.grid_size) 
        self.widgets.gremlin.grid_size = self.data.grid_size
        self.widgets.gremlin.set_property("view",self.data.plot_view[0])
        self.widgets.gremlin.set_property("metric_units", self.widgets.Combi_DRO_x.metric_units)

    # init the function to hide the cursor 
    def init_hide_cursor(self):
        self.widgets.chk_hide_cursor.set_active(self.data.hide_cursor)
        # if hide cursor requested
        # we set the graphics to use touchscreen controls
        if self.data.hide_cursor:
            self.widgets.window1.window.set_cursor(INVISABLE)
            self.widgets.gremlin.set_property("use_default_controls",False)
        else:
            self.widgets.window1.window.set_cursor(None)
            self.widgets.gremlin.set_property("use_default_controls",True)

    # init the keyboard shortcut bindings
    def initialize_keybindings(self):
        try:
            accel_group = gtk.AccelGroup()
            self.widgets.window1.add_accel_group(accel_group)
            self.widgets.button_estop.add_accelerator("clicked", accel_group, 65307,0, gtk.ACCEL_LOCKED)
        except:
            pass
        self.widgets.window1.connect("key_press_event", self.on_key_event,1)
        self.widgets.window1.connect("key_release_event", self.on_key_event,0)

    def on_key_event(self, widget, event, signal):

        #get the keyname
        keyname = gtk.gdk.keyval_name(event.keyval)
        #print("pressed key = ",keyname

        # estop with F1 shold work every time
        # so should also escape aboart actions
        if keyname == "F1": # will estop the machine, but not reset estop!
            self.emc.estop(1)
            return True
        if keyname == "Escape":
            self.emc.abort()
            return True

        # This will avoid excecuting the key press event several times caused by keyboard auto repeat
        if self.data.key_event_last[0] == keyname and self.data.key_event_last[1] == signal:
            return True

        try:
            if keyname =="F2" and signal:
                # only turn on if no estop!
                if self.widgets.tbtn_estop.get_active():
                    return True
                self.widgets.tbtn_on.emit("clicked")
                return True
        except:
            pass

        if keyname =="space" and signal:
            if event.state & gtk.gdk.CONTROL_MASK: # only do it when control is hold down
                self.notification.del_message(-1)
                self.widgets.window1.grab_focus()
                return

        if keyname =="Super_L" and signal: # Left Windows
            self.notification.del_last()
            self.widgets.window1.grab_focus()
            return

        # if the user do not want to use keyboard shortcuts, we leave here
        # in this case we do not return true, otherwise entering code in MDI history 
        # and the integrated editor will not work
        if not self.widgets.chk_use_kb_shortcuts.get_active():
            print("Settings say: do not use keyboard shortcuts, aboart")
            return

        # Only in manual mode jogging with keyboard is allowed
        # in this case we do not return true, otherwise entering code in MDI history 
        # and the integrated editor will not work
        # we also check if we are in settings or terminal or alarm page
        if self.emcstat.task_mode <> _MANUAL or not self.widgets.ntb_main.get_current_page() == 0:
            return

        # offset page is active, so keys must go through
        if self.widgets.ntb_preview.get_current_page() == 1:
            return

        # tooledit page is active, so keys must go through
        if self.widgets.ntb_preview.get_current_page() == 2:
            return

        # take care of differnt key handling for lathe operation
        if self.data.lathe_mode:
            if keyname == "Page_Up" or keyname == "Page_Down":
                return

        if event.state & gtk.gdk.SHIFT_MASK: # SHIFT is hold down, fast jogging active
            fast = True
        else:
            fast = False

        if keyname == "Up":
            if self.data.lathe_mode:
                if self.backtool_lathe:
                    widget = self.widgets.btn_x_plus
                else:
                    widget = self.widgets.btn_x_minus
            else:
                widget = self.widgets.btn_y_plus
            if signal:
                self.on_btn_jog_pressed(widget,fast)
            else:
                self.on_btn_jog_released(widget)
        elif keyname == "Down":
            if self.data.lathe_mode:
                if self.backtool_lathe:
                    widget = self.widgets.btn_x_minus
                else:
                    widget = self.widgets.btn_x_plus
            else:
                widget = self.widgets.btn_y_minus
            if signal:
                self.on_btn_jog_pressed(widget,fast)
            else:
                self.on_btn_jog_released(widget)
        elif keyname == "Left":
            if self.data.lathe_mode:
                widget = self.widgets.btn_z_minus
            else:
                widget = self.widgets.btn_x_minus
            if signal:
                self.on_btn_jog_pressed(widget,fast)
            else:
                self.on_btn_jog_released(widget)
        elif keyname == "Right":
            if self.data.lathe_mode:
                widget = self.widgets.btn_z_plus
            else:
                widget = self.widgets.btn_x_plus
            if signal:
                self.on_btn_jog_pressed(widget,fast)
            else:
                self.on_btn_jog_released(widget)
        elif keyname == "Page_Up":
            widget = self.widgets.btn_z_plus
            if signal:
                self.on_btn_jog_pressed(widget,fast)
            else:
                self.on_btn_jog_released(widget)
        elif keyname == "Page_Down":
            widget = self.widgets.btn_z_minus
            if signal:
                self.on_btn_jog_pressed(widget,fast)
            else:
                self.on_btn_jog_released(widget)
        else:
            print("This key has not been implemented yet")
            print "Key %s (%d) was pressed" % (keyname, event.keyval),signal, self.data.key_event_last
        self.data.key_event_last = keyname,signal
        return True

    # check if macros are in the INI file and add them to MDI Button List
    def add_macro_button(self):
        macros = self.gscreen.inifile.findall("MACROS", "MACRO")
        num_macros = len(macros)
        if num_macros > 9:
            message = _("no more than 9 macros are allowed, only the first 9 will be used")
            self.gscreen.add_alarm_entry(message)
            print(message)
            num_macros = 9
        for increment in range(0,num_macros):
            name = macros[increment]
            # shorten the name if it is to long
            if len(name) > 11:
                lbl = name[0:10]
            else:
                lbl = macros[increment]
            btn = gtk.Button(lbl,None,False)
            btn.connect("pressed", self.on_btn_macro_pressed,name)
            btn.position = increment
            # we add the button to a list to be able later to see what makro to excecute
            self.macrobuttons.append(btn)
            self.widgets.hbtb_MDI.pack_start(btn,True,True,0)
            btn.show()
        # if there is still place, we fill it with empty labels, to be sure the button will not be on differnt
        # places if the amount of macros change.
        if num_macros < 9:
            for label_space in range(num_macros,9):
                lbl = "lbl_sp_%s"%label_space
                lbl = gtk.Label(lbl)
                lbl.position = label_space
                lbl.set_text("")
                self.widgets.hbtb_MDI.pack_start(lbl,True,True,0)
                lbl.show()
        self.widgets.hbtb_MDI.non_homogeneous = False

    # What to do if a macro button has been pushed
    def on_btn_macro_pressed(self, widget = None, data = None):
        o_codes = data.split()
        subroutines_folder = self.gscreen.inifile.find("RS274NGC", "SUBROUTINE_PATH")
        if not subroutines_folder:
            subroutines_folder = self.gscreen.inifile.find("DISPLAY", "PROGRAM_PREFIX")
        if not subroutines_folder:
            message = _("No subroutine folder or program prefix is given in the ini file \n")
            message += _("so the corresponding file could not be found")
            self.gscreen.warning_dialog(_("Important Warning"), True, message)
            self.gscreen.add_alarm_entry( message)
            self.widgets.statusbar1.push(1, message)
            return
        file = subroutines_folder + "/" + o_codes[0] + ".ngc"
        if not os.path.isfile(file):
            message = _("File %s of the macro could not be found\n"%[o_codes[0] + ".ngc"])
            message += _("we searched in subdirectory %s"%[subroutines_folder])
            self.gscreen.warning_dialog(_("Important Warning"), True, message)
            self.gscreen.add_alarm_entry( message)
            self.widgets.statusbar1.push(1, message)
            return
        command = str("O<" + o_codes[0] + "> call")
        for code in o_codes[1:]:
            parameter = self.entry_dialog(data = None, header = _("Enter value:"), 
                                          label=_("Set parameter %s to:")%code, integer=False )
            if parameter == "ERROR":
                print(_("conversion error"))
                self.gscreen.add_alarm_entry(_("Conversion error because off wrong entry for macro %s")
                                             %o_codes[0])
                self.gscreen.warning_dialog(_("Conversion error !"), True, 
                                            _("Please enter only numerical values\nValues have not been applied"))
                return
            elif parameter == "CANCEL":
                self.gscreen.add_alarm_entry(_("entry for macro %s has been canceled")%o_codes[0])
                return
            else:
                self.gscreen.add_alarm_entry(_("macro {0} , parameter {1} set to {2:f}").format(o_codes[0],code,parameter))
            command = command + " [" + str(parameter) + "] "
# TODO: Should not only clear the plot, but also the loaded programm?
        #self.emc.emccommand.program_open("")
        #self.emc.emccommand.reset_interpreter()
        self.widgets.gremlin.clear_live_plotter()
# TODO: End
        self.gscreen.mdi_control.user_command(command)
        for btn in self.macrobuttons:
            btn.set_sensitive(False)
        # we change the widget_image and use the button to interupt running macros
        self.widgets.btn_show_kbd.set_image(self.widgets.img_brake_macro)
        self.widgets.btn_show_kbd.set_property("tooltip-text",_("interrupt running macro"))
        self.widgets.ntb_info.set_current_page(0)

    # There are some settings we can only do if the window is on the screen allready
    def on_window1_show(self, widget, data=None):
        # it is time to get the correct estop state and set the button status
        self.gscreen.status.emcstat.poll()
        estop = self.gscreen.status.emcstat.task_state == self.gscreen.status.emc.STATE_ESTOP
        if estop:
            self.widgets.tbtn_estop.set_active(True)
            self.widgets.tbtn_estop.set_image(self.widgets.img_emergency)
            self.widgets.tbtn_on.set_image(self.widgets.img_machine_off)
        else:
            self.widgets.tbtn_estop.set_active(False)
            self.widgets.tbtn_estop.set_image(self.widgets.img_emergency_off)
            self.widgets.tbtn_on.set_sensitive(True)
        
        # if a file should be loaded, we will do so
        file = self.gscreen.prefs.getpref("open_file", " ", str)
        if file:
            self.widgets.file_to_load_chooser.set_filename(file)
            self.widgets.hal_action_open.load_file(file)

        # check how to start the GUI
        start_as = "rbtn_" + self.gscreen.prefs.getpref("screen1", "window",str)
        self.widgets[start_as].set_active(True)
        if start_as == "rbtn_fullscreen":
            self.widgets.window1.fullscreen()
        elif start_as == "rbtn_maximized":
            self.widgets.window1.maximize()
        else:
            self.widgets.window1.move(int(self.widgets.adj_x_pos.get_value()),
                                      int(self.widgets.adj_y_pos.get_value()))
            self.widgets.window1.resize(int(self.widgets.adj_width.get_value()),
                                        int(self.widgets.adj_height.get_value()))

        # does the user want to show screen2 
        self.widgets.tbtn_use_screen2.set_active(self.gscreen.prefs.getpref("use_screen2", False, bool))
        self.emc.set_manual_mode()

    # kill keyboard and estop machine before closing
    def on_window1_destroy(self, widget, data=None):
        self.kill_keyboard()
        print "estopping / killing gscreen"
        self.emc.machine_off(1)
        self.emc.estop(1)
        time.sleep(2)
        gtk.main_quit()

    # use the current loaded file to be loaded on start up
    def on_btn_use_current_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("use_current_clicked %s"%self.data.file)
        if self.data.file:
            self.widgets.file_to_load_chooser.set_filename(self.data.file)
            self.gscreen.prefs.putpref("open_file", self.data.file, str)

    # Clear the status to load a file on start up, so ther will not be loaded a programm
    # on the next start of the GUI
    def on_btn_none_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("button none clicked %s")
        self.widgets.file_to_load_chooser.set_filename(" ")
        self.gscreen.prefs.putpref("open_file", " ", str)

    # toggle emergency button
    def on_tbtn_estop_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("tbtn_estop_clicked")
        if self.widgets.tbtn_estop.get_active(): # estop is active, open circuit
            self.widgets.tbtn_estop.set_image(self.widgets.img_emergency)
            self.widgets.tbtn_on.set_image(self.widgets.img_machine_on)
            self.emc.estop(1)
            self.widgets.tbtn_on.set_sensitive(False)
            self.widgets.tbtn_on.set_active(False)
        else:   # estop circuit is fine
            self.widgets.tbtn_estop.set_image(self.widgets.img_emergency_off)
            self.widgets.tbtn_on.set_image(self.widgets.img_machine_off)
            self.emc.estop_reset(1)
            self.widgets.tbtn_on.set_sensitive(True)

# TODO: find out why the values are modified by 1 on startup 
#       and correct this to avoid the need of this clicks
        # will need to click, otherwise we get 101 % after startup
        self.widgets.btn_feed_100.emit("clicked")
        self.widgets.btn_spindle_100.emit("clicked")
# TODO: End

    # toggle machine on / off button
    def on_tbtn_on_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("hal_tgbt_on_clicked")
        if self.widgets.tbtn_on.get_active():
            self.widgets.tbtn_on.set_image(self.widgets.img_machine_on)
            self.emc.machine_on(1)
            self._update_widgets(True)
            if self.widgets.ntb_main.get_current_page() != 0:
                self.widgets.rbt_manual.emit("pressed")
        else:
            self.widgets.tbtn_on.set_image(self.widgets.img_machine_off)
            self.emc.machine_off(1)
            self._update_widgets(False)

    def _update_widgets(self,state):
        widgetlist = ["rbt_manual", "btn_homing", "btn_touch", "btn_tool",
                      "ntb_jog", "scl_feed", "btn_feed_100", "rbt_forward", "btn_index_tool",
                      "rbt_reverse", "rbt_stop", "tbtn_flood", "tbtn_mist", "btn_change_tool","btn_select_tool_by_no",
                      "btn_spindle_100", "scl_max_vel", "scl_spindle", 
                      "btn_tool_touchoff_x", "btn_tool_touchoff_z"
                     ]
        self.gscreen.sensitize_widgets(widgetlist,state)

    # The mode buttons
    def on_rbt_manual_pressed(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_manual_pressed")
        self.emc.set_manual_mode()
        self.widgets.ntb_main.set_current_page(0)
        self.widgets.ntb_button.set_current_page(0)
        self.widgets.ntb_info.set_current_page(0)
        self.widgets.ntb_jog.set_current_page(0)

    def on_rbt_mdi_pressed(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_mdi_pressed")
        self.emc.set_mdi_mode()
        if self.widgets.chk_use_kb_on_mdi.get_active():
            self.widgets.ntb_info.set_current_page(1)
        else:
            self.widgets.ntb_info.set_current_page(0)
        self.widgets.ntb_main.set_current_page(0)
        self.widgets.ntb_button.set_current_page(1)
        self.widgets.ntb_jog.set_current_page(1)
        self.widgets.hal_mdihistory.entry.grab_focus()

    def on_rbt_auto_pressed(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_auto_pressed")
        self.emc.set_auto_mode()
        self.widgets.ntb_main.set_current_page(0)
        self.widgets.ntb_button.set_current_page(2)
        self.widgets.ntb_info.set_current_page(0)
        self.widgets.ntb_jog.set_current_page(2)

    def on_ntb_main_switch_page(self, widget, page, page_num, data=None):
        if self.log: 
            message = "ntb_main_page changed to %s"%self.widgets.ntb_main.get_current_page()
            self.gscreen.add_alarm_entry(message)
        if self.widgets.tbtn_setup.get_active():
            if page_num != 1L: # setup page is active,
                 self.widgets.tbtn_setup.set_active(False)

    def on_tbtn_setup_toggled(self, widget, data=None):
        if widget.get_active():
            if self.log: self.gscreen.add_alarm_entry("tbtn_setup_pressed")
            code = False
            # here the user don"t want an unlock code
            if self.widgets.rbt_no_unlock.get_active():
                code = True
            # if hal pin is true, we are allowed to enter settings, this may be 
            # realized using a key switch
            if self.widgets.rbt_hal_unlock.get_active() and self.gscreen.halcomp["unlock-settings"]:
                code = True
            # else we ask for the code using the system.dialog
            if self.widgets.rbt_use_unlock.get_active():
                if self.system_dialog():
                    code = True
                else:
                    code = False
            # Lets see if the user has the right to enter settings
            if code:
                self.widgets.ntb_main.set_current_page(1)
                self.widgets.ntb_setup.set_current_page(1)
                self.widgets.ntb_button.set_current_page(5)
            else:
                if self.widgets.rbt_hal_unlock.get_active():
                    message = _("Hal Pin is low, Access denied")
                else:
                    message = _("wrong code entered, Access denied")
                self.gscreen.warning_dialog(_("Just to warn you"), True, message)
                self.gscreen.add_alarm_entry( message)
                self.widgets.statusbar1.push(1, message)
                self.widgets.rbt_manual.emit("pressed")
                self.widgets.rbt_manual.set_active(True)
        else:
            self.widgets.rbt_manual.set_active(True)
            self.widgets.rbt_manual.pressed()

    # This dialog is for unlocking the system tab
    # The unlock code number is defined at the top of the page
    def system_dialog(self):
        dialog = gtk.Dialog(_("Enter System Unlock Code"),
                   self.widgets.window1,
                   gtk.DIALOG_DESTROY_WITH_PARENT,
                   (gtk.STOCK_CANCEL, gtk.RESPONSE_REJECT,
                    gtk.STOCK_OK, gtk.RESPONSE_ACCEPT))
        label = gtk.Label(_("Enter System Unlock Code"))
        label.modify_font(pango.FontDescription("sans 20"))
        calc = gladevcp.Calculator()
        dialog.vbox.pack_start(label)
        dialog.vbox.add(calc)
        calc.set_value("")
        calc.set_property("font","sans 20")
        calc.set_editable(True)
        calc.entry.connect("activate", lambda w : dialog.emit("response",gtk.RESPONSE_ACCEPT))
        dialog.parse_geometry("400x400")
        dialog.set_decorated(True)
        dialog.show_all()
        response = dialog.run()
        code = calc.get_value()
        dialog.destroy()
        if response == gtk.RESPONSE_ACCEPT:
            if code == int(self.unlock_code):
                return True
        return False

    # Show or hide the user tabs
    def on_tbtn_user_tabs_toggled(self, widget, data=None):
        if widget.get_active():
            self.widgets.ntb_main.set_current_page(2)
            self.widgets.tbtn_fullsize_preview.set_sensitive(False)
        else:
            self.widgets.ntb_main.set_current_page(0)
            self.widgets.tbtn_fullsize_preview.set_sensitive(True)

    # The homing functions
    def on_btn_homing_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_homing_clicked")
        self.widgets.ntb_button.set_current_page(3)

    def on_btn_home_all_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("button ref all clicked")
        self.emc.home_all(1)

    def on_btn_unhome_all_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("button unhome all clicked")
        self.data.all_homed = False
        self.emc.unhome_all(1)

    def on_btn_home_selected_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("button home selected clicked")
        if widget == self.widgets.btn_home_x:
            axis = 0
        elif widget == self.widgets.btn_home_y:
            axis = 1
        elif widget == self.widgets.btn_home_z:
            axis = 2
        elif widget == self.widgets.btn_home_4:
            axis = "xyzabcuvw".index(self.axisletter_four)
        self.emc.home_selected(axis)

    def on_chk_ignore_limits_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("chk_ignore_limits_toggled %s"%widget.get_active())
        self.emc.override_limits(self.widgets.chk_ignore_limits.get_active())

    def on_tbtn_fullsize_preview_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("Fullsize Preview set to %s"%widget.get_active())
        if widget.get_active():
            self.widgets.box_info.hide()
            self.widgets.vbx_jog.hide()
            self.widgets.gremlin.set_property("metric_units", self.widgets.Combi_DRO_x.metric_units)
            self.widgets.gremlin.set_property("enable_dro", True)
            if self.data.lathe_mode:
                self.widgets.gremlin.set_property("show_lathe_radius", not self.data.diameter_mode)
        else:
            self.widgets.box_info.show()
            self.widgets.vbx_jog.show()
            if not self.widgets.chk_show_dro.get_active():
                self.widgets.gremlin.set_property("enable_dro",False) 

    # If button exit is klickt, press emergency button bevor closing the application
    def on_btn_exit_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_exit_clicked")
        self.widgets.tbtn_estop.set_active(1)
        self.widgets.window1.destroy()

    # this are hal-tools through gsreen function
    def on_btn_show_hal_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_show_hal_clicked")
        self.gscreen.on_halshow(None)

    def on_btn_calibration_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_calibration_clicked")
        self.gscreen.on_calibration(None)

    def on_btn_hal_meter_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_hal_meter_clicked")
        self.gscreen.on_halmeter(None)

    def on_btn_status_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("hal_btn_status_clicked")
        self.gscreen.on_status(None)

    def on_btn_hal_scope_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("hal_btn_hal_scope_clicked")
        self.gscreen.on_halscope(None)

    def on_btn_classicladder_clicked(self, widget, data=None):
        if  hal.component_exists("classicladder_rt"):
            p = os.popen("classicladder  &","w")
        else:
            self.gscreen.warning_dialog(_("INFO:"), True, 
                                        _("Classicladder real-time component not detected"))
            self.gscreen.add_alarm_entry(_("ladder not available - is the real-time component loaded?"))

    def _check_spindle_max(self,rpm):
        spindle_override = self.widgets.scl_spindle.get_value() / 100
        real_spindle_speed = rpm * spindle_override
        if real_spindle_speed > self.widgets.adj_spindle_bar_max.get_value():
            try:
                value_to_set = self.widgets.scl_spindle.get_value()/(real_spindle_speed/self.widgets.adj_spindle_bar_max.get_value())
                self.widgets.scl_spindle.set_value(value_to_set)
            except:
                pass

    # spindle stuff
    def _set_spindle(self, widget, data=None):
        rpm = abs(float(self.data.active_spindle_command))
        if rpm == 0:
            rpm = self.data.spindle_start_rpm

        self._check_spindle_max(rpm)
        
        if widget == self.widgets.rbt_forward:
            self.emc.emccommand.spindle(1,rpm)
        elif widget == self.widgets.rbt_reverse:
            self.emc.emccommand.spindle(-1,rpm)
        elif widget == self.widgets.rbt_stop:
            self.emc.emccommand.spindle(0)
        else:
             self.gscreen.add_alarm_entry(_("Something went wrong, we have an unknown widget"))

        if self.log: self.gscreen.add_alarm_entry("Spindle set to %i rpm, mode is %s"%(rpm,self.emc.get_mode()))
        self.widgets.lbl_spindle_act.set_label("S %s"%rpm)
        self.on_scl_spindle_value_changed(self.widgets.scl_spindle)

    def on_rbt_forward_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_forward_clicked")
        if self.widgets.rbt_forward.get_active():
            self.widgets.rbt_forward.set_image(self.widgets.img_forward_on)
            self._set_spindle(widget)
        else:
            self.widgets.rbt_forward.set_image(self.widgets.img_forward)

    def on_rbt_reverse_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_reverse_clicked")
        if self.widgets.rbt_reverse.get_active():
            self.widgets.rbt_reverse.set_image(self.widgets.img_reverse_on)
            self.widgets.hal_hbar_spindle_feedback.set_property("max",float(self.min_spindle_rev)*-1)
            self.widgets.hal_hbar_spindle_feedback.set_property("min",float(self.max_spindle_rev)*-1)
            self._set_spindle(widget)
        else:
            self.widgets.rbt_reverse.set_image(self.widgets.img_reverse)
            self.widgets.hal_hbar_spindle_feedback.set_property("min",float(self.min_spindle_rev))
            self.widgets.hal_hbar_spindle_feedback.set_property("max",float(self.max_spindle_rev))

    def on_rbt_stop_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_stop_clicked")
        if self.widgets.rbt_stop.get_active():
            self.widgets.rbt_stop.set_image(self.widgets.img_stop_on)
            self._set_spindle(widget)
        else:
            self.widgets.rbt_stop.set_image(self.widgets.img_sstop)

    def on_hal_hbar_spindle_feedback_hal_pin_changed(self, widget, data=None):
        self.widgets.lbl_spindle_act.set_text("S %s"%int(self.widgets.hal_hbar_spindle_feedback.value))

    def on_btn_spindle_100_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_spindle_100_clicked")
        self.widgets.adj_spindle.set_value(100)

    def on_scl_spindle_value_changed(self, widget, data = None):
        if not self.data.active_spindle_command:
            return
        spindle_override = self.widgets.scl_spindle.get_value() / 100
        real_spindle_speed = float(self.data.active_spindle_command) * spindle_override
        if real_spindle_speed > self.widgets.adj_spindle_bar_max.get_value():
            try:
                value_to_set = widget.get_value()/(real_spindle_speed/self.widgets.adj_spindle_bar_max.get_value())
                widget.set_value(value_to_set)
            except:
                pass
        self.widgets.lbl_spindle_act.set_text("S %d" %real_spindle_speed)
        self.emc.spindle_override(spindle_override)

    def on_adj_start_spindle_RPM_value_changed(self, widget, data = None):
        if self.log: self.gscreen.add_alarm_entry("sbtn_spindle_start_rpm_clicked")
        self.data.spindle_start_rpm = widget.get_value()
        self.gscreen.prefs.putpref("spindle_start_rpm", widget.get_value(),float)

    def on_adj_spindle_bar_min_value_changed(self, widget, data = None):
        if self.log: self.gscreen.add_alarm_entry("Spindle bar min has been set to %s"%widget.get_value())
        self.gscreen.prefs.putpref("spindle_bar_min", widget.get_value(),float)
        self.widgets.adj_spindle_bar_min.set_value(widget.get_value())
        self.widgets.hal_hbar_spindle_feedback.set_property("min",widget.get_value())

    def on_adj_spindle_bar_max_value_changed(self, widget, data = None):
        if self.log: self.gscreen.add_alarm_entry("Spindle bar max has been set to %s"%widget.get_value())
        self.gscreen.prefs.putpref("spindle_bar_max", widget.get_value(),float)
        self.widgets.adj_spindle_bar_max.set_value(widget.get_value())
        self.widgets.hal_hbar_spindle_feedback.set_property("max",widget.get_value())

    def _update_spindle_btn(self):
        if self.emcstat.task_mode == _AUTO and self.interpreter == _RUN:
            return
        if not self.data.spindle_speed:
            self.widgets.rbt_stop.set_active(True)
            return
        if self.data.spindle_dir > 0:
            self.widgets.rbt_forward.set_active(True)
        elif self.data.spindle_dir < 0:
            self.widgets.rbt_reverse.set_active(True)
        elif not self.widgets.rbt_stop.get_active():
            self.widgets.rbt_stop.set_active(True)

    # Coolant an mist coolant button
    def on_tbtn_flood_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("tbtn_flood_clicked")
        if self.data.flood and self.widgets.tbtn_flood.get_active():
            return
        elif not self.data.flood and not self.widgets.tbtn_flood.get_active():
            return
        elif self.widgets.tbtn_flood.get_active():
            self.widgets.tbtn_flood.set_image(self.widgets.img_coolant_on)
            self.emc.flood_on(1)
        else:
            self.widgets.tbtn_flood.set_image(self.widgets.img_coolant_off)
            self.emc.flood_off(1)

    def on_tbtn_mist_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("tbtn_mist_clicked")
        if self.data.mist and self.widgets.tbtn_mist.get_active():
            return
        elif not self.data.mist and not self.widgets.tbtn_mist.get_active():
            return
        elif self.widgets.tbtn_mist.get_active():
            self.widgets.tbtn_mist.set_image(self.widgets.img_mist_on)
            self.emc.mist_on(1)
        else:
            self.widgets.tbtn_mist.set_image(self.widgets.img_mist_off)
            self.emc.mist_off(1)

    def _update_coolant(self):
        if self.data.flood:
            if not self.widgets.tbtn_flood.get_active():
                self.widgets.tbtn_flood.set_active(True)
                self.widgets.tbtn_flood.set_image(self.widgets.img_coolant_on)
        else:
            if self.widgets.tbtn_flood.get_active():
                self.widgets.tbtn_flood.set_active(False)
                self.widgets.tbtn_flood.set_image(self.widgets.img_coolant_off)
        if self.data.mist:
            if not self.widgets.tbtn_mist.get_active():
                self.widgets.tbtn_mist.set_active(True)
                self.widgets.tbtn_mist.set_image(self.widgets.img_mist_on)
        else:
            if self.widgets.tbtn_mist.get_active():
                self.widgets.tbtn_mist.set_active(False)
                self.widgets.tbtn_mist.set_image(self.widgets.img_mist_off)

    # feed stuff
    def on_scl_feed_value_changed(self, widget, data=None):
        self.emc.feed_override(self.widgets.scl_feed.get_value() / 100)

    def on_btn_feed_100_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_feed_100_clicked")
        self.widgets.adj_feed.set_value(100)

    # Update the velocity labels
    def _update_vel(self):
        self.widgets.lbl_active_feed.set_label(self.data.active_feed_command)
        feed_override = self.widgets.scl_feed.get_value() / 100
        real_feed = float(self.widgets.lbl_active_feed.get_text()) * feed_override
        if self.data.dro_units == _MM:
            self.widgets.lbl_current_vel.set_text("%d" %self.data.velocity)
            if "G95" in self.data.active_gcodes:
                self.widgets.lbl_feed_act.set_text("F  %.2f" %real_feed)
            else:
                self.widgets.lbl_feed_act.set_text("F  %d" %real_feed)
        else:
            self.widgets.lbl_current_vel.set_text("%.2f" %self.data.velocity)
            if "G95" in self.data.active_gcodes:
                self.widgets.lbl_feed_act.set_text("F  %.4f" %real_feed)
            else:
                self.widgets.lbl_feed_act.set_text("F  %.2f" %real_feed)

    # This is the jogging part
    def on_increment_changed(self, widget = None, data = None):
        if self.log: self.gscreen.add_alarm_entry("increment_changed %s"%data)
        if data == 0:
            self.distance = 0
        else:
            self.distance = self.gscreen.parse_increment(data)
        self.gscreen.halcomp["jog-increment"] = self.distance

    def on_adj_jog_vel_value_changed(self, widget, data = None):
        if widget.get_value() > self.widgets.adj_max_vel.get_value():
            widget.set_value(self.widgets.adj_max_vel.get_value())
        self.emc.continuous_jog_velocity(widget.get_value())

    def on_adj_max_vel_value_changed(self, widget, data = None):
        if widget.get_value() < self.widgets.adj_jog_vel.get_value():
            self.widgets.adj_jog_vel.set_value(widget.get_value())
        self.emc.max_velocity(widget.get_value() / 60)

    def on_btn_jog_pressed(self, widget, data = None):
        # only in manual mode we will allow jogging the axis at this development state
        if not self.emcstat.task_mode == _MANUAL:
            return

        # if data = True, then the user pressed SHIFT for Jogging and 
        # want's to jog at full speed
        if data:
            self.emc.continuous_jog_velocity(self.gscreen.data.jog_rate_max)
        else:
            self.emc.continuous_jog_velocity(self.widgets.adj_jog_vel.get_value())

        axisletter = widget.get_label()[0]
        if not axisletter.lower() in "xyzabcuvw":
            print ("unknown axis %s"%axisletter)
            return

        dir = widget.get_label()[1]
        if dir == "+":
            direction = 1
        else:
            direction = -1

        axis = "xyzabcuvw".index(axisletter.lower())

        self.gscreen.add_alarm_entry("btn_jog_%i_%i"%(axis,direction))

        if self.distance <> 0:  # incremental jogging
            # DOKU : emc.incremental_jog(<axis number>,<direction>,<distance to jog>)
            self.emc.incremental_jog(axis, direction, self.distance)
        else:                   # continuous jogging
            self.emc.continuous_jog(axis,direction)

    def on_btn_jog_released(self, widget, data = None):
        axisletter = widget.get_label()[0]
        if not axisletter.lower() in "xyzabcuvw":
            print ("unknown axis %s"%axisletter)
            return

        axis = "xyzabcuvw".index(axisletter.lower())

        if self.distance <>0:
            pass
        else:
            self.emc.continuous_jog(axis,0)

    # this are the MDI thinks we need
    def on_btn_delete_clicked(self, widget, data=None):
        message = _("Do you really want to delete the MDI history?\n")
        message += _("this will not delete the MDI History file, but will\n")
        message += _("delete the listbox entries for this session")
        result = self.yesno_dialog(header=_("Attention!!"), label = message)
        if result:
            self.widgets.hal_mdihistory.model.clear()
        if self.log: self.gscreen.add_alarm_entry("delete_MDI with result %s"%result)

    def yesno_dialog(self, header=_("Question") ,label=_("Please decide:")):
        dialog = gtk.MessageDialog(self.widgets.window1,
                 gtk.DIALOG_DESTROY_WITH_PARENT,
                 gtk.MESSAGE_QUESTION, gtk.BUTTONS_YES_NO,header)
        label = gtk.Label(label)
        dialog.vbox.pack_start(label)
        dialog.set_decorated(True)
        dialog.show_all()
        response = dialog.run()
        dialog.destroy()
        if response == gtk.RESPONSE_YES:
            return True
        return False

    def on_tbtn_use_screen2_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("show window 2 set to %s"%widget.get_active())
        self.gscreen.prefs.putpref("use_screen2", widget.get_active(), bool)
        if widget.get_active():
            self.widgets.window2.show()
            if self.widgets.rbtn_window.get_active():
                try:
                    pos = self.widgets.window1.get_position()
                    size = self.widgets.window1.get_size()
                    left = pos[0] +size[0]
                    self.widgets.window2.move(left,pos[1])
                except:
                    pass
        else:
            self.widgets.window2.hide()

    def on_btn_show_kbd_clicked(self, widget, data=None):
        # if the image is img_brake macro, we want to interupt the running macro
        if self.widgets.btn_show_kbd.get_image() == self.widgets.img_brake_macro:
            if self.log: self.gscreen.add_alarm_entry("btn_brake macro_clicked")
            self.emc.abort()
            for btn in self.macrobuttons:
                btn.set_sensitive(True)
            self.widgets.btn_show_kbd.set_image(self.widgets.img_keyboard)
            self.widgets.btn_show_kbd.set_property("tooltip-text",_("This button will show or hide the keyboard"))
        elif self.widgets.ntb_info.get_current_page() == 1:
            if self.log: self.gscreen.add_alarm_entry("btn_keyboard_clicked")
            self.widgets.ntb_info.set_current_page(0)
        else:
            self.widgets.ntb_info.set_current_page(1)

    def on_ntb_info_switch_page(self, widget, page, page_num, data=None):
        if self.emc.get_mode() == _MDI:
            self.widgets.hal_mdihistory.entry.grab_focus()
        elif self.emc.get_mode() == _AUTO:
            self.widgets.gcode_view.grab_focus()

    # Three back buttons to be able to leave notebook pages
    # All use the same callback offset
    def on_btn_back_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_back_clicked")
        if self.widgets.ntb_button.get_current_page() == 6: # edit mode, go back to auto_buttons
            self.widgets.ntb_button.set_current_page(2)
        elif self.widgets.ntb_button.get_current_page() == 8: # File selection mode
            self.widgets.ntb_button.set_current_page(2)
        else:                                               # else we go to main button on manual
            self.widgets.ntb_button.set_current_page(0)
            self.widgets.ntb_main.set_current_page(0)
            self.widgets.ntb_preview.set_current_page(0)

    def on_btn_clear_statusbar_clicked(self, widget, data=None):
        self.widgets.statusbar1.pop(self.gscreen.statusbar_id)

    # The offset settings, set to zero
    def on_btn_touch_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_touch_clicked")
        self.widgets.ntb_button.set_current_page(4)
        self._show_offset_tab(True)
        if self.widgets.rbtn_show_preview.get_active():
            self.widgets.ntb_preview.set_current_page(0)

    def on_tbtn_edit_offsets_toggled(self, widget, data=None):
        state = widget.get_active()
        self.widgets.offsetpage1.edit_button.set_active(state)
        widgetlist = ["btn_zero_x","btn_zero_y","btn_zero_z","btn_set_value_x","btn_set_value_y",
                      "btn_set_value_z","btn_set_selected","ntb_jog","btn_zero_g92"
                     ]
        self.gscreen.sensitize_widgets(widgetlist,not state)
        
        if not state: # we must switch back to manual mode, otherwise jogging is not possible
            self.emc.set_manual_mode()
        
        # show virtual keyboard
        if state:
            self.widgets.ntb_info.set_current_page(1)
            self.widgets.ntb_preview.set_current_page(1)
        else:
            self.widgets.ntb_info.set_current_page(0)
            self.widgets.ntb_preview.set_current_page(0)

    def on_btn_zero_g92_clicked(self, widget, data=None):
        self.widgets.offsetpage1.zero_g92(self)

    def _show_offset_tab(self,state):
        page = self.widgets.ntb_preview.get_nth_page(1)
        print("offset",state,page.get_visible())
        if page.get_visible()and state or not page.get_visible()and not state:
            return
        if state:
            page.show()
            self.widgets.ntb_preview.set_property("show-tabs",state)
            self.widgets.ntb_preview.set_current_page(1)
            self.widgets.offsetpage1.mark_active((self.system_list[self.data.system]).lower())
            if self.widgets.chk_use_kb_on_offset.get_active():
                self.widgets.ntb_info.set_current_page(1)
        else:
            names = self.widgets.offsetpage1.get_names()
            for system,name in names:
                system_name = "system_name_%s"%system
                self.gscreen.prefs.putpref(system_name, name, str)
            page.hide()
            self.widgets.tbtn_edit_offsets.set_active(False)
            self.widgets.ntb_preview.set_current_page(0)
            self.widgets.ntb_info.set_current_page(0)
            if self.widgets.ntb_preview.get_n_pages() <= 4: # else user tabs are availible
                self.widgets.ntb_preview.set_property("show-tabs",state)

# TODO: what to do when there are more axis?
    def on_btn_zero_x_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_zero_X_clicked")
        self.emc.set_mdi_mode()
        self.gscreen.mdi_control.set_axis("X",0)
        self.widgets.hal_action_reload.emit("activate")
        self.emc.set_manual_mode()

    def on_btn_zero_y_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_zero_Y_clicked")
        self.emc.set_mdi_mode()
        self.gscreen.mdi_control.set_axis("Y",0)
        self.widgets.hal_action_reload.emit("activate")
        self.emc.set_manual_mode()

    def on_btn_zero_z_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_zero_Z_clicked")
        self.emc.set_mdi_mode()
        self.gscreen.mdi_control.set_axis("Z",0)
        self.widgets.hal_action_reload.emit("activate")
        self.emc.set_manual_mode()

    def on_btn_set_value_clicked(self, widget, data=None):
        if widget == self.widgets.btn_set_value_x:
            axis = "x"
        elif widget == self.widgets.btn_set_value_y:
            axis = "y"
        elif widget == self.widgets.btn_set_value_z:
            axis = "z"
        else:
            axis = "Unknown"
            self.gscreen.add_alarm_entry(_("Offset %s could not be set, because off unknown axis")%axis)
            return
        self.gscreen.add_alarm_entry("btn_set_value_%s_clicked"%axis)
        preset = self.gscreen.prefs.getpref("offset_axis_%s"%axis, 0, float)
        offset = self.entry_dialog(data = preset, header = _("Enter value for axis %s")%axis, 
                                   label=_("Set axis %s to:")%axis, integer = False )
        if offset == "CANCEL" or offset == "ERROR":
            return
        if offset != False or offset == 0:
            self.gscreen.add_alarm_entry(_("offset {0} set to {1:f}").format(axis,offset))
            self.emc.set_mdi_mode()
            self.gscreen.mdi_control.set_axis(axis,offset)
            self.widgets.hal_action_reload.emit("activate")
            self.emc.set_manual_mode()
            self.gscreen.prefs.putpref("offset_axis_%s"%axis, offset, float)
        else:
            print(_("Conversion error in btn_set_value"))
            self.gscreen.add_alarm_entry(_("Offset conversion error because off wrong entry"))
            self.gscreen.warning_dialog(_("Conversion error in btn_set_value!"), True, 
                                        _("Please enter only numerical values\nValues have not been applied"))
# TODO: End

    def on_btn_set_selected_clicked(self, widget, data=None):
        system , name = self.widgets.offsetpage1.get_selected()
        if not system:
            message = _("you did not selected a system to be changed to, so nothing will be changed")
            self.gscreen.warning_dialog(_("Important Warning!"), True, message)
            self.gscreen.add_alarm_entry(message)
            return
        if system == self.system_list[self.data.system]:
            return
        else:
            self.emc.set_mdi_mode()
            self.gscreen.mdi_control.user_command(system)
            self.emc.set_manual_mode()

    def entry_dialog(self, data=None, header=_("Enter value") ,label=_("Enter the value to set"), integer = False):
        dialog = gtk.Dialog(header,
                   self.widgets.window1,
                   gtk.DIALOG_DESTROY_WITH_PARENT,
                   (gtk.STOCK_CANCEL, gtk.RESPONSE_REJECT,
                    gtk.STOCK_OK, gtk.RESPONSE_ACCEPT))
        label = gtk.Label(label)
        label.modify_font(pango.FontDescription("sans 20"))
        calc = gladevcp.Calculator()
        dialog.vbox.pack_start(label)
        dialog.vbox.add(calc)
        if data != None:
            calc.set_value(data)
        else:
            calc.set_value("")
        calc.set_property("font","sans 20")
        calc.set_editable(True)
        calc.entry.connect("activate", lambda w : dialog.emit("response",gtk.RESPONSE_ACCEPT))
        dialog.parse_geometry("400x400")
        dialog.set_decorated(True)
        dialog.show_all()
        if integer: # The user is only allowed to enter integer values, we hide some button
            calc.num_pad_only(True)
            calc.integer_entry_only(True)
        response = dialog.run()
        value = calc.get_value()
        dialog.destroy()
        if response == gtk.RESPONSE_ACCEPT:
            if value!= None:
                print("Value = ",value)
                return float(value)
            else:
                return "ERROR"
        return "CANCEL"

    # choose a theme to aply
    def on_theme_choice_changed(self, widget):
        if self.log: self.gscreen.add_alarm_entry("theme changed to %s"%widget.get_active_text())
        self.gscreen.change_theme(widget.get_active_text())

    def on_rbt_unlock_toggled(self, widget, data = None):
        if widget.get_active():
            if widget == self.widgets.rbt_use_unlock:
                self.gscreen.prefs.putpref("unlock_way", "use", str)
            elif widget == self.widgets.rbt_no_unlock:
                self.gscreen.prefs.putpref("unlock_way", "no", str)
            else:
                self.gscreen.prefs.putpref("unlock_way", "hal", str)

    def on_rbtn_run_from_line_toggled(self, widget, data = None):
        if widget.get_active():
            if widget == self.widgets.rbtn_no_run_from_line:
                self.gscreen.prefs.putpref("run_from_line", "no_run", str)
                self.widgets.btn_from_line.set_sensitive(False)
            else: #widget == self.widgets.rbtn_run_from_line:
                self.gscreen.prefs.putpref("run_from_line", "run", str)
                self.widgets.btn_from_line.set_sensitive(True)

    def on_chk_use_kb_on_offset_toggled(self, widget, data = None):
        self.gscreen.prefs.putpref("show_keyboard_on_offset", widget.get_active(), bool)

    def on_chk_use_kb_on_tooledit_toggled(self, widget, data = None):
        self.gscreen.prefs.putpref("show_keyboard_on_tooledit", widget.get_active(), bool)

    def on_chk_use_kb_on_edit_toggled(self, widget, data = None):
        self.gscreen.prefs.putpref("show_keyboard_on_edit", widget.get_active(), bool)

    def on_chk_use_kb_on_mdi_toggled(self, widget, data = None):
        self.gscreen.prefs.putpref("show_keyboard_on_mdi", widget.get_active(), bool)

    def on_chk_use_kb_on_file_selection_toggled(self, widget, data = None):
        self.gscreen.prefs.putpref("show_keyboard_on_file_selection", widget.get_active(), bool)

    def on_chk_use_desktop_notify_toggled(self, widget, data = None):
        self.gscreen.prefs.putpref("desktop_notify", widget.get_active(), bool)
        self.data.desktop_notify = widget.get_active()

    def on_chk_use_kb_shortcuts_toggled(self, widget, data = None):
        self.gscreen.prefs.putpref("use_keyboard_shortcuts", widget.get_active(), bool)

    def on_rbtn_show_preview_toggled(self, widget, data = None):
        self.gscreen.prefs.putpref("show_preview_on_offset", widget.get_active(), bool)

    def on_adj_scale_max_vel_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("scale_max_vel", widget.get_value(), float)
        self.scale_max_vel = widget.get_value()

    def on_adj_scale_jog_vel_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("scale_jog_vel", widget.get_value(), float)
        self.scale_jog_vel = widget.get_value()

    def on_adj_scale_feed_override_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("scale_feed_override", widget.get_value(), float)
        self.scale_feed_override = widget.get_value()

    def on_adj_scale_spindle_override_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("scale_spindle_override", widget.get_value(), float)
        self.scale_spindle_override = widget.get_value()

    def on_rbt_use_gmoccapy_toggled(self, widget):
        if widget.get_active():
            self.gscreen.prefs.putpref("error_style", "gmoccapy", str)
            self.widgets.frm_message_position.set_sensitive(True)
            print("gmoccapy_toggled")
            self.init_notification()

    def on_rbt_use_gscreen_toggled(self, widget):
        if widget.get_active():
            self.gscreen.prefs.putpref("error_style", "gscreen", str)
            self.widgets.frm_message_position.set_sensitive(False)
            print("gscreen_toggled")

    def on_rbtn_fullscreen_toggled(self, widget):
        if self.log: self.gscreen.add_alarm_entry("rbtn_fullscreen_toggled to %s"%widget.get_active())
        if widget.get_active():
            self.widgets.window1.fullscreen()
            self.gscreen.prefs.putpref("screen1", "fullscreen", str)
        else:
            self.widgets.window1.unfullscreen()

    def on_rbtn_maximized_toggled(self, widget):
        if self.log: self.gscreen.add_alarm_entry("rbtn_maximized_toggled to %s"%widget.get_active())
        if widget.get_active():
            self.widgets.window1.maximize()
            self.gscreen.prefs.putpref("screen1", "maximized", str)
        else:
            self.widgets.window1.unmaximize()

    def on_rbtn_window_toggled(self, widget):
        if self.log: self.gscreen.add_alarm_entry("rbtn_window_toggled to %s"%widget.get_active())
        self.widgets.spbtn_x_pos.set_sensitive(widget.get_active())
        self.widgets.spbtn_y_pos.set_sensitive(widget.get_active())
        self.widgets.spbtn_width.set_sensitive(widget.get_active())
        self.widgets.spbtn_height.set_sensitive(widget.get_active())
        # we have to check also if the window is active, because the button is toggled the first time
        # before the window is shown
        if widget.get_active() and self.widgets.window1.is_active():
            self.widgets.window1.move(int(self.widgets.adj_x_pos.get_value()),
                                      int(self.widgets.adj_y_pos.get_value()))
            self.widgets.window1.resize(int(self.widgets.adj_width.get_value()),
                                        int(self.widgets.adj_height.get_value()))
            self.gscreen.prefs.putpref("screen1", "window", str)

    def on_adj_x_pos_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("x_pos", widget.get_value(),float)
        position = self.widgets.window1.get_position()
        self.widgets.window1.move(int(widget.get_value()),position[1])

    def on_adj_y_pos_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("y_pos", widget.get_value(),float)
        position = self.widgets.window1.get_position()
        self.widgets.window1.move(position[0],int(widget.get_value()))

    def on_adj_width_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("width", widget.get_value(),float)
        size = self.widgets.window1.get_size()
        self.widgets.window1.resize(int(widget.get_value()),size[1])

    def on_adj_height_value_changed(self, widget, data = None):
        self.gscreen.prefs.putpref("height", widget.get_value(),float)
        size = self.widgets.window1.get_size()
        self.widgets.window1.resize(size[0],int(widget.get_value()))

    def on_chk_hide_cursor_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("hide_cursor_toggled to %s"%widget.get_active())
        self.gscreen.prefs.putpref("hide_cursor", widget.get_active())
        self.data.hide_cursor = widget.get_active()
        if widget.get_active():
            self.widgets.window1.window.set_cursor(INVISABLE)
        else:
            self.widgets.window1.window.set_cursor(None)

    def on_rel_colorbutton_color_set(self, widget):
        color = widget.get_color()
        if self.log: self.gscreen.add_alarm_entry("rel color set to %s"%color)
        self.gscreen.prefs.putpref('rel_color', color, str)
        self._change_dro_color("rel_color", color)

    def on_abs_colorbutton_color_set(self, widget):
        color = widget.get_color()
        if self.log: self.gscreen.add_alarm_entry("abs color set to %s"%color)
        self.gscreen.prefs.putpref('abs_color', widget.get_color(), str)
        self._change_dro_color("abs_color", color)

    def on_dtg_colorbutton_color_set(self, widget):
        color = widget.get_color()
        if self.log: self.gscreen.add_alarm_entry("dtg color set to %s"%color)
        self.gscreen.prefs.putpref('dtg_color', widget.get_color(), str)
        self._change_dro_color("dtg_color", color)

    def _change_dro_color(self, property, color):
        for axis in self.data.axis_list:
            if axis == self.axisletter_four:
                axis = 4
            self.widgets["Combi_DRO_%s"%axis].set_property(property, color)

    def on_file_to_load_chooser_file_set(self, widget):
        if self.log: self.gscreen.add_alarm_entry("file to load on startup set to : %s"%widget.get_filename())
        self.gscreen.prefs.putpref("open_file", widget.get_filename(), str)

    def on_jump_to_dir_chooser_file_set(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("jump to dir has been set to : %s"%widget.get_filename())
        self.gscreen.prefs.putpref("jump_to_dir", widget.get_filename(), str)
        self.widgets.IconFileSelection1.set_property("jump_to_dir", widget.get_filename())

    def on_grid_size_value_changed(self, widget, data=None):
        self.gscreen.set_grid_size(widget)

    def on_tbtn_log_actions_toggled(self, widget, data=None):
        self.log = widget.get_active()
        self.gscreen.prefs.putpref("log_actions", widget.get_active(), bool)

    def on_chk_show_dro_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("show_gremlin_DRO =  %s"%widget.get_active())
        self.widgets.gremlin.set_property("metric_units", self.widgets.Comi_DRO_x.metric_units)
        self.widgets.gremlin.set_property("enable_dro",widget.get_active())
        self.gscreen.prefs.putpref("enable_dro", widget.get_active(), bool)
        self.widgets.chk_show_offsets.set_sensitive(widget.get_active())
        self.widgets.chk_show_dtg.set_sensitive(widget.get_active())

    def on_chk_show_dtg_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("show_gremlin_DTG =  %s"%widget.get_active())
        self.widgets.gremlin.set_property("show_dtg", widget.get_active())
        self.gscreen.prefs.putpref("show_dtg", widget.get_active(), bool)

    def on_chk_show_offsets_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("show_offset_button_toggled to %s"%widget.get_active())
        self.widgets.gremlin.show_offsets = widget.get_active()
        self.gscreen.prefs.putpref("show_offsets", widget.get_active(), bool)

    # tool stuff
    def on_btn_tool_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_tool_clicked")
        self.widgets.ntb_button.set_current_page(7)
        self._show_tooledit_tab(True)

    def _show_tooledit_tab(self,state):
        page = self.widgets.ntb_preview.get_nth_page(2)
        print("tooledit",state,page.get_visible())
        if page.get_visible()and state or not page.get_visible()and not state:
            return
        if state:
            page.show()
            self.widgets.ntb_preview.set_property("show-tabs",not state)
            self.widgets.vbx_jog.hide()
            self.widgets.ntb_preview.set_current_page(2)
            self.widgets.tooledit1.set_selected_tool(self.data.tool_in_spindle)
            if self.widgets.chk_use_kb_on_tooledit.get_active():
                self.widgets.ntb_info.set_current_page(1)
        else:
            page.hide()
            if self.widgets.ntb_preview.get_n_pages() > 4: # user tabs are availible
                self.widgets.ntb_preview.set_property("show-tabs",not state)
            self.widgets.vbx_jog.show()
            self.widgets.ntb_preview.set_current_page(0)
            self.widgets.ntb_info.set_current_page(0)

    def _update_toolinfo(self, tool):
        toolinfo = self.widgets.tooledit1.get_toolinfo(tool)
        if toolinfo:
            # Doku
            # toolinfo[0] = cell toggle
            # toolinfo[1] = tool number
            # toolinfo[2] = pocket number
            # toolinfo[3] = X offset
            # toolinfo[4] = Y offset
            # toolinfo[5] = Z offset
            # toolinfo[6] = A offset
            # toolinfo[7] = B offset
            # toolinfo[8] = C offset
            # toolinfo[9] = U offset
            # toolinfo[10] = V offset
            # toolinfo[11] = W offset
            # toolinfo[12] = tool diameter
            # toolinfo[13] = frontangle
            # toolinfo[14] = backangle
            # toolinfo[15] = tool orientation
            # toolinfo[16] = tool info
            self.widgets.lbl_tool_no.set_text(str(toolinfo[1]))
            self.widgets.lbl_tool_dia.set_text(toolinfo[12])
            self.widgets.lbl_tool_name.set_text(toolinfo[16])
        if tool == 0:
            self.widgets.lbl_tool_no.set_text("0")
            self.widgets.lbl_tool_dia.set_text("0")
            self.widgets.lbl_tool_name.set_text(_("No tool description available"))
        if "G43" in self.data.active_gcodes:
            mode = self.emc.get_mode()
            if mode != _AUTO:
                self.emc.set_mdi_mode()
                self.gscreen.mdi_control.user_command("G43")
            if mode == _MANUAL:
                self.wait_tool_change = True
                self.on_hal_status_interp_idle(self)

    def on_btn_delete_tool_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("on_btn_delete_tool_clicked")
        self.tooledit_btn_delete_tool.emit("clicked")

    def on_btn_add_tool_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("on_btn_add_tool_clicked")
        self.tooledit_btn_add_tool.emit("clicked")

    def on_btn_reload_tooltable_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("on_btn_reload_tooltable_clicked")
        self.tooledit_btn_reload_tool.emit("clicked")

    def on_btn_apply_tool_changes_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("on_btn_apply_tool_changes_clicked")
        self.tooledit_btn_apply_tool.emit("clicked")
        tool = self.widgets.tooledit1.get_selected_tool()

    def on_btn_tool_touchoff_clicked(self, widget, data=None):
        if not self.widgets.tooledit1.get_selected_tool():
            return
        if widget == self.widgets.btn_tool_touchoff_x:
            axis = "x"
        elif widget == self.widgets.btn_tool_touchoff_z:
            axis = "z"
        else:
            self.gscreen.warning_dialog(_("Real big error!"), True, 
                                        _("You managed to come to a place that is not possible in on_btn_tool_touchoff"))
            return

        if "G41" in self.data.active_gcodes or "G42" in self.data.active_gcodes:
            message = _("Tool touch off is not possible with cutter radius compensation switched on!\n")
            message += _("Please emit an G40 before tool touch off")
            print(message)
            self.gscreen.add_alarm_entry(message)
            self.gscreen.warning_dialog(_("Warning Tool Touch off not possible!"), True, message)
            return

        value = self.entry_dialog(data = None, 
                                  header = _("Enter value for axis %s to set:")%axis.upper(),
                                  label=_("Set parameter of tool {0:d} and axis {1} to:").format(self.data.tool_in_spindle,axis.upper()),
                                  integer = False)
        
        if value == "ERROR":
            message = _("Conversion error because of wrong entry for touch off axis %s")%axis.upper()
            print(message)
            self.gscreen.add_alarm_entry(message)
            self.gscreen.warning_dialog(_("Conversion error !"), True, message)
            return
        elif value == "CANCEL":
            self.gscreen.add_alarm_entry(_("entry for axis %s has been canceled")%axis.upper())
            return
        else:
            self.gscreen.add_alarm_entry(_("axis {0} , has been set to {1:f}").format(axis.upper(),value))
        self.gscreen.mdi_control.touchoff(self.widgets.tooledit1.get_selected_tool(),axis,value)
        self.widgets.rbt_manual.emit("pressed")
        self.widgets.rbt_manual.set_active(True)

    # select a tool entering a number
    def on_btn_select_tool_by_no_clicked(self, widget, data=None):
        value = self.entry_dialog(data = None, header = _("Enter the tool number as integer "), 
                                      label=_("Select the tool to change"),integer=True)
        if value == "ERROR":
            message = _("Conversion error because of wrong entry for tool number\n")
            message += _("enter only integer nummbers")
            print(message)
            self.gscreen.add_alarm_entry(message)
            self.gscreen.warning_dialog(_("Conversion error !"), True, message)
            return
        elif value == "CANCEL":
            self.gscreen.add_alarm_entry(_("entry for selection of tool number has been canceled"))
            return
        else:
            if int(value) == self.gscreen.data.tool_in_spindle:
                message = _("Selected tool is already in spindle, no change needed.")
                self.gscreen.warning_dialog(_("Important Warning!"), True, message)
                self.gscreen.add_alarm_entry(message)
                return
            self.wait_tool_change = True
            self.emc.set_mdi_mode()
            command = "T%s M6"%int(value)
            self.gscreen.mdi_control.user_command(command)
            self.wait_tool_change = True

    # set tool with M61 Q? or with T? M6
    def on_btn_selected_tool_clicked(self, widget, data=None):
        tool = self.widgets.tooledit1.get_selected_tool()
        if tool == None:
            message = _("you selected no or more than one tool, the tool selection must be unique")
            self.gscreen.warning_dialog(_("Important Warning!"), True, message)
            self.gscreen.add_alarm_entry(message)
            return
        if tool == self.gscreen.data.tool_in_spindle:
            message = _("Selected tool is already in spindle, no change needed.")
            self.gscreen.warning_dialog(_("Important Warning!"), True, message)
            self.gscreen.add_alarm_entry(message)
            return
        if tool or tool == 0:
            tool = int(tool)
            self.wait_tool_change = True
            self.emc.set_mdi_mode()
            
            if widget == self.widgets.btn_change_tool:
                command = "T%s M6"%tool
            else:
                command = "M61 Q%s"%tool
            self.gscreen.mdi_control.user_command(command)
            if self.log: self.gscreen.add_alarm_entry("set_tool_with %s"%command)
        else:
            message = _("Could not understand the entered tool number. Will not change anything")
            self.gscreen.warning_dialog(_("Important Warning!"), True, message)
            self.widgets.statusbar1.push(1,message)

    # gremlin relevant calls
    def on_rbt_view_p_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_view_p_toggled")
        if self.widgets.rbt_view_p.get_active():
            self.widgets.gremlin.set_property("view","p")
        self.gscreen.prefs.putpref("gremlin_view", "rbt_view_p", str)

    def on_rbt_view_x_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_view_x_toggled")
        if self.widgets.rbt_view_x.get_active():
            self.widgets.gremlin.set_property("view","x")
        self.gscreen.prefs.putpref("gremlin_view", "rbt_view_x", str)

    def on_rbt_view_y_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_view_y_toggled")
        if self.widgets.rbt_view_y.get_active():
            self.widgets.gremlin.set_property("view","y")
        self.gscreen.prefs.putpref("gremlin_view", "rbt_view_y", str)

    def on_rbt_view_z_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_view_z_toggled")
        if self.widgets.rbt_view_z.get_active():
            self.widgets.gremlin.set_property("view","z")
        self.gscreen.prefs.putpref("gremlin_view", "rbt_view_z", str)

    def on_rbt_view_y2_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("rbt_view_y2_toggled")
        if self.widgets.rbt_view_y2.get_active():
            self.widgets.gremlin.set_property("view","y2")
        self.gscreen.prefs.putpref("gremlin_view", "rbt_view_y2", str)

    def on_btn_zoom_in_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_zoom_in_clicked")
        self.widgets.gremlin.zoom_in()

    def on_btn_zoom_out_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_zoom_out_clicked")
        self.widgets.gremlin.zoom_out()

    def on_btn_delete_view_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_delete_view_clicked")
        self.widgets.gremlin.clear_live_plotter()

    def on_tbtn_view_dimension_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("tbtn_view_dimensions_toggled")
        self.widgets.gremlin.set_property("show_extents_option", widget.get_active())
        self.gscreen.prefs.putpref("view_dimension", self.widgets.tbtn_view_dimension.get_active(), bool)

    def on_tbtn_view_tool_path_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_view_tool_path_clicked")
        self.widgets.gremlin.set_property("show_live_plot", widget.get_active())
        self.gscreen.prefs.putpref("view_tool_path", self.widgets.tbtn_view_tool_path.get_active(), bool)

    def _show_iconview_tab(self,state):
        page = self.widgets.ntb_preview.get_nth_page(3)
        if page.get_visible()and state or not page.get_visible()and not state:
            return
        if state:
            page.show()
            self.widgets.ntb_preview.set_property("show-tabs", not state)
            self.widgets.ntb_preview.set_current_page(3)
            if self.widgets.chk_use_kb_on_file_selection.get_active():
                self.widgets.box_info.show()
                self.widgets.ntb_info.set_current_page(1)
        else:
            page.hide()
            if self.widgets.ntb_preview.get_n_pages() > 4: # user tabs are availible
                self.widgets.ntb_preview.set_property("show-tabs",not state)
            self.widgets.ntb_preview.set_current_page(0)
            self.widgets.ntb_info.set_current_page(0)

    def on_btn_load_clicked(self, widget, data=None):
        self.widgets.ntb_button.set_current_page(8)
        self.widgets.ntb_preview.set_current_page(3)
        self.widgets.tbtn_fullsize_preview.set_active(True)
        self._show_iconview_tab(True)
        self.widgets.IconFileSelection1.iconView.grab_focus()

    def on_btn_sel_next_clicked(self, widget, data=None):
        self.widgets.IconFileSelection1.btn_sel_next.emit("clicked")

    def on_btn_sel_prev_clicked(self, widget, data=None):
        self.widgets.IconFileSelection1.btn_sel_prev.emit("clicked")

    def on_btn_home_clicked(self, widget, data=None):
        self.widgets.IconFileSelection1.btn_home.emit("clicked")

    def on_btn_jump_to_clicked(self, widget, data=None):
        self.widgets.IconFileSelection1.btn_jump_to.emit("clicked")

    def on_btn_dir_up_clicked(self, widget, data=None):
        self.widgets.IconFileSelection1.btn_dir_up.emit("clicked")

    def on_btn_select_clicked(self, widget, data=None):
        self.widgets.IconFileSelection1.btn_select.emit("clicked")

    def on_IconFileSelection1_selected(self,widget,path=None):
        print(path)
        if path:
            try:
                self.widgets.hal_action_open.load_file(path)
                self.widgets.ntb_preview.set_current_page(0)
                self.widgets.tbtn_fullsize_preview.set_active(False)
                self.widgets.ntb_button.set_current_page(2)
            except:
                print(_("error trying opening file %s"%path))
            self._show_iconview_tab(False)

    def on_IconFileSelection1_exit(self,widget):
        self.widgets.ntb_preview.set_current_page(0)
        self.widgets.tbtn_fullsize_preview.set_active(False)
        self._show_iconview_tab(False)

    # edit a program or make a new one
    def on_btn_edit_clicked(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("btn_edit_clicked")
        self.widgets.ntb_button.set_current_page(6)
        self.widgets.ntb_preview.hide()
        self.widgets.hbx_dro.hide()
        w1 , h1 = self.widgets.window1.get_size_request()
        w2 , h2 = self.widgets.vbtb_main.get_size_request()
        self.widgets.vbx_jog.set_size_request(w1 - w2 , -1)
        self.widgets.gcode_view.set_sensitive(True)
        self.widgets.gcode_view.grab_focus()
        if self.widgets.chk_use_kb_on_edit.get_active():
            self.widgets.ntb_info.set_current_page(1)
        else:
            self.widgets.ntb_info.set_current_page(0)
        self.widgets.ntb_message.set_current_page(1)

    # search forward while in edit mode
    def on_btn_search_forward_clicked(self, widget, data=None):
        self.widgets.gcode_view.text_search(direction=True,text=self.widgets.search_entry.get_text())

    # search backward while in edit mode
    def on_btn_search_back_clicked(self, widget, data=None):
        self.widgets.gcode_view.text_search(direction=False,text=self.widgets.search_entry.get_text())

    # undo changes while in edit mode
    def on_btn_undo_clicked(self, widget, data=None):
        self.widgets.gcode_view.undo()

    # redo changes while in edit mode
    def on_btn_redo_clicked(self, widget, data=None):
        self.widgets.gcode_view.redo()

    # if we leave the edit mode, we will have to show all widgets again
    def on_ntb_button_switch_page(self, *args):
        message = "ntb_button_page changed to %s"%self.widgets.ntb_button.get_current_page()
        if self.log: self.gscreen.add_alarm_entry(message)

        if self.widgets.ntb_preview.get_current_page() == 0: # preview tab is active, 
            # check if offset tab is visible, if so we have to hide it
            page = self.widgets.ntb_preview.get_nth_page(1)
            if page.get_visible():
                self._show_offset_tab(False)
        elif self.widgets.ntb_preview.get_current_page() == 1:
            self._show_offset_tab(False)
        elif self.widgets.ntb_preview.get_current_page() == 2:
            self._show_tooledit_tab(False)
        elif self.widgets.ntb_preview.get_current_page() == 3:
            self._show_iconview_tab(False)

        if self.widgets.tbtn_fullsize_preview.get_active():
            self.widgets.tbtn_fullsize_preview.set_active(False)
        if self.widgets.ntb_button.get_current_page() == 6 or self.widgets.ntb_preview.get_current_page() == 3:
            self.widgets.ntb_preview.show()
            self.widgets.hbx_dro.show()
            self.widgets.vbx_jog.set_size_request(360 , -1)
            self.widgets.gcode_view.set_sensitive(0)
            self.widgets.btn_save.set_sensitive(True)
            self.widgets.hal_action_reload.emit("activate")
            self.widgets.ntb_info.set_current_page(0)
            self.widgets.ntb_message.set_current_page(0)

    # Save all changes and run the program
    def on_btn_save_and_run_clicked(self, widget, data=None):
        if self.widgets.lbl_program.get_label() == "":
            self.widgets.btn_save_as.emit("clicked")
        else:
            self.widgets.btn_save.emit("clicked")
        self.widgets.hal_action_reload.emit("activate")
        self.widgets.ntb_button.set_current_page(2)
        self.widgets.btn_run.emit("clicked")

    # make a new file
    def on_btn_new_clicked(self, widget, data=None):
        tempfilename = os.path.join(_TEMPDIR,"temp.ngc")
        content = self.gscreen.inifile.find("RS274NGC", "RS274NGC_STARTUP_CODE")
        content += "\n\n\n\nM2"
        gcodefile = open(tempfilename,"w")
        gcodefile.write(content)
        gcodefile.close()
        self.emc.emccommand.program_open(tempfilename)
        self.widgets.gcode_view.grab_focus()
        self.widgets.btn_save.set_sensitive(False)
        self.widgets.lbl_program.set_label("")

    def on_tbtn_optional_blocks_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("on_tbtn_optional_blocks_toggled to %s"%widget.get_active())
        self.emc.blockdel(widget.get_active())
        self.gscreen.prefs.putpref("blockdel", widget.get_active())
        self.widgets.hal_action_reload.emit("activate")

    def on_tbtn_optional_stops_toggled(self, widget, data=None):
        if self.log: self.gscreen.add_alarm_entry("on_tbtn_optional_stops_toggled to %s"%widget.get_active())
        self.emc.opstop(not widget.get_active())
        self.gscreen.prefs.putpref("opstop", not widget.get_active())

    # use the hal_status widget to control buttons and 
    # actions allowed by the user and sensitive widgets
    def on_hal_status_all_homed(self,widget):
        self.data.all_homed = True
        self.gscreen.add_alarm_entry("all_homed")
        self.widgets.statusbar1.push(1,"")
        self.widgets.ntb_button.set_current_page(0)
        widgetlist = ["rbt_mdi", "rbt_auto", "btn_index_tool", "btn_change_tool","btn_select_tool_by_no", 
                      "btn_tool_touchoff_x", "btn_tool_touchoff_z", "btn_touch"
                     ]
        self.gscreen.sensitize_widgets(widgetlist,True)
        
    def on_hal_status_not_all_homed(self,*args):
        self.data.all_homed = False
        self.gscreen.add_alarm_entry("not_all_homed")
        widgetlist = ["rbt_mdi", "rbt_auto", "btn_index_tool", "btn_touch", "btn_change_tool","btn_select_tool_by_no", 
                      "btn_tool_touchoff_x", "btn_tool_touchoff_z", "btn_touch"
                     ]
        self.gscreen.sensitize_widgets(widgetlist,False)
        
    def on_hal_status_homed(self,widget,data):
        if self.log:self.gscreen.add_alarm_entry(_("Axis %s are homed")%"XYZABCUVW"[int(data[0])])

    def on_hal_status_file_loaded(self, widget, filename):
        self.gscreen.add_alarm_entry("file_loaded_%s"%filename)
        if len(filename) > 50:
            filename = filename[0:10] + "..." + filename[len(filename)-39:len(filename)]
        self.widgets.lbl_program.set_text(filename)
        self.widgets.btn_use_current.set_sensitive(True)

    def on_hal_status_interp_idle(self,widget):
        self.gscreen.add_alarm_entry("idle")
        widgetlist = ["rbt_manual", "btn_step", "ntb_jog", "btn_from_line",
                      "tbtn_flood", "tbtn_mist", "rbt_forward", "rbt_reverse", "rbt_stop", 
                      "btn_load", "btn_edit","tbtn_optional_blocks"
                     ]
        if not self.widgets.rbt_hal_unlock.get_active():
            widgetlist.append("tbtn_setup")
        if self.data.all_homed or self.no_force_homing:
            widgetlist.append("rbt_mdi")
            widgetlist.append("rbt_auto")
            widgetlist.append("btn_index_tool")
            widgetlist.append("btn_change_tool")
            widgetlist.append("btn_select_tool_by_no")
            widgetlist.append("btn_tool_touchoff_x")
            widgetlist.append("btn_tool_touchoff_z")
            widgetlist.append("btn_touch")
        self.gscreen.sensitize_widgets(widgetlist,True)
        for btn in self.macrobuttons:
            btn.set_sensitive(True)
        self.widgets.btn_show_kbd.set_image(self.widgets.img_keyboard)
        self.widgets.btn_run.set_sensitive(True)
        if self.wait_tool_change == True:
            self.widgets.rbt_manual.emit("pressed")
            self.widgets.rbt_manual.set_active(True)
            self.wait_tool_change = False
        self.interpreter = _IDLE
        self.data.restart_dialog = None

    # this can not be done with the status widget, 
    # because it will not emit a RESUME signal
    def on_tbtn_pause_toggled(self, widget, data=None):
        self.gscreen.add_alarm_entry("pause_toggled")
        widgetlist = ["btn_step", "rbt_forward", "rbt_reverse", "rbt_stop"]
        self.gscreen.sensitize_widgets(widgetlist,widget.get_active())

    def on_btn_stop_clicked(self, widget, data=None):
        self.gscreen.update_restart_line(0,0)

    def on_hal_status_interp_run(self,widget):
        self.gscreen.add_alarm_entry("run")
        widgetlist = ["rbt_manual", "rbt_mdi", "rbt_auto", "tbtn_setup", "btn_step","btn_index_tool",
                      "btn_from_line", "btn_change_tool","btn_select_tool_by_no",
                      "btn_load", "btn_edit", "tbtn_optional_blocks", 
                      "btn_tool_touchoff_x", "btn_tool_touchoff_z", "btn_touch"
                     ]
        # in MDI it should be possible to add more commands, even if the interpreter is running
        if self.emcstat.task_mode <> _MDI:
            widgetlist.append("ntb_jog")
        
        self.gscreen.sensitize_widgets(widgetlist,False)
        self.widgets.btn_run.set_sensitive(False)
        self.interpreter = _RUN
        if self.data.restart_dialog:
            self.data.restart_dialog.destroy()
            self.data.restart_dialog = None

    def on_btn_from_line_clicked(self, widget, data=None):
        self.gscreen.add_alarm_entry("Restart the program from line clicked")
        self.gscreen.launch_restart_dialog(self)

    def on_hal_status_tool_in_spindle_changed(self, object, new_tool_no):
        self.gscreen.add_alarm_entry(_("tool_in_spindle has changed to %s"%new_tool_no))
        print("hal_status tool changed emitted")
        self._update_toolinfo(new_tool_no)

    def on_hal_status_state_estop(self,widget=None):
        self.gscreen.add_alarm_entry("estop")
        self.widgets.tbtn_estop.set_active(True)
        self.widgets.tbtn_on.set_active(False)
        self.widgets.tbtn_on.set_sensitive(False)

    def on_hal_status_state_estop_reset(self,widget=None):
        self.gscreen.add_alarm_entry("estop_reset")
        self.widgets.tbtn_estop.set_active(False)
        self.widgets.tbtn_on.set_sensitive(True)

    def on_hal_status_state_off(self,widget):
        self.gscreen.add_alarm_entry("state_off")
        widgetlist = ["rbt_manual", "rbt_mdi", "rbt_auto", "btn_homing", "btn_touch", "btn_tool",
                      "ntb_jog", "scl_feed", "btn_feed_100", "rbt_forward", "btn_index_tool",
                      "rbt_reverse", "rbt_stop", "tbtn_flood", "tbtn_mist", "btn_change_tool","btn_select_tool_by_no",
                      "btn_spindle_100", "scl_max_vel", "scl_spindle", 
                      "btn_tool_touchoff_x", "btn_tool_touchoff_z"
                     ]
        self.gscreen.sensitize_widgets(widgetlist,False)
        self.widgets.rbt_manual.set_active(True)
        if self.widgets.tbtn_on.get_active():
            self.widgets.tbtn_on.set_active(False)

    def on_hal_status_state_on(self,widget):
        self.gscreen.add_alarm_entry("state_on")
        widgetlist = ["rbt_manual", "btn_homing", "btn_touch", "btn_tool",
                      "ntb_jog", "scl_feed", "btn_feed_100", "rbt_forward",
                      "rbt_reverse", "rbt_stop", "tbtn_flood", "tbtn_mist",
                      "btn_spindle_100", "scl_max_vel", "scl_spindle"
                     ]
        self.gscreen.sensitize_widgets(widgetlist,True)
        self.widgets.rbt_manual.set_active(True)
        if not self.widgets.tbtn_on.get_active():
            self.widgets.tbtn_on.set_active(True)

    def on_hal_status_mode_manual(self,widget):
        print("manual mode")
        if self.widgets.rbt_manual.get_active():
            return
        else:
            self.widgets.rbt_manual.emit("pressed")
            self.widgets.rbt_manual.set_active(True)

    def on_hal_status_mode_mdi(self,widget):
        print("mdi mode")
        if self.widgets.rbt_mdi.get_active():
            return
        else:
            self.widgets.rbt_mdi.emit("pressed")
            self.widgets.rbt_mdi.set_active(True)

    def on_hal_status_mode_auto(self,widget):
        print("auto mode")
        if self.widgets.rbt_auto.get_active():
            return
        else:
            self.widgets.rbt_auto.emit("pressed")
            self.widgets.rbt_auto.set_active(True)

    def change_sound(self,widget,sound):
        file = widget.get_filename()
        if file:
            self.data[sound+"_sound"] = file
            temp = "audio_"+ sound
            self.gscreen.prefs.putpref(temp, file, str)

    # This connects signals without using glade"s autoconnect method
    # in this case to destroy the window
    # it calls the method in gscreen: gscreen.on_window_destroy()
    # and run-at-line dialog
    def connect_signals(self,handlers):
        signal_list = [ ["window1","destroy", "on_window1_destroy"],
                        ["audio_error_chooser","selection_changed","change_sound","error"],
                        ["audio_alert_chooser","selection_changed","change_sound","alert"],
                      ]
        for i in signal_list:
            if len(i) == 3:
                self.gscreen.widgets[i[0]].connect(i[1], self.gscreen[i[2]])
            elif len(i) == 4:
                self.gscreen.widgets[i[0]].connect(i[1], self.gscreen[i[2]],i[3])

    # every 100 milli seconds this gets called
    # add pass so gscreen doesn't try to update it"s regular widgets or
    # add the individual function names that you would like to call.
    # In this case we wish to call Gscreen's default function for units button update
    # check linuxcnc for status, error and then update the readout
    def timer_interrupt(self):
        self.emc.mask()
        self.emcstat.poll()
        self.gscreen.status.periodic()
        self.data.system = self.gscreen.status.get_current_system()
        e = self.emcerror.poll()
        if e:
            kind, text = e
            print kind,text
            if "joint" in text:
                for letter in self.data.axis_list:
                    axnum = "xyzabcuvws".index(letter)
                    text = text.replace( "joint %d"%axnum,"Axis %s"%letter.upper() )
            if kind in (linuxcnc.NML_ERROR, linuxcnc.OPERATOR_ERROR):
                icon = ALERT_ICON
                type = _("Error Message")
            elif kind in (linuxcnc.NML_TEXT, linuxcnc.OPERATOR_TEXT):
                icon = INFO_ICON
                type = _("Message")
            elif kind in (linuxcnc.NML_DISPLAY, linuxcnc.OPERATOR_DISPLAY):
                icon = INFO_ICON
                type = _("Message")
            if self.widgets.rbt_use_gmoccapy.get_active():
                self.notification.add_message(text,icon)
            else:
                if self.data.desktop_notify: # otherwise it will apear twice, 
                                             # because notify add the message also to the statusbar
                    self.gscreen.notify(type,text,icon,3)
                else:
                    self.widgets.statusbar1.push(1, str(kind) + " " + text)

            if self.data.audio_available:
                if kind != 13:
                    self.gscreen.audio.set_sound(self.data.error_sound)
                else:
                    self.gscreen.audio.set_sound(self.data.alert_sound)
                self.gscreen.audio.run()

        self.emc.unmask()

        self.gscreen.update_active_gcodes()
        self.gscreen.update_active_mcodes()
        if "G8" in self.data.active_gcodes and self.data.lathe_mode and self.data.diameter_mode:
            homed_color = self.widgets.Combi_DRO_y.get_property("homed_color")
            unhomed_color = self.widgets.Combi_DRO_y.get_property("unhomed_color")
            self.widgets.Combi_DRO_y.set_property("abs_color", gtk.gdk.color_parse("#F2F1F0"))
            self.widgets.Combi_DRO_y.set_property("rel_color", gtk.gdk.color_parse("#F2F1F0"))
            self.widgets.Combi_DRO_y.set_property("dtg_color", gtk.gdk.color_parse("#F2F1F0"))
            self.widgets.Combi_DRO_x.set_property("abs_color", gtk.gdk.color_parse(self.data.abs_color))
            self.widgets.Combi_DRO_x.set_property("rel_color", gtk.gdk.color_parse(self.data.rel_color))
            self.widgets.Combi_DRO_x.set_property("dtg_color", gtk.gdk.color_parse(self.data.dtg_color))
            self.data.diameter_mode = False
        elif "G7" in self.data.active_gcodes and self.data.lathe_mode and not self.data.diameter_mode:
            self.widgets.Combi_DRO_x.set_property("abs_color", gtk.gdk.color_parse("#F2F1F0"))
            self.widgets.Combi_DRO_x.set_property("rel_color", gtk.gdk.color_parse("#F2F1F0"))
            self.widgets.Combi_DRO_x.set_property("dtg_color", gtk.gdk.color_parse("#F2F1F0"))
            self.widgets.Combi_DRO_y.set_property("abs_color", gtk.gdk.color_parse(self.data.abs_color))
            self.widgets.Combi_DRO_y.set_property("rel_color", gtk.gdk.color_parse(self.data.rel_color))
            self.widgets.Combi_DRO_y.set_property("dtg_color", gtk.gdk.color_parse(self.data.dtg_color))
            self.data.diameter_mode = True
        self._update_vel()
        self._update_coolant()
        self._update_spindle_btn()
        self.widgets.active_speed_label.set_label(self.data.active_spindle_command)

        # keep the timer running
        return True

    # Initialize the file to load dialog, setting an title and the correct
    # folder as well as a file filter
    def init_file_to_load(self):
        file_dir = self.gscreen.inifile.find("DISPLAY", "PROGRAM_PREFIX")
        self.widgets.file_to_load_chooser.set_current_folder(file_dir)
        title = _("Select the file you want to be loaded at program start")
        self.widgets.file_to_load_chooser.set_title(title)
        self.widgets.ff_file_to_load.set_name("linuxcnc files")
        self.widgets.ff_file_to_load.add_pattern("*.ngc")
        file_ext = self._get_file_ext()
        for ext in file_ext:
            self.widgets.ff_file_to_load.add_pattern(ext)

    def _get_file_ext(self):
        file_ext = self.gscreen.inifile.findall("FILTER", "PROGRAM_EXTENSION")
        if file_ext:
            ext_list =["*.ngc"]
            for data in file_ext:
                raw_ext = data.split(",")
                for extension in raw_ext:
                    ext = extension.split()
                    ext_list.append(ext[0].replace(".","*."))
        else:
            print("Error converting the file extensions from INI File 'FILTER','PROGRAMM_PREFIX")
            print("using as default '*.ngc'")
            ext_list = ["*.ngc"]
        return ext_list

    # We need extra HAL pins here is where we do it.
    # Note you must import hal at the top of this script to do it.
    # we make pins for the hardware buttons witch can be placed around the
    # screen to activate the coresponding buttons on the GUI
    def initialize_pins(self):
        # generate the horizontal button pins 
        for h_button in range(0,10):
            self.signal = hal_glib.GPin(self.gscreen.halcomp.newpin("h-button-%s"%h_button, 
                                                                    hal.HAL_BIT, hal.HAL_IN))
            self.signal.connect("value_changed", self._on_h_button_changed)
            
        # generate the vertical button pins 
        for v_button in range(0,7):
            self.signal = hal_glib.GPin(self.gscreen.halcomp.newpin("v-button-%s"%v_button, 
                                                                    hal.HAL_BIT, hal.HAL_IN))
            self.signal.connect("value_changed", self._on_v_button_changed)

        # buttons for jogging the axis
        for jog_button in self.data.axis_list:
            if jog_button not in "xyz":
                jog_button = self.axisletter_four
            self.signal = hal_glib.GPin(self.gscreen.halcomp.newpin("jog-%s-plus"%jog_button, 
                                                                    hal.HAL_BIT, hal.HAL_IN))
            self.signal.connect("value_changed", self._on_pin_jog_changed, jog_button,1)
            self.signal = hal_glib.GPin(self.gscreen.halcomp.newpin("jog-%s-minus"%jog_button, 
                                                                    hal.HAL_BIT, hal.HAL_IN))
            self.signal.connect("value_changed", self._on_pin_jog_changed, jog_button,-1)

        # jog_increment out pin 
        self.jog_increment = hal_glib.GPin(self.gscreen.halcomp.newpin("jog-increment", 
                                                                       hal.HAL_FLOAT, hal.HAL_OUT))

        # generate the pins to set the increments
        self._check_len_increments()
        for buttonnumber in range(0,self.no_increments):
            self.signal = hal_glib.GPin(self.gscreen.halcomp.newpin("jog-inc-%s"%buttonnumber, 
                                                                    hal.HAL_BIT, hal.HAL_IN))
            self.signal.connect("value_changed", self._on_pin_incr_changed, buttonnumber)

        self.signal = hal_glib.GPin(self.gscreen.halcomp.newpin("unlock-settings", hal.HAL_BIT, hal.HAL_IN))
        self.signal.connect("value_changed", self._on_unlock_settings_changed)

        # generate the pins to connect encoders to the sliders
        self.feed_override_counts = hal_glib.GPin(self.gscreen.halcomp.newpin("feed-override-counts", 
                                                                             hal.HAL_S32, hal.HAL_IN))
        self.feed_override_counts.connect("value_changed", self._on_fo_counts_changed, "scl_feed")
        self.spindle_override_counts = hal_glib.GPin(self.gscreen.halcomp.newpin("spindle-override-counts", 
                                                                                hal.HAL_S32, hal.HAL_IN))
        self.spindle_override_counts.connect("value_changed", self._on_so_counts_changed, "scl_spindle")
        self.jog_speed_counts = hal_glib.GPin(self.gscreen.halcomp.newpin("jog-speed-counts", hal.HAL_S32, 
                                                                          hal.HAL_IN))
        self.jog_speed_counts.connect("value_changed", self._on_jv_counts_changed, "hal_scl_jog_vel")
        self.max_vel_counts = hal_glib.GPin(self.gscreen.halcomp.newpin("max-vel-counts", hal.HAL_S32, 
                                                                        hal.HAL_IN))
        self.max_vel_counts.connect("value_changed", self._on_mv_counts_changed, "scl_max_vel")

        # This is only necessary, because after connecting the Encoder the value will be increased by one
        self.widgets.btn_feed_100.emit("clicked")

        # make pins to react to tool_offset changes
        self.pin_offset_x = hal_glib.GPin(self.gscreen.halcomp.newpin("tooloffset_x", hal.HAL_FLOAT, hal.HAL_IN))
        self.pin_offset_x.connect("value_changed", self._offset_changed,"tooloffset_x")
        self.pin_offset_z = hal_glib.GPin(self.gscreen.halcomp.newpin("tooloffset_z", hal.HAL_FLOAT, hal.HAL_IN))
        self.pin_offset_z.connect("value_changed", self._offset_changed,"tooloffset_z")

    def _offset_changed(self,pin,tooloffset):
        if self.widgets.Combi_DRO_x.machine_units == _MM:
            self.widgets.lbl_tool_offset_z.set_text("%.3f"%self.gscreen.halcomp["tooloffset_z"])
            self.widgets.lbl_tool_offset_x.set_text("%.3f"%self.gscreen.halcomp["tooloffset_x"])
        else:
            self.widgets.lbl_tool_offset_z.set_text("%.4f"%self.gscreen.halcomp["tooloffset_z"])
            self.widgets.lbl_tool_offset_x.set_text("%.4f"%self.gscreen.halcomp["tooloffset_x"])

    def _on_pin_incr_changed(self, pin, buttonnumber):
        if not pin.get(): 
            return
        data = self.data.jog_increments[int(buttonnumber)]
        self.on_increment_changed(self.incr_rbt_list[int(buttonnumber)], data)
        self.incr_rbt_list[int(buttonnumber)].set_active(True)

    def _on_pin_jog_changed(self, pin, axis,direction):
        if axis not in "xyz":
            axis = "4"
        if direction == 1:
            widget = self.widgets["btn_%s_plus"%axis]
        else:
            widget = self.widgets["btn_%s_minus"%axis]
        if pin.get():
            self.on_btn_jog_pressed(widget)
        else:
            self.on_btn_jog_released(widget)

    def _on_unlock_settings_changed(self, pin):
        if not self.widgets.rbt_hal_unlock.get_active():
            return
        self.widgets.tbtn_setup.set_sensitive(pin.get())

    def _on_fo_counts_changed(self, pin, widget):
        counts = pin.get()
        difference = (counts - self.fo_counts) * self.scale_feed_override
        self.fo_counts = counts
        val = self.widgets[widget].get_value() + difference
        if val < 0: 
            val = 0
        if difference != 0: 
            self.widgets[widget].set_value(val)

    def _on_so_counts_changed(self, pin, widget):
        counts = pin.get()
        difference = (counts - self.so_counts) * self.scale_spindle_override
        self.so_counts = counts
        val = self.widgets[widget].get_value() + difference
        if val < 0: 
            val = 0
        if difference != 0: 
            self.widgets[widget].set_value(val)

    def _on_jv_counts_changed(self, pin, widget):
        counts = pin.get()
        difference = (counts - self.jv_counts) * self.scale_jog_vel
        self.jv_counts = counts
        val = self.widgets[widget].get_value() + difference
        if val < 0: 
            val = 0
        if difference != 0: 
            self.widgets[widget].set_value(val)

    def _on_mv_counts_changed(self, pin, widget):
        counts = pin.get()
        difference = (counts - self.mv_counts) * self.scale_max_vel
        self.mv_counts = counts
        val = self.widgets[widget].get_value() + difference
        if val < 0: 
            val = 0
        if difference != 0: 
            self.widgets[widget].set_value(val)

    # The actions of the buttons
    def _on_h_button_changed(self,pin):
        self.gscreen.add_alarm_entry("got h_button_signal %s"%pin.name)
        # we check if the button is pressed ore release,
        # otehrwise a signal will be emitted, wenn the button is released and
        # the signal drob down to zero
        if not pin.get(): 
            return
        # lets see on witch button_box we are
        page = self.widgets.ntb_button.get_current_page()
        # witch button has been pressed
        btn = str(pin.name)
        # from the list we declared under __init__ we get the button number
        nr = int(btn[-1])
        tab = self.h_tabs[page] # see in the __init__ section for the declaration of self.tabs
        button = None
        # we check if there is a button or the user pressed a hardware button under
        # a non existing software button
        for index in tab:
            if int(index[0]) == nr:
                # this is the name of the button
                button = index[1]
        if button:
            # only emit a signal if the button is sensitive, otherwise
            # running actions may be interupted
            if self.widgets[button].get_sensitive() == False:
                print("%s not_sensitive"%button)
                self.gscreen.add_alarm_entry("%s not_sensitive"%button)
                return
            self.widgets[button].emit("clicked")
        else:
            # as we are generating the macro buttons dynamecely, we can"t use the same
            # method as above, here is how we do it
            if page == 1:   # macro page
                # does the user press a valid hardware button?
                if nr < len(self.macrobuttons):
                    button = self.macrobuttons[nr] # This list is generated in add_macros_buttons(self)
                    # is the button sensitive?
                    if button.get_sensitive() == False:
                        print("%s not_sensitive"%button)
                        return
                    button.emit("pressed")
                else:
                    print("No function on this button")
                    self.gscreen.add_alarm_entry("%s not_sensitive"%button)
            else:
                print("No function on this button")

    def _on_v_button_changed(self,pin):
        self.gscreen.add_alarm_entry("got v_button_signal %s"%pin.name)
        if not pin.get():
            return
        btn = str(pin.name)
        nr = int(btn[-1])
        tab = self.v_tabs # see in the __init__ section for the declaration of self.tabs
        button = None
        for index in tab:
            if int(index[0]) == nr:
                # this is the name of the button
                button = index[1]
        if button:
            # only emit a signal if the button is sensitive, otherwise
            # running actions may be interupted
            if self.widgets[button].get_sensitive() == False:
                print("%s not_sensitive"%button)
                self.gscreen.add_alarm_entry("%s not_sensitive"%button)
                return
            button_pressed_list = ("rbt_manual","rbt_mdi","rbt_auto","tbtn_setup")
            if button in button_pressed_list:
                self.widgets[button].set_active(True)
                self.widgets[button].emit("pressed")
            else:
                self.widgets[button].emit("clicked")
        else:
            print("No button found in v_tabs from %s"%pin.name)
            self.gscreen.add_alarm_entry("No button found in v_tabs from %s"%pin.name)

    def _check_len_increments(self):
        increments = self.gscreen.inifile.find("DISPLAY", "INCREMENTS")
        if increments:
            if "," in increments:
                self.data.jog_increments = [i.strip() for i in increments.split(",")]
            else:
                self.data.jog_increments = increments.split()
            self.data.jog_increments.insert(0,0)
        else:
            self.data.jog_increments = [0,"1,000","0,100","0,010","0,001"]
            self.add_alarm_entry(_("No default jog increments entry found in [DISPLAY] of INI file"))
        self.no_increments = len(self.data.jog_increments)
        if self.no_increments > 10:
            print(_("To many increments given in INI File for this screen"))
            print(_("Only the first 10 will be reachable through this screen"))
            self.no_increments = 10
