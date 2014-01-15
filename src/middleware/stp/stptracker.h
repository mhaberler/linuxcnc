#ifndef STPTRACKER_H
#define STPTRACKER_H


#ifdef __cplusplus
extern "C" {
#endif
#include <czmq.h>
#include <stddef.h>

    typedef struct _mvar mvar_t;
    typedef struct _mgroup mgroup_t;
    typedef struct _msource msource_t;
    typedef struct _mtracker mtracker_t;

    // the tracker object. A thread
    // ctx == NULL - create own context
    mtracker_t *stmon_tracker_new(zctx_t *ctx);

    // add a source
    int stmon_tracker_add_source(mtracker_t *t, msource_t *s);

    // run/stop a tracker
    int stmon_tracker_run(mtracker_t *t);
    int stmon_tracker_stop(mtracker_t *t);

    // create monitored variable  objects
    mvar_t *stmon_double(const char *name, double *dref);
    mvar_t *stmon_s32(const char *name, int *sref);
    mvar_t *stmon_u32(const char *name, unsigned *uref);
    mvar_t *stmon_bool(const char *name,  bool *bref);
    mvar_t *stmon_string(const char *name, const void **sref);
    mvar_t *stmon_blob(const char *name, const void **bref, size_t *bsize);

    // if non-NULL: called when a group has been updated
    typedef int (group_update_complete_cb) (mtracker_t *tracker, mgroup_t *group, void *args);

    // create a named group
    mgroup_t *stmon_group_new(const char *name, group_update_complete_cb callback, void *callback_arg);

    // add monitored variable to group
    int stmon_add_var(mgroup_t *g, mvar_t *v);

    //--------------------

    // create a source for updates. A socket + set of groups.
    msource_t *stmon_source_new(mtracker_t *t);
    int stmon_source_add_origin(msource_t *s, const char *uri);
    int stmon_source_add_group(msource_t *s, mgroup_t *g);



#ifdef __cplusplus
}
#endif

#endif // STPTRACKER_H

    /* int stmon_tracker_updates_seen(mservice_t *t, mgroup_t *g); */
    /* // introspection */
    /* typedef int (*stmon_group_callback_t)(mgroup_t *group,  void *cb_data); */
    /* int stmon_foreach_group(const char *groupname, */
    /* 			   stmon_group_callback_t callback, void *cb_data); */

    /* typedef int (*stmon_member_callback_t)(int level, mgroup_t *group, */
    /* 					  mvar_t *member, void *cb_data); */
    /* int stmon_foreach_member(const char *group, */
    /* 			    stmon_member_callback_t callback, void *cb_data, int flags); */

    //int stmon_set_callback(mgroup_t *g, group_update_complete_cb *cb, const void *args);

    // library error handler callback !!
