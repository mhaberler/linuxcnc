
#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"
#include "hal_group.h"		/* HAL group decls */

#if defined(ULAPI)
#include <stdlib.h>		/* malloc()/free() */
#include <assert.h>
#endif

int hal_group_new(const char *name, int arg1, int arg2)
{
    CHECK_HALDATA();
    CHECK_STRLEN(name, HAL_NAME_LEN);
    CHECK_LOCK(HAL_LOCK_LOAD);

    HALDBG("creating group '%s' arg1=%d arg2=%d/0x%x",
	   name, arg1, arg2, arg2);
    {
	WITH_HAL_MUTEX();

	hal_group_t *group = halpr_find_group_by_name(name);
	if (group != 0) {
	    HALERR("group '%s' already defined", name);
	    return -EINVAL;
	}
	if ((group = shmalloc_desc(sizeof(hal_group_t))) == NULL)
	    NOMEM("group '%s'",  name);

	hh_init_hdrf(&group->hdr, HAL_GROUP, 0, "%s", name);
	group->userarg1 = arg1;
	group->userarg2 = arg2;

	halg_add_object(false, (hal_object_ptr)group);
	return 0;
    }
}

int hal_group_delete(const char *name)
{
    CHECK_HALDATA();
    CHECK_STR(name);
    CHECK_LOCK(HAL_LOCK_CONFIG);

    HALDBG("deleting group '%s'", name);
    {
	WITH_HAL_MUTEX();

	hal_group_t *group = halpr_find_group_by_name(name);
	if (group == NULL) {
	    HALERR("group '%s' not found", name);
	    return -ENOENT;
	}
	if (group->refcount) {
	    HALERR("cannot delete group '%s' (still used: %d)",
		   name, group->refcount);
	    return -EBUSY;
	}
	// NB: freeing any members is done in free_group_struct
	free_group_struct(group);
    }
    return 0;
}

int hal_ref_group(const char *name)
{
    WITH_HAL_MUTEX();
    hal_group_t *group  = halpr_find_group_by_name(name);
    if (group == NULL)
	return -ENOENT;
    group->refcount += 1;
    return 0;
}

int hal_unref_group(const char *name)
{
    WITH_HAL_MUTEX();
    hal_group_t *group = halpr_find_group_by_name(name);
    if (group == NULL)
	return -ENOENT;
    group->refcount -= 1;
    return 0;
}

int hal_member_new(const char *group, const char *member,
		   int arg1, int eps_index)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_LOAD);
    CHECK_STRLEN(group, HAL_NAME_LEN);
    CHECK_STRLEN(member, HAL_NAME_LEN);

    HALDBG("creating member '%s' arg1=%d epsilon[%d]=%f",
	   member, arg1, eps_index, hal_data->epsilon[eps_index]);
    {
	WITH_HAL_MUTEX();

	hal_member_t *new;
	hal_sig_t *sig = NULL;

	hal_group_t *grp = halpr_find_group_by_name(group);
	if (!grp) {
	    HALERR("no such group '%s'", group);
	    return -ENOENT;
	}

	// fail if group referenced
	if (grp->refcount) {
	    HALERR("cannot change referenced group '%s', refcount=%d",
		   group, grp->refcount);
	    return -EBUSY;
	}

	if ((sig = halpr_find_sig_by_name(member)) == NULL) {
	    HALERR("no such signal '%s'", member);
	    return -ENOENT;
	}
	HALDBG("adding signal '%s' to group '%s'",  member, group);

	// TBD: detect duplicate insertion

	if ((new = shmalloc_desc(sizeof(hal_member_t))) == NULL)
	    NOMEM("member '%s'",  member);
	hh_init_hdrf(&new->hdr, HAL_MEMBER, ho_id(grp), "%s", member);

	new->sig_ptr = SHMOFF(sig);
	new->userarg1 = arg1;
	new->eps_index = eps_index;

	halg_add_object(false, (hal_object_ptr)new);
	return 0;
    }
}

int hal_member_delete(const char *group, const char *member)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_LOAD);
    CHECK_STRLEN(group, HAL_NAME_LEN);
    CHECK_STRLEN(member, HAL_NAME_LEN);

    {
	WITH_HAL_MUTEX();

	hal_group_t *grp;
	hal_member_t  *mptr;

	grp = halpr_find_group_by_name(group);
	if (!grp) {
	    HALERR("no such group '%s'", group);
	    return -EINVAL;
	}
	// fail if group referenced
	if (grp->refcount) {
	    HALERR("cannot change referenced group '%s', refcount=%d",
		   group, grp->refcount);
	    return -EBUSY;
	}
	mptr = halg_find_object_by_name(0, HAL_MEMBER, member).member;
	if (!mptr) {
	    HALERR("no such member '%s'", member);
	    return -ENOENT;
	}
	HALDBG("deleting member '%s' from group '%s'",  member, group);
	halg_free_object(false, (hal_object_ptr) mptr);
    }
    return 0;
}

