
#include <string>

int shutdown_zmq(void);
int publish_ticket_update(int state, int ticket, std::string origin, std::string text = "");
int setup_zmq(void);

extern int zdebug;
extern void *cmd; // socket
