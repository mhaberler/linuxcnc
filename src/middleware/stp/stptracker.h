#ifndef STPTRACKER_H
#define STPTRACKER_H


#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>

    typedef struct _mvar mvar_t;
    typedef struct _mgroup mgroup_t;
    typedef struct _msource msource_t;
    typedef struct _mservice mservice_t;

    typedef int (*group_callback)(mgroup_t *g, void *arg);

    mvar_t *stmon_double(const char *name, double *dref);
    mvar_t *stmon_s32(const char *name, int *sref);
    mvar_t *stmon_u32(const char *name, unsigned *uref);
    mvar_t *stmon_bool(const char *name,  bool *bref);
    mvar_t *stmon_string(const char *name, const void **sref);
    mvar_t *stmon_blob(const char *name, const void **bref, size_t *bsize);

    typedef int (group_update_complete_cb) (mservice_t *tracker, mgroup_t *group);

    mgroup_t *stmon_group_new(const char *name, group_complete_cb callback);
    int stmon_addvar(mgroup_t *g, mvar_t *v);
    int stmon_set_callback(mgroup_t *g, group_update_complete_cb *cb, void *args);


    msource_t *stmon_add_source(mservice_t *t,
				int protocol, int minversion,
				int service_id);


    int stmon_addgroup(mservice_t *t, mgroup_t *g);

    mtracker_t *stmon_tracker_new(int discovery_port);

    /* int stmon_tracker_addservice(mservice_t *t, mservice_t *g); */
    /* int stmon_tracker_run(mservice_t *t); */
    /* int stmon_tracker_stop(mservice_t *t); */
    /* int stmon_tracker_updates_seen(mservice_t *t, mgroup_t *g); */


    /* // introspection */
    /* typedef int (*stmon_group_callback_t)(mgroup_t *group,  void *cb_data); */
    /* int stmon_foreach_group(const char *groupname, */
    /* 			   stmon_group_callback_t callback, void *cb_data); */

    /* typedef int (*stmon_member_callback_t)(int level, mgroup_t *group, */
    /* 					  mvar_t *member, void *cb_data); */
    /* int stmon_foreach_member(const char *group, */
    /* 			    stmon_member_callback_t callback, void *cb_data, int flags); */

    // library error handler callback !!

#ifdef __cplusplus
}
#endif

#endif // STPTRACKER_H
