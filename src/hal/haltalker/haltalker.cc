
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <sys/timerfd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <alloca.h>
#include <limits.h>		/* for CHAR_BIT */
#include <signal.h>


#ifndef ULAPI
#error This is intended as a userspace component only.
#endif

#include <rtapi.h>
#include <hal.h>
#include <hal_priv.h>
#include <hal_group.h>
#include <inifile.h>
#include <czmq.h>

#include <protobuf/generated/types.pb.h>
#include <protobuf/generated/value.pb.h>
#include <protobuf/generated/object.pb.h>
#include <protobuf/generated/message.pb.h>

class HalReport {
 public:

 private:

};

main()
{}
