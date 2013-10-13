// issues command immediately
int emcTaskIssueCommand(NMLmsg * cmd);

// Variables to handle MDI call interrupts
// Depth of call level before interrupted MDI call
extern int mdi_execute_level;
// Schedule execute(0) command
extern int mdi_execute_next;
// Wait after interrupted command
extern int mdi_execute_wait;