#ifdef ULAPI

static int cgroup_init_members_cb(hal_object_ptr o, foreach_args_t *args)
{
    hal_member_t *member = o.member;
    hal_compiled_group_t *tc = args->user_ptr1;
    hal_group_t *group  = args->user_ptr2;

    tc->member[tc->mbr_index] = member;
    tc->mbr_index++;
    if ((member->userarg1 & MEMBER_MONITOR_CHANGE) ||
	(group->userarg2 & GROUP_MONITOR_ALL_MEMBERS)) {
	tc->mon_index++;
    }
    return 0;
}

static int cgroup_size_cb(hal_object_ptr o, foreach_args_t *args)
{
    hal_member_t *member = o.member;
    hal_compiled_group_t *tc = args->user_ptr1;
    hal_group_t *group  = args->user_ptr2;

    tc->n_members++;
    if ((member->userarg1 & MEMBER_MONITOR_CHANGE) ||
	(group->userarg2 & GROUP_MONITOR_ALL_MEMBERS))
	tc->n_monitored++;
    return 0;
}

// group generic change detection & reporting support
int halpr_group_compile(const char *name, hal_compiled_group_t **cgroup)
{
    hal_compiled_group_t *tc;
    hal_group_t *grp;

    CHECK_STR(name);

    if ((grp = halpr_find_group_by_name(name)) == NULL) {
	HALERR("no such group '%s'", name);
	return -EINVAL;
    }

    // a compiled group is a userland memory object
    if ((tc = malloc(sizeof(hal_compiled_group_t))) == NULL)
	NOMEM("hal_compiled_group");

    memset(tc, 0, sizeof(hal_compiled_group_t));

    // first pass: determine sizes
    // this fills sets the n_members and n_monitored fields
    foreach_args_t args =  {
	.type = HAL_MEMBER,
	.owner_id = ho_id(grp),
	.user_ptr1 = tc,
	.user_ptr2 = grp,
    };
    halg_foreach(0, &args, cgroup_size_cb);

    HALDBG("hal_group_compile(%s): %d signals %d monitored",
	   name, tc->n_members, tc->n_monitored );

    if ((tc->member =
	 malloc(sizeof(hal_member_t  *) * tc->n_members )) == NULL)
	NOMEM("%d hal_members",  tc->n_members);

    tc->mbr_index = 0;
    tc->mon_index = 0;

    // second pass: fill in references (same args)
    halg_foreach(0, &args, cgroup_init_members_cb);

    assert(tc->n_monitored == tc->mon_index);
    assert(tc->n_members == tc->mbr_index);

    // this attribute combination does not make sense - such a group
    // definition will never trigger a report:
    if ((grp->userarg2 & (GROUP_REPORT_ON_CHANGE|GROUP_REPORT_CHANGED_MEMBERS)) &&
	(tc->n_monitored  == 0)) {
	HALERR("changed-monitored group '%s' with no members to check",
	       name);
	return -EINVAL;
    }

    // set up value tracking if either the whole group is to be monitored for changes
    // to cause a report, or only changed members should be included in a periodic report
    if ((grp->userarg2 & (GROUP_REPORT_ON_CHANGE|GROUP_REPORT_CHANGED_MEMBERS)) ||
	(tc->n_monitored  > 0)) {
	if ((tc->tracking =
	     malloc(sizeof(hal_data_u) * tc->n_monitored )) == NULL)
	    return -ENOMEM;
	memset(tc->tracking, 0, sizeof(hal_data_u) * tc->n_monitored);
	if ((tc->changed =
	     malloc(RTAPI_BITMAP_BYTES(tc->n_members))) == NULL)
	    return -ENOMEM;
	RTAPI_ZERO_BITMAP(tc->changed, tc->n_members);
    } else {
	// nothing to track
	tc->n_monitored = 0;
	tc->tracking = NULL;
	tc->changed = NULL;
    }

    tc->magic = CGROUP_MAGIC;
    tc->group = grp;
    grp->refcount++;
    tc->user_data = NULL;
    tc->user_flags = 0;
    *cgroup = tc;

    return 0;
}

