// this exports protobuf message descriptors in nanopb format
// as automatically generated in machinetalk/Submakefile
// from machinetalk/proto/*.proto definitions, as well
// as the gpb C++ descriptors

#include "config.h"		/* GIT_VERSION */
#include "msginfo.h"
#include "assert.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>

#include <machinetalk/generated/message.pb.h>
#include <machinetalk/generated/types.pb.h>

#define PB_MSGID
#include <machinetalk/generated/message.npb.h>
#include <machinetalk/generated/test.npb.h>

#undef PB_MSG
#define PB_MSG(id, size, name) {					\
	id,								\
	    size,							\
	    # name,							\
	    sizeof(PB_MSG_ ## id),					\
	    name ## _fields,						\
	    NULL, /* descriptor */					\
	    NULL, /* user_ptr */					\
	    0,    /* user_flags */					\
	    },

#define PB_MSGINFO_DELIMITER {0, -1, NULL, 0, NULL, NULL, NULL, 0}

pbmsginfo_t pbmsginfo[] = {

     #include "machinetalk/generated/message_defs.h"

    PB_MSGINFO_DELIMITER
};

using namespace google::protobuf;

unsigned pbmsginfo_count;

// shared library constructor
static void  __attribute__ ((constructor))  init_metadata(void)
{
    // insert gpb message descriptors - nanopb doesnt know about them

    for (pbmsginfo_t *p = pbmsginfo; p->msgid != 0; p++) {
	std::string name(p->name);
	name[2] = '.'; // undo nanopb damage

	const Descriptor *d = DescriptorPool::generated_pool()->FindMessageTypeByName(name);
	if (d == NULL) {
	    fprintf(stderr, "%s:%s:%d: fatal: Message descriptor for '%s' not found\n",
		    __FILE__, __FUNCTION__, __LINE__, name.c_str());
	} else {
	    // sanity check the message id:
	    const MessageOptions& options = d->options();
	    uint32 msgid = options.GetExtension(nanopb_msgopt).msgid();
	    if (msgid != p->msgid) {
		fprintf(stderr, "id mismatch for '%s': descriptor=%d msginfo=%d\n",
			name.c_str(), msgid, p->msgid);
		assert(msgid == p->msgid);
	    }
	    if (p->msgid >= pb::MSGID_MAX) {
		fprintf(stderr,"FATAL: message %s has msgid %d (>= MSGID_MAX/%d)!!",
			p->name, p->msgid, pb::MSGID_MAX);
		assert(p->msgid < pb::MSGID_MAX);
	    }

	    // fprintf(stderr, "init '%s' %p\n", name.c_str(), d);
	    p->descriptor = d;
	}
	pbmsginfo_count++;
    }
    // and sort them by msgid for bsearch(), see pbmsgdesc_by_id()
    qsort(pbmsginfo, pbmsginfo_count, sizeof(pbmsginfo_t), msginfo_cmp);
}

