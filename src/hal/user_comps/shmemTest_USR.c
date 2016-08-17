/****************************************************
To compile:

 gcc -o shmemTest_USR.o -I. -I/usr/include/xenomai -D_GNU_SOURCE -D_REENTRANT -D__XENO__ -DTHREAD_FLAVOR_ID=2 \
-DRTAPI -D_GNU_SOURCE -D_FORTIFY_SOURCE=0 -D__MODULE__ -mieee-fp -I/home/$USER/machinekit/include \ 
-fPIC -g -funwind-tables -URTAPI -U__MODULE__ -DULAPI -Os -c shmemTest_USR.c

gcc -o shmemTest_USR shmemTest_USR.o -Wl,-rpath,/home/$USER/machinekit/lib -L/home/$USER/machinekit/lib -llinuxcnchal

cp shmemTest_USR ~/machinekit/bin

******************************************************/


#include <stdio.h>
#include <unistd.h>
#include <string.h>


#include "rtapi.h"
#include "hal.h"

// as in https://github.com/sittner/linuxcnc-ethercat/blob/master/src/lcec_conf.h#L26-L27
#define LCEC_CONF_SHMEM_KEY   0x00B572C7
#define LCEC_CONF_SHMEM_MAGIC 0x006ED5A3

char *modname = "shmemTest_USR";
int hal_comp_id;
int shmem_id;

void main(void){ 
   

   // initialize component
  hal_comp_id = hal_init(modname);
  if (hal_comp_id < 1) {
    fprintf(stderr, "%s: ERROR: hal_init failed\n", modname);
    return;
  }

// as in https://github.com/sittner/linuxcnc-ethercat/blob/master/src/lcec_conf.c#L318
   shmem_id = rtapi_shmem_new(LCEC_CONF_SHMEM_KEY, hal_comp_id, 100);
   rtapi_print_msg (RTAPI_MSG_ERR,"0 allocation, non RT component, shmem_id %d\n", shmem_id);

   //hal_exit(hal_comp_id);
   hal_ready (hal_comp_id);
   return;
   
}
