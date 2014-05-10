#include <stdio.h>
#include <czmq.h>
#include <getopt.h>

#include "config.h"
#include "mk-zeroconf.hh"
#include "syslog_async.h"

static const char *progname;
zservice_t zs;

struct _zconfig_t {
    char *name;                 //  Property name if any
    char *value;                //  Property value, if any
    struct _zconfig_t
        *child,                 //  First child if any
        *next,                  //  Next sibling if any
        *parent;                //  Parent if any
    zlist_t *comments;          //  Comments if any
};

void printcomments(zconfig_t *self)
{
    if (self->comments) {
        char *comment = (char *) zlist_first (self->comments);
        while (comment) {
            fprintf (stderr, "---#%s\n", comment);
            comment = (char *) zlist_next (self->comments);
        }
        //  Blank line after comments is nice
        fprintf (stderr, "\n");
    }
}

static void usage(int argc, char **argv) 
{
    printf("Usage:  %s [options]\n", argv[0]);
}

static struct option long_options[] = {
    {"help",  no_argument,          0, 'h'},
    {"foreground",  no_argument,    0, 'F'},
    {"instance", required_argument, 0, 'I'},
    {"drivers",   required_argument, 0, 'D'},
    {"uri",   required_argument,    0, 'U'},
    {"debug",   no_argument,    0, 'd'},
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    int c;
    progname = argv[0];

    openlog_async(progname, LOG_NDELAY, LOG_LOCAL1);
    setlogmask_async(LOG_UPTO(LOG_DEBUG));

#if 0
    while (1) {
	int option_index = 0;
	int curind = optind;
	c = getopt_long (argc, argv, "hH:m:I:f:r:U:NFd",
			 long_options, &option_index);
	if (c == -1)
	    break;

	switch (c)	{

	case 'd':
	    debug++;
	    break;

	case 'D':
	    use_drivers = 1;
	    break;

	case 'F':
	    foreground = 1;
	    rtapi_set_msg_handler(stderr_rtapi_msg_handler);
	    break;

	case 'I':
	    instance_id = atoi(optarg);
	    break;

	case 'f':
	    if ((flavor = flavor_byname(optarg)) == NULL) {
		fprintf(stderr, "no such flavor: '%s' -- valid flavors are:\n", 
			optarg);
		flavor_ptr f = flavors;
		while (f->name) {
		    fprintf(stderr, "\t%s\n", f->name);
		    f++;
		}
		exit(1);
	    }
	    break;

	case 'U':
	    z_uri = optarg;
	    break;

	case '?':
	    if (optopt)  fprintf(stderr, "bad short opt '%c'\n", optopt);
	    else  fprintf(stderr, "bad long opt \"%s\"\n", argv[curind]);
	    //usage(argc, argv);
	    exit(1);
	    break;

	default:
	    usage(argc, argv);
	    exit(0);
	}
    }
#endif
    int opt_zeroconf = 1;
#ifdef HAVE_AVAHI
    void *avahi = NULL;
#endif

#ifdef HAVE_AVAHI
    /* Zeroconf registration */
    if (opt_zeroconf) {

	zs.name = "foo service on host bar";
	zs.proto =  AVAHI_PROTO_INET;
	zs.interface = AVAHI_IF_UNSPEC;
	zs.port = 4711;
	zs.type = MACHINEKIT_DNS_SERVICE_TYPE;
	zs.txt = NULL;
	zs.txt = avahi_string_list_add(zs.txt, "foo=bar");
	zs.txt = avahi_string_list_add(zs.txt, "blah=fasel");
	zs.txt = avahi_string_list_add_pair(zs.txt, "key", "value");
	zs.txt = avahi_string_list_add_printf(zs.txt, "version=%s", GIT_VERSION);

        if (!(avahi = mk_zeroconf_register(&zs)))
            return -1; //EXIT_CONNECT_FAILED;
    }
#endif
    sleep(10);
#ifdef HAVE_AVAHI
    /* Remove zeroconf registration */
    if (opt_zeroconf) {
        if (mk_zeroconf_unregister(avahi) != 0)
            return -1; // EXIT_CONNECT_FAILED;
    }
#endif

