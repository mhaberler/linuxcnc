// Xenomai-only.

#include "config.h"

#if THREAD_FLAVOR_ID == RTAPI_XENOMAI_ID

#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

#include <native/task.h>
#include <native/timer.h>
#include <native/pipe.h>

#include <rtdm/rtcan.h>


MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("test component for RT-CAN device communication");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "rtcantest";

#if 0
typedef struct can_frame {
	/** CAN ID of the frame
	 *
	 *  See @ref CAN_xxx_FLAG "CAN ID flags" for special bits.
	 */
	can_id_t can_id;

	/** Size of the payload in bytes */
	uint8_t can_dlc;

	/** Payload data bytes */
	uint8_t data[8] __attribute__ ((aligned(8)));
} can_frame_t;
cansend can1  001#04.01.00.00.0f.ff.e7.00

/**
 * Structure containing a TMCL command and related data.
 *
 * @see TMCLComm
 */
typedef struct TMCLCommandStruct {
  uint8_t 	command;   /**< @ref TMCLComm "Command" */
  uint8_t 	type;  	   /**< Type */
  uint32_t	value; 	   /**< Value */
} TMCLCommand;

#endif

struct inst_data {
    int can_mask;
    int socket;
    struct can_frame frame;
    struct sockaddr_can to_addr;
    int motor;
    hal_u32_t *in_msgs;
    hal_u32_t *out_msgs;
    hal_u32_t *err_msgs;
    hal_s32_t *motor_cmd_pos;
    hal_s32_t prev_motor_cmd_pos;
};


static char *interface = "rtcan1";
RTAPI_IP_STRING(interface, "RT-CAN interface name, see /proc/rtcan");

static int can_id = 1;
RTAPI_IP_INT(can_id, "CAN ID to talk to");

static int can_mask = 0;
RTAPI_IP_INT(can_mask, "CAN mask");

static int motor = 0;
RTAPI_IP_INT(motor, "motor number");

static char *prefix = "can.0";
RTAPI_IP_STRING(prefix, "instance prefix");

static int can_funct(const void *arg, const hal_funct_args_t *fa)
{
    long period __attribute__((unused))  = fa_period(fa);
    struct inst_data *ip = arg;
    int ret;

    //cansend can1  001#04.01.00.00.0f.ff.e7.00
    // Grün Ist Motor Can Adresse , Blau = Befehl +Type, Rot sind Koordinaten  bzw Anzahl der Schritte
    //Siehe TMCL  Reference und Programming Manual Seite 15  3.4 „ Move to Position” im Relativ Mode

    // change detect on motor-cmd-pos
    hal_s32_t p = *(ip->motor_cmd_pos);
    if (p != ip->prev_motor_cmd_pos) {

	ip->frame.data[0] = 4; // instruction
	ip->frame.data[1] = 0; // type, 0=abs 1=rel
	ip->frame.data[2] = ip->motor; // motor/bank

	ip->frame.data[3] = p >> 3;
	ip->frame.data[4] = p >> 2;
	ip->frame.data[5] = p >> 1;
	ip->frame.data[6] = p;
	//	ip->frame.data[7] = 0; // chksum

	ip->frame.can_dlc = 7;

	ret = rt_dev_sendto(ip->socket,
			    (void *)&ip->frame,
			    sizeof(can_frame_t), 0,
			    (struct sockaddr *)&ip->to_addr,
			    sizeof(ip->to_addr));

	if (ret < 0) {
	    *(ip->err_msgs) += 1;
	    switch (ret) {
	    case -ETIMEDOUT:
		HALERR("rt_dev_send(to): timed out");
		break;
	    case -EBADF:
		HALERR("rt_dev_send(to): aborted because socket was closed");
		break;
	    default:
		HALERR("rt_dev_send: %s\n", strerror(-ret));
		break;
	    }
	} else
	    *(ip->out_msgs) += 1;
	ip->prev_motor_cmd_pos = *(ip->motor_cmd_pos);
    }
    return 0;
}

