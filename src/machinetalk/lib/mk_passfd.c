#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mk-passfd.h"
#include "ancillary.h"
#include "passfd_proto.h"


static const char *socket_path = PASSFD_SOCKET;

int mt_passfd_socket(const int instance_id)
{
    struct sockaddr_un address;
    int rc;

    int socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0)	{
	rc = -errno;
	perror("socket");
	return rc;
    }

    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s:%d",
	     socket_path, instance_id);
    address.sun_path[0] = '\0';

    if (connect(socket_fd, (struct sockaddr *) &address,
		sizeof(struct sockaddr_un)) != 0) {
	rc = -errno;
	perror("connect");
	return rc;
    }
    return socket_fd;
}

int mt_send_fd(const int socket, const char *name, const int fd)
{
    struct passfd_request req;
    struct passfd_response resp;
    int rc, n;

    req.op = PFD_PUT;
    strncpy(req.name, name, sizeof(req.name));
    if (write(socket, &req, sizeof(req)) != sizeof(req)) {
	rc = -errno;
	perror("write");
	return rc;
    }
    if ((rc = ancil_send_fd(socket, fd)) != 0) {
	perror("ancil_send_fd");
	return rc;
    }
    n = read(socket, &resp, sizeof(resp));
    rc = -errno;
    if (n != sizeof(resp)) {
	perror("read");
	return rc;
    }
    return resp.hal_rc;
}

int mt_fetch_fd(const int socket, const char *name, int *pfd)
{
    struct passfd_request req;
    struct passfd_response resp;
    int rc, n;

    req.op = PFD_GET;
    strncpy(req.name, name, sizeof(req.name));
    if (write(socket, &req, sizeof(req)) != sizeof(req)) {
	rc = -errno;
	perror("write");
	return rc;
    }
    n = read(socket, &resp, sizeof(resp));
    rc = -errno;
    if (n != sizeof(resp)) {
	perror("read");
	return rc;
    }
    if (resp.hal_rc)
	return resp.hal_rc;
    if ((rc = ancil_recv_fd(socket, pfd)) != 0) {
	perror("ancil_recv_fd");
	return rc;
    }
    return resp.hal_rc;
}
