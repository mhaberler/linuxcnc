void readahead_reading(void);
void readahead_waiting(void);
void mdi_execute_abort(void);
void mdi_execute_hook(void);
// puts command on interp list
int emcTaskQueueCommand(NMLmsg * cmd);
