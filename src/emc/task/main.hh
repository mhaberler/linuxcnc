extern NMLmsg *emcTaskCommand;


extern NML_INTERP_LIST mdi_execute_queue;

extern void set_eager(bool e);
extern bool get_eager(void);


extern int  get_interpResumeState(void);
extern void set_interpResumeState(int);


//XXX make accessors
extern int stepping;
extern int steppingWait;
