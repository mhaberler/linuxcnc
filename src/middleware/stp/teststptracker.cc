#include <stdio.h>
#include <unistd.h>
#include <string>

#define _DEBUG
#include <stptracker.h>
#include <stptracker-private.h>

double notupdated;
double typoval;
double xval;
int fooval;
bool flagval;

double x,y,z;

double xcmd,ycmd,zcmd;
double xfb,yfb,zfb;

const char *filename;
size_t blob_size;
const void *blob_ptr;


std::string vstr(_mvar_t *v)
{
    char buf[256];

    switch (v->type) {
    default:
	return "invalid type";

    case pb::HAL_BIT:
	snprintf(buf, sizeof(buf), "%s", *(v->value.b) ? "true" : "false");
	break;
    case pb::HAL_FLOAT:
	snprintf(buf, sizeof(buf), "%f",  *(v->value.f));
	break;
    case pb::HAL_S32:
	snprintf(buf, sizeof(buf), "%d",  *(v->value.s));
	break;
    case pb::HAL_U32:
	snprintf(buf, sizeof(buf), "%d",  *(v->value.u));
	break;
    case pb::STRING:
	{
	    stpblob_t *bp = &v->value.blob;
	    snprintf(buf, sizeof(buf), "%s", (char *)*(bp->bref));
	}
	break;
    case pb::BYTES:
	stpblob_t *bp = &v->value.blob;
	snprintf(buf, sizeof(buf), "blob(%d)", *(bp->bsize));
	break;
    }
    return std::string(buf);
}


void var_changed(_mvar_t *var, void *args)
{
    stp_debug("------------- var %s changed: %s\n", var->name, vstr(var).c_str());
}

int group_updated(_mgroup_t *group, void *args)
{
    stp_debug("group %s updated args=%p\n", group->name, args);

    for (name_iterator ni = group->byname.begin();
	 ni != group->byname.end(); ni++) {
	_mvar_t *t = ni->second;
	stp_debug("var %s  %s\n", t->name, vstr(t).c_str());
    }
    return 0;
}

void showvars(void)
{
    fprintf(stderr, "x=%f y=%f z=%f fooval=%d flagval=%d\n",
	    x,y,z,fooval,flagval );
    fprintf(stderr, "filename=%s blobsize=%d\n",
	    filename ? filename : "NULL", blob_size);
}

void display(int secs)
{
    for (int i = 0; i < secs; i++) {
	showvars();
	sleep(2);
    }
}

int main(int argc, const char **argv)
{
    stp_set_log_level(-1, NULL);

    mgroup_t *g1 = stmon_group_new("group1", NULL, NULL);

    mvar_t *v = stmon_double("xval", &xval);
    stmon_add_var(g1, v);
    stmon_add_var(g1, stmon_s32("fooval", &fooval));
    stmon_add_var(g1, stmon_bool("flagval", &flagval));
    stmon_add_var(g1, stmon_string("filename", (const void **) &filename));
    stmon_add_var(g1, stmon_blob("ablob", &blob_ptr, &blob_size));

    mgroup_t *g2 = stmon_group_new("group2",  group_updated, NULL);
    stmon_add_var(g2, stmon_double("x", &x));
    stmon_add_var(g2, stmon_double("y", &y));
    stmon_add_var(g2, stmon_double("z", &z));
    stmon_add_var(g2, stmon_double("notupdated", &notupdated));

    stmon_set_change_callback(v, var_changed, NULL);

    mgroup_t *g3 = stmon_group_new("atypo",  group_updated, NULL);
    stmon_add_var(g3, stmon_double("typoval", &typoval));

    mgroup_t *g4 = stmon_group_new("axis-pos",  group_updated, NULL);

    mvar_t *vxcmd = stmon_double("xcmd", &xcmd);
    stmon_set_change_callback(vxcmd, var_changed, NULL);

    stmon_add_var(g4, vxcmd);
    stmon_add_var(g4, stmon_double("ycmd", &ycmd));
    stmon_add_var(g4, stmon_double("zcmd", &zcmd));
    stmon_add_var(g4, stmon_double("xfb", &xfb));
    stmon_add_var(g4, stmon_double("yfb", &yfb));
    stmon_add_var(g4, stmon_double("zfb", &zfb));



    mtracker_t* trk = stmon_tracker_new(NULL);
    msource_t *src = stmon_source_new(trk);

    assert(stmon_source_add_origin(src,  "tcp://127.0.0.1:6042") == 0);
    assert(stmon_source_add_group(src,  g1) == 0);
    assert(stmon_source_add_group(src,  g2) == 0);
    assert(stmon_source_add_group(src,  g3) == 0);
    assert(stmon_source_add_group(src,  g4) == 0);

    assert(stmon_tracker_add_source(trk, src) == 0);

    showvars();
    stp_notice("start tracker\n");

    stmon_tracker_run(trk);
    sleep(200);
    display(1);
    stmon_tracker_stats(trk);
    stmon_tracker_stop(trk);
    return 0;
}
