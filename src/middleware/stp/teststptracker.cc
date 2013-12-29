#include <stdio.h>
#include <string>
#include <stptracker.h>
#include <discovery.h>

double xval;
int fooval;
bool flagval;

double x,y,z;

const char *filename = "foo.txt";

char blob[100];
size_t blob_size = sizeof(blob);
const void *blob_ptr = blob;

int group_updated(mgroup_t *g, void *arg)
{
    return 0;
}


int main(int argc, const char **argv)
{
    mgroup_t *g1 = stmon_group_new("group1", NULL);

    stmon_addvar(g1, stmon_double("xval", &xval));
    stmon_addvar(g1, stmon_s32("fooval", &fooval));
    stmon_addvar(g1, stmon_bool("flagval", &flagval));
    stmon_addvar(g1, stmon_string("filename", (const void **) &filename));
    stmon_addvar(g1, stmon_blob("ablob", &blob_ptr, &blob_size));

    mgroup_t *g2 = stmon_group_new("group2", NULL);
    stmon_addvar(g2, stmon_double("x", &x));
    stmon_addvar(g2, stmon_double("y", &y));
    stmon_addvar(g2, stmon_double("z", &z));

    stmon_set_callback(g2, group_updated, "foo");

    mtracker_t* t = stmon_tracker_new(BEACON_PORT);

    msource_t *s = stmon_add_source(t, PROTOCOL, VERSION, uri);
    stmon_source_addgroup(s, g1);
    stmon_source_addgroup(s, g2);

    stmon_tracker_run(t);
    sleep(10);

    stmon_service_stop(t);


    //nt service, const char *uri, const char **groups);
    return 0;
}
