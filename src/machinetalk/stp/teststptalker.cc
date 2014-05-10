#include <stdio.h>
#include <string>
#include <stptalker.h>
#include <machinetalk/generated/message.pb.h>

double xval = 6.28;
int fooval = 4711;
bool flagval = true;
const char *filename = "foo.txt";

char blob[100];
size_t blob_size = sizeof(blob);
const void *blob_ptr = blob;

double x = 3.14;
double y = 2.718;
double z = 42.42;

static zctx_t *ctx;

int subscribe(sttalker_t *self, zframe_t *msg)
{
    unsigned char *s = zframe_data (msg);
    fprintf(stderr, "subscribe recv: %d '%.*s'\n",
	    *s,  (int) zframe_size (msg) - 1, s+1);
    if (*s)
	strack_talker_update(self,NULL, true);
    return 0;
}

int main(int argc, const char **argv)
{
    ctx = zctx_new ();

    sttalker_t *t = strack_talker_new();
    strack_set_epsilon(t, 0.001);
    strack_set_empty_updates(t, false);

    stgroup_t *g1 = strack_group_new("group1", 100);
    strack_group_add(g1, strack_double("xval", &xval));
    strack_group_add(g1, strack_s32("fooval", &fooval));
    strack_group_add(g1, strack_bool("flagval", &flagval));
    strack_group_add(g1, strack_string("filename", (const void **) &filename));
    strack_group_add(g1, strack_blob("ablob", &blob_ptr, &blob_size));

    stgroup_t *g2 = strack_group_new("group2", 100);
    strack_group_add(g2, strack_double("x", &x));
    strack_group_add(g2, strack_double("y", &y));
    strack_group_add(g2, strack_double("z", &z));

    strack_talker_add(t, g1);
    strack_talker_add(t, g2);

    // use ephemeral port, run beacon, explicit callback, no timer
    //strack_talker_run(t, ctx,"tcp://*:*", 10001, 10042, subscribe);
    //strack_talker_update(t,NULL, true);

    //strack_talker_run(t, ctx,"tcp://*:*", 1000, 10042, subscribe);

    // explicit uri, run beacon, subscribe callback, 1s timer
    //strack_talker_run(t, ctx, "tcp://127.0.0.1:6042", 1000, 10042, subscribe);
    strack_talker_run(t, ctx, "tcp://127.0.0.1:6042", 1000, 10042,
		      pb::ST_STP_HALGROUP, subscribe);

    sleep(3000);
    strack_talker_exit(t);
    sleep(1);

    //---- manual mode --
    // void subscribe_callback(sttalker_t *self, bool new, const char *topic);
    // use ephemeral port
    // no timed updates
    // service discovery on 10042
    // strack_talker_run(ctx,"tcp://*:*", -1, 10042, subscribe_callback);

    // manual update
    // strack_talker_update(t, full/incremental);
    // exits thread


    //---- automatic mode --
    // 1000msec, service discovery
    // strack_talker_run(t, ctx, "tcp://127.0.0.1:6042", 1000, 10042, NULL);

    // strack_talker_stop(t); // exits thread

    //----
    return 0;
}