    fprintf(stderr, "hello world.\n");
    int verbose = 1;
    
    zconfig_t *root = zconfig_new ("root", NULL);
    zconfig_set_comment (root, "root comment");

    zconfig_t *section, *item;

    item = zconfig_new ("atroot", root);
    zconfig_set_value (item, "some@random.com");
    zconfig_set_comment (item, "an atroot comment, pid=%d", getpid());

    section = zconfig_new ("section", root);
    zconfig_set_comment (section, "a section comment");
    item = zconfig_new ("insection", section);
    zconfig_set_value (item, "value");
    zconfig_set_comment (item, "an insection comment, pid=%d", getpid());


    zconfig_save (root, "-");
#if 0
 printf (" * zconfig: ");
    
    //  @selftest
    //  Create temporary directory for test files
#   define TESTDIR "TESTDIR"
    zsys_dir_create (TESTDIR);
    
    zconfig_t *root = zconfig_new ("root", NULL);
    zconfig_t *section, *item, *subsection;
    
    section = zconfig_new ("headers", root);
    zconfig_set_comment (section, "a root comment!!!");

    item = zconfig_new ("email", section);
    zconfig_set_value (item, "some@random.com");

    item = zconfig_new ("name", section);
    zconfig_set_value (item, "Justin Kayce");

    subsection = zconfig_new ("subsection", section);
    zconfig_set_comment (subsection, "ein kommentar auf subsection");

    item = zconfig_new ("subkey", subsection);
    zconfig_set_value (item, "blahfasel");
    zconfig_set_comment (item, "ein kommentar aufblahfasel");
    //    printf ("blahfasel comment: %s\n", zconfig_comments(item));
    zconfig_set_comment (item, "NOCH EIN kommentar aufblahfasel");
    //printf ("blahfasel comment itzo: %s\n", zconfig_comments(item));
    printf ("blahfasel comments\n");
    printcomments(item);
    printf ("root comments\n");
    printcomments(root);

    char *fasel = zconfig_resolve (root, "/headers/subsection", "defaultfasel");
    printf (" * fasel: %s\n", fasel);


    zconfig_put (root, "/curve/secret-key", "Top Secret");
    zconfig_set_comment (root, "   CURVE certificate");
    zconfig_set_comment (root, "   -----------------");
    assert (zconfig_comments (root));
    zconfig_save (root, TESTDIR "/test.cfg");
    zconfig_destroy (&root);
    root = zconfig_load (TESTDIR "/test.cfg");
    if (verbose)
        zconfig_save (root, "-");
        
    char *email = zconfig_resolve (root, "/headers/email", NULL);
    assert (email);
    assert (streq (email, "some@random.com"));
    char *passwd = zconfig_resolve (root, "/curve/secret-key", NULL);
    assert (passwd);
    assert (streq (passwd, "Top Secret"));

    zconfig_save (root, TESTDIR "/test.cfg");
    zconfig_destroy (&root);

    // Test improperly terminated config files
    const char *chunk_data = "section\n    value = somevalue";
    zchunk_t *chunk = zchunk_new (chunk_data, strlen (chunk_data));
    assert (chunk);
    root = zconfig_chunk_load (chunk);
    assert(root);
    char *value = zconfig_resolve (root, "/section/value", NULL);
    assert (value);
    assert (streq (value, "somevalue"));
    zconfig_destroy (&root);
    zchunk_destroy (&chunk);
    
    //  Delete all test files
    // zdir_t *dir = zdir_new (TESTDIR, NULL);
    // zdir_remove (dir, true);
    // zdir_destroy (&dir);
    //  @end
    zconfig_t *test; 
    test = zconfig_load (TESTDIR "/input.cfg");
    zconfig_save (test, "-");
#endif

    printf ("OK\n");
}
