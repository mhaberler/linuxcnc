/******************************************************
To compile

gcc -o shmemTest.o -I. -I/usr/include/xenomai -D_GNU_SOURCE -D_REENTRANT \ 
-D__XENO__ -DTHREAD_FLAVOR_ID=2 -DRTAPI -D_GNU_SOURCE -D_FORTIFY_SOURCE=0 \
-D__MODULE__ -mieee-fp -I/home/$USER/machinekit/include -fPIC -g -funwind-tables  -Os -c shmemTest.c

gcc -shared -o shmemTest.so shmemTest.o  -Wl,-rpath,/home/$USER/machinekit/lib -L/home/$USER/machinekit/lib -llinuxcnchal

cp shmemTest.so ~/machinekit/rtlib/xenomai/
*****************************************************/





#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"


// as in https://github.com/sittner/linuxcnc-ethercat/blob/master/src/lcec_conf.h#L26-L27
#define LCEC_CONF_SHMEM_KEY   0x00B572C7
#define LCEC_CONF_SHMEM_MAGIC 0x006ED5A3


static int comp_id = -1;

int rtapi_app_main(void) {

 // connect to the HAL
  if ((comp_id = hal_init ("shmemTest")) < 0) {
     rtapi_print_msg(RTAPI_MSG_ERR, "hal_init() failed\n");
     hal_exit (comp_id);
     return 0;
  }

  int shmem_id;
   
// as in https://github.com/sittner/linuxcnc-ethercat/blob/master/src/lcec_main.c#L406
   shmem_id = rtapi_shmem_new(LCEC_CONF_SHMEM_KEY, comp_id, 10);
   rtapi_print_msg (RTAPI_MSG_ERR,"First allocation, RT component, shmem_id %d\n", shmem_id);

// as in https://github.com/sittner/linuxcnc-ethercat/blob/master/src/lcec_main.c#L423
   int statusCode = rtapi_shmem_delete(shmem_id, comp_id);
   rtapi_print_msg (RTAPI_MSG_ERR,"First deletion, RT component, statusCode %d\n", statusCode);

// as in https://github.com/sittner/linuxcnc-ethercat/blob/master/src/lcec_main.c#L426
   shmem_id = rtapi_shmem_new(LCEC_CONF_SHMEM_KEY, comp_id, 100);
   rtapi_print_msg (RTAPI_MSG_ERR,"Second allocation, RT component, shmem_id %d\n", shmem_id);

   statusCode = rtapi_shmem_delete(shmem_id, comp_id);
   rtapi_print_msg (RTAPI_MSG_ERR,"Second deletion, RT component, statusCode %d\n", statusCode);

   hal_ready (comp_id);
   return 0;

}

void rtapi_app_exit(void) {
  hal_exit(comp_id);
}

