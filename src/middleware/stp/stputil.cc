#include <czmq.h>
#include <string>


#include <google/protobuf/text_format.h>
#include <middleware/generated/message.pb.h>
#include <stp.h>

namespace gpb = google::protobuf;

extern int stp_debug;

int retcode(void *pipe)
{
    char *retval = zstr_recv (pipe);
    int rc = atoi(retval);
    zstr_free(&retval);
    return rc;
}
