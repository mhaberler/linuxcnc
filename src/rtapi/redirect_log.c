#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <syslog_async.h>
#include <stdlib.h>

#include "redirect_log.h"

static size_t writer(void *cookie, char const *data, size_t leng)
{
    int level= LOG_ERR;
    const char *tag = cookie;
    char *l, *lines = strndup(data, leng);

    if (lines == NULL)
	return  leng; // fail silently

    l = strtok(lines, "\n");
    while (l != NULL) {
	syslog_async(level, "%s%s", tag, l);
	l = strtok(NULL, "\n");
    }
    free(lines);
    return  leng;
}

static int noop(void) {return 0;}

static cookie_io_functions_t log_fns = {
    (void*) noop,
    (void*) writer,
    (void*) noop,
    (void*) noop
};

void to_syslog(const char *tag, FILE **pfp)
{
    setvbuf(*pfp = fopencookie((char *) tag, "w", log_fns), NULL, _IOLBF, 0);
}

#ifdef TEST
int main(int argc, char **argv)
{
    openlog_async("foo", LOG_NDELAY , LOG_USER);
    to_syslog("stderr: ", &stderr);
    fprintf(stderr, "this came via stderr\n");
    exit(0);
}
#endif
