#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER machinekit_provider

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./machinekit_tp.h"

#if !defined(_MACHINEKIT_TP_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _MACHINEKIT_TP_H

#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(
    machinekit_provider,
    cycle_counter_tracepoint,
    TP_ARGS(
	char*, cycle_name,
        int, cycle_counter
    ),
    TP_FIELDS()
)

TRACEPOINT_EVENT(
    machinekit_provider,
    function_tracepoint,
    TP_ARGS(
        char*, function_name
    ),
    TP_FIELDS()
)

#endif /* _MACHINEKIT_TP_H */
#include <lttng/tracepoint-event.h>
