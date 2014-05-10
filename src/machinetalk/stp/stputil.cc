#include <czmq.h>
#include <string>


#include <google/protobuf/text_format.h>
#include <machinetalk/generated/message.pb.h>
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

static int log_level = STP_ERR | STP_WARN | STP_NOTICE;

static void stp_emit_stderr(int level, const char *line);
static void (*stp_emit)(int level, const char *line) = stp_emit_stderr;

void _stp_log(int filter, const char *format, ...)
{
	char buf[256];
	va_list ap;

	if (!(log_level & filter))
		return;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	buf[sizeof(buf) - 1] = '\0';
	va_end(ap);

	stp_emit(filter, buf);
}

/**
 * stp_set_log_level() - Set the logging bitfield
 * @level:	OR together the LLL_ debug contexts you want output from
 * @log_emit_function:	NULL to leave it as it is, or a user-supplied
 *			function to perform log string emission instead of
 *			the default stderr one.
 *
 *	log level defaults to "err", "warn" and "notice" contexts enabled and
 *	emission on stderr.
 */

void stp_set_log_level(int level, void (*log_emit_function)(int level,
							      const char *line))
{
	log_level = level;
	if (log_emit_function)
		stp_emit = log_emit_function;
}

static void stp_emit_stderr(int level, const char *line)
{
    fprintf(stderr, line);
}
