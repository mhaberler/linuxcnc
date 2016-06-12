#ifdef RTAPI

// store a file descriptor in a S32 out pin value.
// if the pin does not exist and creat is non-zero, it is created and owned by hal_lib.
int hal_set_fd(const char *name, hal_s32_t fd, int creat);

// look up a pin 'name' which must be of type S32 and dir OUT.
// Store it's value in *pfd on success.
int hal_get_fd(hal_s32_t *pfd, const char *name);

// lookup a named file descriptor, and close it.
int hal_close_fd(const char *name);

#endif
