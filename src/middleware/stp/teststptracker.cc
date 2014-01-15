#include <stdio.h>
#include <unistd.h>
#include <string>
#include <stptracker.h>
#include <stptracker-private.h>

double xval;
int fooval;
bool flagval;

double x,y,z;

const char *filename = "foo.txt";

char blob[100];
size_t blob_size = sizeof(blob);
const void *blob_ptr = blob;

int group_updated(struct _mtracker *tracker, struct _mgroup *group, void *args)
{
    fprintf(stderr, "group %s update complete\n", group->name);
    return 0;
}


int main(int argc, const char **argv)
{
    mgroup_t *g1 = stmon_group_new("group1", NULL, NULL);

    stmon_add_var(g1, stmon_double("xval", &xval));
    stmon_add_var(g1, stmon_s32("fooval", &fooval));
    stmon_add_var(g1, stmon_bool("flagval", &flagval));
    stmon_add_var(g1, stmon_string("filename", (const void **) &filename));
    stmon_add_var(g1, stmon_blob("ablob", &blob_ptr, &blob_size));

    mgroup_t *g2 = stmon_group_new("group2",  group_updated, NULL);
    stmon_add_var(g2, stmon_double("x", &x));
    stmon_add_var(g2, stmon_double("y", &y));
    stmon_add_var(g2, stmon_double("z", &z));

    mtracker_t* trk = stmon_tracker_new(NULL);
    msource_t *src = stmon_source_new(trk);

    assert(stmon_source_add_origin(src,  "tcp://127.0.0.1:6042") == 0);
    assert(stmon_source_add_group(src,  g1) == 0);
    assert(stmon_source_add_group(src,  g2) == 0);

    assert(stmon_tracker_add_source(trk, src) == 0);

    stmon_tracker_run(trk);
    sleep(10);
    stmon_tracker_stop(trk);
    return 0;
}
