// all tests which require a running RT instance.
// does not fork.

#include <stdlib.h>
#include <check.h>

#include "machinetalk-suite.h"
#include "hal-suite.h"
#include "atomic-suite.h"
#include "ring-suite.h"

#ifdef EXAMPLE_TEST
#include "helloworld-suite.h"
#endif


int comp_id;

void hal_setup(void)
{
    comp_id = hal_init("testme");
    hal_ready(comp_id);
    if (hal_data == NULL) {
	fprintf(stderr, "ERROR: could not connect to HAL\n");
	exit(1);
    }
}

void hal_teardown(void)
{
    if (comp_id > 0) {
	hal_exit(comp_id);
	comp_id = 0;
    }
}

int main(int argc, char **argv)
{
    int number_failed;
    Suite *s;
    SRunner *sr;
    hal_setup();

    s = hal_suite();
    sr = srunner_create(s);
    srunner_set_fork_status (sr, CK_NOFORK);
    srunner_add_suite(sr, ring_suite());
    srunner_add_suite(sr, machinetalk_suite());
    srunner_add_suite(sr, atomic_suite());

#ifdef EXAMPLE_TEST
    srunner_add_suite(sr, hello_world_suite());
#endif

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    hal_teardown();
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
