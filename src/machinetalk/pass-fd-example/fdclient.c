#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <inttypes.h>
#include <assert.h>

#include "mk-passfd.h"


char *fdname = "evclient";
int instance_id = 0;

int updatefd(int fd)
{
    uint64_t u = 0;
    while (1) {
	ssize_t s = write(fd, &u, sizeof(uint64_t));
	if (s != sizeof(uint64_t))
	    perror("write");
	else
	    printf("Sent %"PRId64"\n", u);
	u++;
	sleep(1);
    }
    return 0;
}

int watchfd(int fd)
{
    if (fd < 0) {
	printf("bad event fd\n");
	exit(1);
    }
    struct pollfd   fds[1];
    int             rc;
    fds[0].fd = fd;
    fds[0].events = POLLERR | POLLIN ;

    printf("starting poll\n");

    while (1) {
	rc = poll((struct pollfd *) &fds, 1, -1);
	if (rc < 0)
	    perror("poll");
	else {
	    uint64_t u;
	    ssize_t s;
	    if (fds[0].revents & POLLIN) {
		printf("about to read\n");
		s = read(fd, &u, sizeof(uint64_t));
		if (s != sizeof(uint64_t))
		    perror("read");
		printf("read %llu (0x%llx) from efd %d\n",
		       (unsigned long long) u, (unsigned long long) u, fd);
	    }
	    if (fds[0].revents & POLLERR) {
		printf("POLLERR \n");
		break;
	    }
	}
    }
    return 0;
}

int main(int argc, char **argv)
{
    int  socket_fd;
    int opt;
    int send = 0;
    int fd = -1;
    int update = 0; // update fd periodically
    int watch = 0;  // watch fd

    while ((opt = getopt(argc, argv, "csn:i:uw")) != -1) {
	switch (opt) {
	case 's':
	    // create an eventfd
	    // transfer fd to rtapi_app under name (set with -n <name>)
	    send = 1;
	    break;
	case 'n':
	    fdname = optarg;
	    break;
	case 'i':
	    instance_id = atoi(optarg);
	    break;
	case 'u':
	    // signal towards HAL
	    update = 1;
	    break;
	case 'w':
	    // watch updates from HAL
	    watch = 1;
	    break;
	default:
	    fprintf(stderr, "Usage: %s [-n fdname] [-i instance_id] [-s] [-u] [-w]\n",
		    argv[0]);
	    exit(EXIT_FAILURE);
	}
    }

    socket_fd = mt_passfd_socket(instance_id);
    assert(socket_fd >= 0);

    int retval;
    if (send) {
	fd = eventfd(0,0);
	assert (fd > -1);
	retval = mt_send_fd(socket_fd, fdname, fd);
    } else {
	retval = mt_fetch_fd(socket_fd, fdname, &fd);
	assert (fd > -1);
    }

    if (retval == 0) {
	if (update)
	    updatefd(fd);
	if (watch)
	    watchfd(fd);
    } else {
	fprintf(stderr, "fd transfer failed: %d\n", retval);
    }
    return 0;
}
