/********************************************************************
 * Copyright (C) 2012, 2013 Michael Haberler <license AT mah DOT priv DOT at>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ********************************************************************/

// helper to watch mutexes 

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <getopt.h>

#include "config.h"

#include "rtapi.h"
#include "rtapi_common.h"
#include "rtapi_global.h"
#include "rtapi/shmdrv/shmdrv.h"
#include "hal.h"
#include "hal_priv.h"

global_data_t *global_data;
rtapi_data_t *rtapi_data;
hal_data_t *hal_data;

int shmdrv_loaded;
long page_size;

int gm = -1;
int rm = -1;
int rrm = -1;
int hm = -1;
int mm = -1;
int gh = -1;

int rtapi_instance = 0;

struct timespec looptime = {
    .tv_sec = 0,
    .tv_nsec = 1000 * 1000 * 100,
};

int halmutex = -1;
int halheapmutex = -1;
int globalmutex = -1;
int globalheapmutex = -1;
int rtapimutex = -1;
int help = 0;
static struct option long_options[] = {
    { "help",  no_argument,                  &help, 1},
    { "instance",  required_argument,        0, 'i'},
    { "clearhalmutex",  no_argument,   &halmutex, 0},
    { "sethalmutex",  no_argument,     &halmutex, 1},
    { "clearhalheapmutex",  no_argument, &halheapmutex, 0},
    { "sethalheapmutex",  no_argument, &halheapmutex, 1},
    { "setglobalmutex",  no_argument,     &globalmutex,1},
    { "clearglobalmutex",  no_argument,     &globalmutex,0},
    { "setglobalheapmutex",  no_argument, &globalheapmutex, 1},
    { "clearglobalheapmutex",  no_argument, &globalheapmutex, 1},
    { "setrtapimutex",  no_argument,      &rtapimutex,1},
    { "clearrtapimutex",  no_argument,    &rtapimutex,0},
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    int globalkey,rtapikey,halkey,retval;
    int size;
    int quit = 0;

    page_size = sysconf(_SC_PAGESIZE);
    shmdrv_loaded = shmdrv_available();

    while (1) {
	int option_index = 0;
	int c = getopt_long (argc, argv, ":i:q",
			 long_options, &option_index);
	if (c == -1)
	    break;
	switch (c)	{
	    //	case -1:
	    // break;
	case 0:
	    break;
	case 'q':
	    quit++;
	    break;
	case 'i':
	    rtapi_instance = atoi(optarg);
	    break;
	case ':':
	    fprintf(stderr, "%s: option `-%c' requires an argument\n",
		    argv[0], optopt);
        break;
	case '?':
                printf("Unknown option\n"); break;
	}
    }

    globalkey = OS_KEY(GLOBAL_KEY, rtapi_instance);
    rtapikey = OS_KEY(RTAPI_KEY, rtapi_instance);
    halkey = OS_KEY(HAL_KEY, rtapi_instance);

    size = sizeof(global_data_t);
    retval = shm_common_new(globalkey, &size, 
			    rtapi_instance, (void **) &global_data, 0);
    if (retval < 0)
	 fprintf(stderr, "cannot attach global segment key=0x%x %s\n",
		globalkey, strerror(-retval));

     if (MMAP_OK(global_data)) {
	 if (global_data->magic != GLOBAL_READY) {
	     printf("global_data MAGIC wrong: %x %x\n", global_data->magic, GLOBAL_READY);
	 }
	 if (globalmutex > -1) {
	     printf("setting global mutex from %ld to %d\n",
		    global_data->mutex, globalmutex);
	     global_data->mutex = globalmutex;
	 }
	 if (globalheapmutex > -1) {
	     printf("setting global heapmutex from %ld to %d\n",
		    global_data->heap.mutex, globalheapmutex);
	     global_data->heap.mutex = globalheapmutex;
	 }
    }

    size = sizeof(rtapi_data_t);
    retval = shm_common_new(rtapikey,  &size, 
			    rtapi_instance, (void **) &rtapi_data, 0);
    if (retval < 0)
	 fprintf(stderr, "cannot attach rtapi segment key=0x%x %s\n",
		rtapikey, strerror(-retval));

    if (MMAP_OK(rtapi_data) && (rtapi_data->magic != RTAPI_MAGIC)) {
	    printf("rtapi_data MAGIC wrong: %x\n", rtapi_data->magic);
    } else {
	if (rtapimutex > -1) {
	    printf("setting rtapi mutex from %ld to %d\n",
		   rtapi_data->mutex, rtapimutex);
	    rtapi_data->mutex = rtapimutex;
	}
    }

    if (MMAP_OK(global_data)) {
	size = global_data->hal_size;
	// global_data is needed for actual size of the HAL data segment
	retval = shm_common_new(halkey, &size, 
				rtapi_instance, (void **) &hal_data, 0);
	if (retval < 0) {
	    fprintf(stderr, "cannot attach hal segment key=0x%x %s\n",
		    halkey, strerror(-retval));
	    goto noglobal;
	}
	if (MMAP_OK(hal_data) && (hal_data->version != HAL_VER)) {
	    printf("hal_data HAL_VER wrong: %x\n", hal_data->version);
	    goto noglobal;
	}
	if (globalmutex > -1)
	    global_data->mutex = globalmutex;
    noglobal:;
    }
    if (MMAP_OK(hal_data)) {
	 if (halmutex > -1) {
	     printf("setting hal mutex from %ld to %d\n",
		    hal_data->mutex, halmutex);
	     hal_data->mutex = halmutex;
	 }
	 if (halheapmutex > -1) {
	     printf("setting hal heapmutex from %ld to %d\n",
		    hal_data->heap.mutex, halheapmutex);
	     hal_data->heap.mutex = halheapmutex;
	 }
    }
    if (quit)
	exit(0);

    if (!(MMAP_OK(global_data) || MMAP_OK(rtapi_data) || MMAP_OK(hal_data))) {
	printf("nothing to attach to!\n");
	exit(1);
    }

    do {
	if (nanosleep(&looptime, &looptime))
	    break;

	if (MMAP_OK(global_data) && (global_data->mutex != gm)) {
	    printf("global_data->mutex: %ld\n", global_data->mutex);
	    gm = global_data->mutex;
	}
	if (MMAP_OK(rtapi_data) && (rtapi_data->ring_mutex != rrm)) {
	    printf("rtapi_data->ring_mutex: %ld\n", rtapi_data->ring_mutex);
	    rrm = rtapi_data->ring_mutex;
	}
	if (MMAP_OK(rtapi_data) && (rtapi_data->mutex != rm)) {
	    printf("rtapi_data->mutex: %ld\n", rtapi_data->mutex);
	    rm = rtapi_data->mutex;
	}
	if (MMAP_OK(hal_data) && (hal_data->mutex != hm)) {
	    printf("hal_data->mutex: %ld\n", hal_data->mutex);
	    hm = hal_data->mutex;
	}
	if (MMAP_OK(hal_data) && (hal_data->heap.mutex != mm)) {
	    printf("hal_data->heap.mutex: %ld\n", hal_data->heap.mutex);
	    mm = hal_data->heap.mutex;
	}
	if (MMAP_OK(global_data) && (global_data->heap.mutex != gh)) {
	    printf("global_data->heap.mutex: %ld\n", global_data->heap.mutex);
	    gh = global_data->heap.mutex;
	}

    } while (1);

    exit(0);
}
