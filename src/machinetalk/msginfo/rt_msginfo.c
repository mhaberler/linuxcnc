// this component provides support for other RT components
// it exports protobuf message descriptors in nanopb format
// as automatically generated in machinetalk/Submakefile
// from machinetalk/proto/*.proto definitions

#define EXPORT_FIELDS 1

#ifdef RTAPI // build as loadable component

#include "config.h"		/* GIT_VERSION */
#include "msginfo.h"

#define PB_MSGID
#include <machinetalk/generated/message.npb.h>
#include <machinetalk/generated/test.npb.h>

#if defined(BUILD_SYS_USER_DSO)
#include <stdlib.h>   // qsort()
#else  //defined(BUILD_SYS_KBUILD)
#include <linux/sort.h>
#endif

#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_logging.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("RT-accessible protobuf message descriptors");
MODULE_LICENSE("GPL");

static int debug = 0;
RTAPI_MP_INT(debug, "print the sorted list of descriptors to the RTAPI log");

#if EXPORT_FIELDS
// export pb_<messagename>_fields within RTAPI
#undef PB_MSG
#define PB_MSG(id, size, name)  EXPORT_SYMBOL(name ## _fields);
#include "machinetalk/generated/message_defs.h"
#endif

// export the descriptor array
#undef PB_MSG
#define PB_MSG(id, size, name) {					\
	id,								\
	    size,							\
	    # name,							\
	    sizeof(PB_MSG_ ## id),					\
	    name ## _fields,						\
	    NULL, /* descriptor */					\
	    NULL, /* user_ptr */					\
	    0    /* user_flags */					\
	    },

#define PB_MSGINFO_DELIMITER {0, -1, NULL, 0, NULL, NULL, NULL, 0}

// this likely supersedes the above exports, as it
// contains a superset of pb_<message>_fields
pbmsginfo_t pbmsginfo[] = {
    #include "machinetalk/generated/message_defs.h"
    PB_MSGINFO_DELIMITER
};
unsigned pbmsginfo_count;

EXPORT_SYMBOL(pbmsginfo);
EXPORT_SYMBOL(pbmsginfo_count);

static int comp_id;
static const char *name = "msginfo";

int rtapi_app_main(void)
{
    if ((comp_id = hal_init(name)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: ERROR: hal_init() failed: %d\n",
			name, comp_id);
	return -1;
    }

    // post linking, the descriptors are unsorted
    // sort the descriptors by msgid so we can use bsearch for lookup
    pbmsginfo_count = sizeof(pbmsginfo)/sizeof(pbmsginfo_t) - 1;

#if defined(BUILD_SYS_USER_DSO)
    qsort(pbmsginfo, pbmsginfo_count, sizeof(pbmsginfo_t), msginfo_cmp);
#else //defined(BUILD_SYS_KBUILD)
    sort(pbmsginfo, pbmsginfo_count, sizeof(pbmsginfo_t), msginfo_cmp, NULL);
#endif
    hal_ready(comp_id);
    rtapi_print_msg(RTAPI_MSG_DBG,
		    "%s git=" GIT_VERSION " nanopb=" VERSION ", %d descriptors\n",
		    name, pbmsginfo_count);


    const pbmsginfo_t *p;
    for (p = pbmsginfo; p->msgid != 0; p++) {
	if (p->msgid >= pb_msgidType_MSGID_MAX) {
	    HALERR("FATAL: message %s has msgid %d (>= MSGID_MAX/%d)!!",
		   p->name, p->msgid, pb_msgidType_MSGID_MAX);
	    return -1;
	}
	if (debug)
	    // just show msgid's and message names in log
	    HALINFO("%5.5d %s", p->msgid, p->name);
    }
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id);
}

#endif // RTAPI

#if 0

// since the above macro expansions look quite opaque:
//
// below is a fragment of what is actually being emitted by the above macros.
//
// one descriptor for each protobuf message which has the msgid option set
// note the fields are for the nanopb library; the NULL user pointer can be
// used to descriptors of other libraries, like Google Protobuf/C++

// since the proto definitions use 'package pb;', the string name is prefixed by
// the package name (pb_)

// this descriptor array can now be used for many purposes,
// for example Cython bindings: serialize/deserialize from/to nanopb C structs
// or other API purposes, for instance mapping a msgid to the descriptor, or its
// string name for a log message

pbmsginfo_t pbmsginfo[] = {
    { 100,                         // the msgid value
      (13 + 91),                   // Maximum encoded size of messages (where known)
      "pb_Emc_Traj_Set_G5x",       // string name of the Message, nanopb convention
      sizeof(pb_Emc_Traj_Set_G5x), // size of the nanopb-generated C struct which holds
                                   // a deserialized message
      pb_Emc_Traj_Set_G5x_fields,  // reference to the nanopb message descriptor
                                   // used for encoding and decoding this message
      ((void *)0),                 // gpb c++ Descriptor not available in RT
      ((void *)0),                 // user extensible
      0                            // user extensible
    },

    { 101,
      (6 + 91),
      "pb_Emc_Traj_Set_G92",
      sizeof(pb_Emc_Traj_Set_G92),
      pb_Emc_Traj_Set_G92_fields,
      ((void *)0),
      ((void *)0),
      0
    },
....
    {0, -1, ((void *)0), 0, ((void *)0), ((void *)0), ((void *)0), ((void *)0), 0}
};

__attribute__((section(".rtapi_export"))) char rtapi_exported_msginfo[] = "msginfo";;

#endif
