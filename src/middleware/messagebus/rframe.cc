#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <czmq.h>

#define MAXFRAMES 5

// header 'wire' and API representation
typedef struct {
    unsigned int more : 1;
    unsigned int userflags  : 31;
    size_t len;
} rframe_header_t;

// rframe 'wire' representation in ring
typedef struct {
    rframe_header_t rh;
    unsigned char payload[0];
} wrframe_t;

// API rframe representation
typedef struct {
    rframe_header_t rh;
    void *buffer;
} rframe_t;

// API rmsg representation
typedef struct {
    size_t   nframes;
    rframe_t rf[MAXFRAMES];
} rmsg_t;

int main(int argc, const char **argv)
{
#if 0
    const char *from = "from";
    const char *to = "to";

    void *dest = malloc(1000);

    //struct point ptarray[10] = { [2].y = yv2, [2].x = xv2, [0].x = xv0 };


    rmsg_t m = // (rmsg_t)
	{
	.nframes = 1,
	.rf = {
		[0] = {
		    //.rh.more = 1,
		    .buffer = from
		}
	    }
	.rf[0] = {
	    .more = 1,
	    .userflags = 42,
	    .len = strlen(from),
	    .buffer = from
	}

    };
#endif

    return 0;
}
