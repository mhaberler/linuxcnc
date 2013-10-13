//    Copyright 2007 Jeff Epler <jepler@unpythonic.net>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#ifndef EMC_TASK_HH
#define EMC_TASK_HH
#include "taskclass.hh"
extern NMLmsg *emcTaskCommand;
extern int stepping;
extern int steppingWait;

// puts command on interp list
extern int emcTaskQueueCommand(NMLmsg *cmd);
// issues command immediately
extern int emcTaskIssueCommand(NMLmsg *cmd);

extern int emcPluginCall(EMC_EXEC_PLUGIN_CALL *call_msg);
extern int emcIoPluginCall(EMC_IO_PLUGIN_CALL *call_msg);
extern int emcTaskOnce(const char *inifile);
extern int emcRunHalFiles(const char *filename);

// emctask.cc
extern int emcTaskUpdate(EMC_TASK_STAT * stat); // use: emctaskmain.cc
extern int emcTaskPlanSetWait(void);
extern int emcTaskPlanIsWait(void);
extern int emcTaskPlanClearWait(void);


// exported by emctaskplan.cc
extern int emcTaskPlan(RCS_CMD_MSG *emcCommand, EMC_STAT *emcStatus);
// used by emcTaskPlan()
extern NML_INTERP_LIST mdi_execute_queue;
extern void mdi_execute_hook(void);
extern void readahead_waiting(void);
extern void readahead_reading(void);

extern int  get_interpResumeState(void);
extern void set_interpResumeState(int);

// exported by emctaskexecute.cc

extern int emcTaskExecute(EMC_STAT *emcStatus);
// used by emctaskexecute.cc
extern void set_eager(bool e);
extern bool get_eager(void);
extern void mdi_execute_abort(void);

// systemcmd.cc
extern int emcSystemCmd(char *s);
extern pid_t get_SystemCmdPid(void);
extern void set_SystemCmdPid(pid_t pid);

// exported by conditions.cc
extern int emcTaskCheckPreconditions(NMLmsg * cmd);
extern int emcTaskCheckPostconditions(NMLmsg * cmd);
#endif

