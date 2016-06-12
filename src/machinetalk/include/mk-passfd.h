/**
 * filedescriptor sharing between rtapi_app and any process on the RT host
*/

#ifndef _MK_PASSFD_H
#define _MK_PASSFD_H

/**  mt_passfd_socket - connect Unix domain socket to rtapi_app
 *   for passing file descriptors back and forth
 *
 * returns a usable socket (>= 0) or an negative errno
 *
 * @instance_id: the RTAPI instance id (default 0)
 */
int mt_passfd_socket(const int instance_id);

/**  mt_send_fd - transfer a file descriptor and its HAL name to rtapi_app
 * returns 0 on success
 *
 * @socket: as returned from mt_passfd_socket
 * @name:   the HAL name of the fd
 * @fd:     the filedescriptor being passed
 */
int mt_send_fd(const int socket, const char *name, const int fd);


/**  mt_fetch_fd - fetch a file descriptor by HAL name from rtapi_app
 * returns 0 on success
 *
 * @socket: as returned from mt_passfd_socket
 * @name:   the HAL name of the fd
 * @pfd:    the filedescriptor being retrieved
 */
int mt_fetch_fd(const int socket, const char *name, int *pfd);


#endif // _MK_PASSFD_H
