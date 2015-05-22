#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>

int running_on_rt_host(const int rtapi_instance, const char *uuid)
{

    char buf[PATH_MAX];
    struct stat sb;

    // check RTAPI command socket
    snprintf(buf, sizeof(buf), ZMQIPC_FORMAT,
	     RUNDIR, rtapi_instance, "rtapi", uuid);

    if (stat(buf, &sb) == -1)
	return 0;

    if (!((sb.st_mode & S_IFMT) == S_IFSOCK))
	return 0;

    return 1;
}
