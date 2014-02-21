#ifndef STPTALKER_H
#define STPTALKER_H


#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <czmq.h>

    // delta for double values to consider 'changed'
    // can be set by strack_set_epsilon()
    #define STP_DEFAULT_EPSILON 0.00001

    extern int stp_debug;

    typedef struct _stvar    stvar_t;
    typedef struct _stgroup  stgroup_t;
    typedef struct _sttalker sttalker_t;

    stvar_t *strack_double(const char *name, double *dref);
    stvar_t *strack_s32(const char *name, int *sref);
    stvar_t *strack_u32(const char *name, unsigned *uref);
    stvar_t *strack_bool(const char *name, bool *bref);
    stvar_t *strack_string(const char *name, const void **sref);
    stvar_t *strack_blob(const char *name, const void **bref, size_t *bsize);

    // mark blob/string as changed
    void  strack_variable_changed(stvar_t *v);

    stgroup_t *strack_group_new(const char *name, int interval);
    stgroup_t *strack_find_group(sttalker_t *self, const char *groupname);

    int        strack_group_add(stgroup_t *g, stvar_t *v);
    //    int strack_group_update(sttalker_t *t, stgroup_t *g, bool full);

    typedef int (subscribe_cb) (sttalker_t *self, zframe_t *msg);

    sttalker_t *strack_talker_new();
    int strack_talker_add(sttalker_t *t, stgroup_t *g);

    void strack_set_epsilon(sttalker_t *t, double epsilon);
    void strack_set_empty_updates(sttalker_t *t, bool empty);

    int strack_talker_run(sttalker_t *self,
			  zctx_t *ctx, const char *uri,
			  int interval, int beacon_port,
			  int stp_service,
			  subscribe_cb callback);

    int strack_talker_update(sttalker_t *self, const char *groupname, bool full);

    int strack_talker_exit(sttalker_t *t);


#ifdef __cplusplus
}
#endif

#endif // STPTALKER_H
