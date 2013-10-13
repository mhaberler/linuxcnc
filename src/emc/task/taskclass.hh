#ifndef TASKCLASS_HH
#define TASKCLASS_HH

#include "emc.hh"
#include "emc_nml.hh"
#include "initool.hh"
#include "tool_parse.h"
#include "inifile.hh"

extern int emcTaskOnce(const char *inifile);
extern int emcRunHalFiles(const char *filename);

class Task {
public:
    Task();
    virtual ~Task();

    virtual int emcIoInit();
    virtual int emcIoHalt();
    virtual int emcIoAbort(int reason);
    virtual int emcToolStartChange();
    virtual int emcAuxEstopOn();
    virtual int emcAuxEstopOff();
    virtual int emcCoolantMistOn();
    virtual int emcCoolantMistOff();
    virtual int emcCoolantFloodOn();
    virtual int emcCoolantFloodOff();
    virtual int emcLubeOn();
    virtual int emcLubeOff();
    virtual int emcIoSetDebug(int debug);
    virtual int emcToolSetOffset(int pocket, int toolno, EmcPose offset, double diameter,
				 double frontangle, double backangle, int orientation);
    virtual int emcToolPrepare(int p, int tool);
    virtual int emcToolLoad();
    virtual int emcToolLoadToolTable(const char *file);
    virtual int emcToolUnload();
    virtual int emcToolSetNumber(int number);
    virtual int emcIoUpdate(EMC_IO_STAT * stat);

    virtual int emcIoPluginCall(int len, const char *msg);

    int use_iocontrol;
    int random_toolchanger;
    const char *ini_filename;
    const char *tooltable_filename;
private:

    char *ttcomments[CANON_POCKETS_MAX];
    int fms[CANON_POCKETS_MAX];
};

extern Task *task_methods;
extern int emcIoUpdate(EMC_IO_STAT * stat);
extern int emcIoPluginCall(EMC_IO_PLUGIN_CALL *call_msg);
extern int emcPluginCall(EMC_EXEC_PLUGIN_CALL * cmd);
#endif