int hal_cgroup_match(hal_compiled_group_t *cg)
{
    int i, monitor, nchanged = 0, m = 0;
    hal_sig_t *sig;
    hal_bit_t halbit;
    hal_s32_t hals32;
    hal_s32_t halu32;
    hal_float_t halfloat,delta;

    HAL_ASSERT(cg->magic ==  CGROUP_MAGIC);

    // scan for changes if needed
    monitor = (cg->group->userarg2 & (GROUP_REPORT_ON_CHANGE|GROUP_REPORT_CHANGED_MEMBERS))
	|| (cg->n_monitored > 0);

    // walk the group if either the whole group is to be monitored for changes
    // to cause a report, or only changed members should be included in a periodic
    // report.
    if (monitor) {
	RTAPI_ZERO_BITMAP(cg->changed, cg->n_members);
	for (i = 0; i < cg->n_members; i++) {
	    if (!((cg->member[i]->userarg1 &  MEMBER_MONITOR_CHANGE) ||
		  (cg->group->userarg2 &  GROUP_MONITOR_ALL_MEMBERS)))
		continue;
	    sig = SHMPTR(cg->member[i]->sig_ptr);
	    switch (sig->type) {
	    case HAL_BIT:
		halbit = *((hal_bit_t *) SHMPTR(sig->data_ptr));
		if (cg->tracking[m].b != halbit) {
		    nchanged++;
		    RTAPI_BIT_SET(cg->changed, i);
		    cg->tracking[m].b = halbit;
		}
		break;
	    case HAL_FLOAT:
		halfloat = *((hal_float_t *) SHMPTR(sig->data_ptr));
		delta = HAL_FABS(halfloat - cg->tracking[m].f);
		if (delta > hal_data->epsilon[cg->member[i]->eps_index]) {
		    nchanged++;
		    RTAPI_BIT_SET(cg->changed, i);
		    cg->tracking[m].f = halfloat;
		}
		break;
	    case HAL_S32:
		hals32 =  *((hal_s32_t *) SHMPTR(sig->data_ptr));
		if (cg->tracking[m].s != hals32) {
		    nchanged++;
		    RTAPI_BIT_SET(cg->changed, i);
		    cg->tracking[m].s = hals32;
		}
		break;
	    case HAL_U32:
		halu32 =  *((hal_u32_t *) SHMPTR(sig->data_ptr));
		if (cg->tracking[m].u != halu32) {
		    nchanged++;
		    RTAPI_BIT_SET(cg->changed, i);
		    cg->tracking[m].u = halu32;
		}
		break;
	    default:
		HALERR("BUG: detect_changes(%s): invalid type for signal %s: %d",
		       ho_name(cg->group), ho_name(sig), sig->type);
		return -EINVAL;
	    }
	    m++;
	}
	return nchanged;
    } else
	  return 1; // by default match
}


int hal_cgroup_report(hal_compiled_group_t *cg,
		      group_report_callback_t report_cb,
		      void *cb_data,
		      int force_all)
{
    int retval, i, reportall;

    if (!report_cb)
	return 0;
    if ((retval = report_cb(REPORT_BEGIN, cg, NULL,  cb_data)) < 0)
	return retval;

    // report all members if forced, there are no members with
    // change detect in the group,
    // or the group doesnt have the 'report changed members only' bit set
    reportall = force_all || (cg->n_monitored == 0) ||
	!(cg->group->userarg2 & GROUP_REPORT_CHANGED_MEMBERS);

    for (i = 0; i < cg->n_members; i++) {
	if (reportall || RTAPI_BIT_TEST(cg->changed, i))
	    if ((retval = report_cb(REPORT_SIGNAL, cg,
				    SHMPTR(cg->member[i]->sig_ptr),
				    cb_data)) < 0)
		return retval;
    }
    return report_cb(REPORT_END, cg, NULL,  cb_data);
}

int hal_cgroup_free(hal_compiled_group_t *cgroup)
{
    if (cgroup == NULL)
	return -ENOENT;
    if (cgroup->tracking)
	free(cgroup->tracking);
    if (cgroup->changed)
	free(cgroup->changed);
    if (cgroup->member)
	free(cgroup->member);
    free(cgroup);
    return 0;
}
#endif // ULAPI

void free_group_struct(hal_group_t * group)
{
    // delete all owned members first
    foreach_args_t args =  {
	.type = HAL_MEMBER,
	.owner_id = ho_id(group),
    };
    halg_foreach(0, &args, yield_free);
    halg_free_object(false, (hal_object_ptr)group);
}

