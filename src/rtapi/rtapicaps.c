// inspect a module's RTAPI_TAG flags.

#include "rtapi.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "hal.h"
#include "rtapi_compat.h"

char *progname;
char *hal_prefix = "HAL=";
char *name_prefix = "HALCOMPNAME=";

void decode(const char *fn, char *s)
{
    if (strncmp(s, hal_prefix, strlen(hal_prefix)) == 0) {
	// bitmask flags, may be repeated
	char *cp = s;
	unsigned long value = strtoul(s+4, &cp, 0);
	if ((*cp != '\0') && (!isspace(*cp))) {
	    // invalid chars in string
	    fprintf(stderr, "%s: value '%s':  invalid integer value\n", fn, s);
	} else
	    printf("%s: ", fn);
	if (value & HC_INSTANTIABLE) printf("HC_INSTANTIABLE ");
	if (value & HC_SINGLETON) printf("HC_SINGLETON ");
	if (value & HC_SMP_SAFE) printf("HC_SMP_SAFE ");
	printf("\n");
    } else if (strncmp(s, name_prefix, strlen(name_prefix)) == 0) {
	printf("%s: HAL component name: '%s'\n", fn, s + strlen(name_prefix));
    }
}


int main(int argc, char **argv)
{
    progname = argv[0];
    if (argc < 2) {
	    fprintf(stderr,"Usage: %s <module pathnames>\n", progname);
	    exit(1);
    }
    for (int i = 1; i < argc; i++) {
	char *fn = argv[i];
	struct stat sb;

	if (stat(fn, &sb) < 0) {
	    perror(fn);
	    continue;
	}
	char **caps = (char **) get_caps(fn);
	if (caps) {
	    char **s = caps;
	    while (*s) {
		if (strlen(*s)) {
		    // printf("--- %s: '%s'\n", fn, *s);
		    decode(fn, *s);
		}
		s++;
	    }
	    free(caps);
	} else {
	    printf("%s: no RTAPI_TAGs present\n", fn);
	}
    }
    exit(0);
}
