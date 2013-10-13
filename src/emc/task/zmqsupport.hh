int shutdown_zmq(void);
int publish_ticket_update(int state, int ticket, string origin, string text = "");
int setup_zmq(void);

extern int zdebug;
extern void *cmd; // socket
