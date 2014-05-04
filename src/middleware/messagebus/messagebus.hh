// actor types
#define ACTOR_INJECTOR  1
#define ACTOR_RESPONDER 2
#define ACTOR_ECHO      4
#define ACTOR_TRACE     8

#define DESERIALIZE_TO_RT 16  // send as nanopb struct if decoded as such
#define SERIALIZE_FROM_RT 32  // encode from nanopb struct 

extern int comp_id;
extern const char *progname;