static int export_halobjs(struct inst_data *ip, int owner_id, const char *name)
{
    int ret;
    struct ifreq ifr;

    if ((ip->socket = rt_dev_socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
	HALERR("rt_dev_socket: %s\n", strerror(ip->socket));
	return -1;
    }

    strncpy(ifr.ifr_name, interface, IFNAMSIZ);
    if ((ret = rt_dev_ioctl(ip->socket, SIOCGIFINDEX, &ifr)) < 0)  {
	HALERR("rt_dev_ioctl: %s if=%s\n", strerror(-ret), interface);
	return ret;
    }

    memset(&ip->to_addr, 0, sizeof(ip->to_addr));
    ip->to_addr.can_ifindex = ifr.ifr_ifindex;
    ip->to_addr.can_family = AF_CAN;

    ip->frame.can_id = can_id;
    ip->can_mask = can_mask;
    ip->motor = motor;

    HALDBG("if=%s ifindex=%d can_id=%d can_mask=%d motor=%d",
	   interface,  ifr.ifr_ifindex, can_id, can_mask);

    if (hal_pin_u32_newf(HAL_OUT, &ip->in_msgs, owner_id, "%s.in-msgs", name))
	return -1;
    if (hal_pin_u32_newf(HAL_OUT, &ip->out_msgs, owner_id, "%s.out-msgs", name))
	return -1;
    if (hal_pin_u32_newf(HAL_OUT, &ip->err_msgs, owner_id, "%s.error-msgs", name))
	return -1;
    if (hal_pin_s32_newf(HAL_IN, &ip->motor_cmd_pos, owner_id, "%s.motor-cmd-pos", name))
	return -1;
    ip->prev_motor_cmd_pos = 0;

    hal_export_xfunct_args_t xtf = {
	.type = FS_XTHREADFUNC,
	.funct.x = can_funct,
	.arg = ip,
	.uses_fp = 0,
	.reentrant = 0,
	.owner_id = owner_id
    };
    if (hal_export_xfunctf(&xtf,"%s.funct", name))
	return -1;
    return 0;
}

static int init_comp(void)
{

    return 0;
}

static int exit_comp(void)
{

    return 0;
}

static int instantiate(const char *name, const int argc, const char**argv)
{
    struct inst_data *ip;

    // allocate a named instance, and some HAL memory for the instance data
    int inst_id = hal_inst_create(name, comp_id,
				  sizeof(struct inst_data),
				  (void **)&ip);
    if (inst_id < 0)
	return -1;

    // here ip is guaranteed to point to a blob of HAL memory of size sizeof(struct inst_data).
    hal_print_msg(RTAPI_MSG_ERR,"%s: inst=%s argc=%d\n",__FUNCTION__, name, argc);

    // these pins/params/functs will be owned by the instance, and can be separately exited
    int retval = export_halobjs(ip, inst_id, name);

    return retval;
}

static int delete(const char *name, void *inst, const int inst_size)
{
    struct inst_data *ip = inst;
    int ret;

    hal_print_msg(RTAPI_MSG_ERR,"%s inst=%s size=%d %p\n",
		  __FUNCTION__, name, inst_size, inst);
    if (ip->socket > -1) {
	ret = rt_dev_close(ip->socket);
	if (ret)
	    HALERR("rt_dev_close: %s\n", strerror(-ret));
    }
    return 0;
}

int rtapi_app_main(void)
{
    int retval;

    comp_id = hal_xinit(TYPE_RT, 0, 0, instantiate, delete, compname);
    if (comp_id < 0)
	return comp_id;

    retval = init_comp();
    if (retval)
	return retval;

    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    exit_comp();
    hal_exit(comp_id); // calls delete() on all insts
}


#else
#warning "creating null module for flavor " THREAD_FLAVOR_ID
#endif
