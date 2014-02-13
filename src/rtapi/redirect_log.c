#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <syslog.h>


static size_t writer(void *cookie, char const *data, size_t leng)
{
    const char *tag = cookie;
    int     p = LOG_ERR;
    syslog(p, "%s%.*s", tag, leng, data);
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
    setvbuf(*pfp = fopencookie(tag, "w", log_fns), NULL, _IOLBF, 0);
}

#ifdef TEST
int main(int argc, char **argv)
{
    openlog("foo", LOG_NDELAY , LOG_USER);
    to_syslog("stderr:", &stderr);
    fprintf(stderr, "this came via stderr\n");
    exit(0);
}
#endif
