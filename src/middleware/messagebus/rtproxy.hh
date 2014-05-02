
typedef enum {
    IDLE,
    WAIT_FOR_COMMAND,     // from bus - cmd-out
    WAIT_FOR_RT_MSG,      // from RTcomp - not decided yet whether it'll be
                          // a subcommand or reply (dual injector/responder comps)
    WAIT_FOR_RESPONSE,    // from bus - response-out (injector only)
    WAIT_FOR_RT_RESPONSE, // from RTcomp - command sent to RTcomp, waiting for RTcomp to reply (responder only)
} rtproxystate_t;

typedef struct {
    const char *name;
    unsigned flags;
    rtproxystate_t state;
    void *pipe;
    void *proxy_response_in;
    void *proxy_response_out;
    void *proxy_cmd_in;
    void *proxy_cmd_out;
    ringbuffer_t to_rt;
    ringbuffer_t from_rt;
    const char *to_rt_name;
    const char *from_rt_name;
    bool decode_out, encode_in;  // pb_encode()/pb_decode() before/after RT I/O
    int min_delay, max_delay, current_delay; // poll delay with exponential backoff
} rtproxy_t;


void rtproxy_thread(void *arg, zctx_t *ctx, void *pipe);
