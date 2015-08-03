#include "config.h"
#include "msginfo.h"
#include "rtapi_export.h"

#if defined(ULAPI) || defined(BUILD_SYS_USER_DSO)
#include <stdlib.h>   // bsearch()
#else
#include <linux/bsearch.h>
#endif

extern pbmsginfo_t pbmsginfo[];
extern unsigned pbmsginfo_count;


int msginfo_cmp(const void *p1, const void *p2)
{
    const pbmsginfo_t *m1 = p1;
    const pbmsginfo_t *m2 = p2;
    return m1->msgid - m2->msgid;
}

// given a msgid, retrieve the pbmsginfo_t descriptor
const  pbmsginfo_t *pbmsgdesc_by_id(const unsigned msgid)
{
    pbmsginfo_t key = { .msgid = msgid };

    const pbmsginfo_t *p = bsearch(&key, pbmsginfo, pbmsginfo_count, sizeof( pbmsginfo_t),msginfo_cmp);
    return p;
}

// given a message name, retrieve the pbmsginfo_t descriptor
const  pbmsginfo_t *pbmsgdesc_by_name(const char *name)
{
    const pbmsginfo_t *p;
    for (p = pbmsginfo; p->msgid != 0; p++)
	if (strcmp(p->name, name) == 0)
	    return p;
    return NULL;
}

#ifdef RTAPI
EXPORT_SYMBOL(msginfo_cmp);
EXPORT_SYMBOL(pbmsgdesc_by_id);
EXPORT_SYMBOL(pbmsgdesc_by_name);
#endif
