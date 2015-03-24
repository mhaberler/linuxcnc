/*
  a HAL Modbus driver supporting several slave devices (requires an RS485 port)
  example modbus handling for estun PRONET AC Servo Drive

  Michael Haberler 3/2012
*/

#ifndef MAXSLAVES
#define MAXSLAVES 10
#endif
#ifndef MODNAME
#define MODNAME "generic"
#endif

// AC Servo PRONET series User’s Manual V. 1.04
#define RO_FORMER_ALARMS       0x7F1 // former 10 alarms, up to 07FA
#define RO_SPEED_FEEDBACK      0x806
#define RO_SPEED_CMD           0x807
#define RO_LOAD_PCT            0x815
#define RO_OVERLOAD_PCT        0x816
#define RO_CURRENT_ALARM       0x817

#define RO_DRIVE_STATE         0x901 // bit map, see page 54
#define RO_SW_VERSION          0x90E // If the read datum is D201H,it means the software edition is D-2.01.

#define RW_JOG_ENABLE          0x1023
#define RW_JOG_FWD             0x1024
#define RW_JOG_REV             0x1025

#define RW_CLR_FORMER_ALARMS   0x1021
#define RW_CLR_CURRENT_ALARMS  0x1022


#ifndef ULAPI
#error This is intended as a userspace component only.
#endif

#ifdef DEBUG
#define DBG(fmt, ...)					\
    do {						\
	if (param.debug) printf(fmt,  ## __VA_ARGS__);	\
    } while(0)
#else
#define DBG(fmt, ...)
#endif

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>

#include "rtapi.h"
#include "hal.h"
#include <modbus.h>
#include <modbus-tcp.h>
#include <modbus-rtu.h>
#include "inifile.h"

#define MODBUS_MIN_OK	10      // assert the modbus-ok pin after 10 successful modbus transactions

/* per-component HAL data struct */
typedef struct {
    hal_bit_t	*modbus_ok;	// the last MODBUS_OK transactions returned successfully
    hal_s32_t	*errorcount;    // number of failed Modbus transactions - hints at logical errors
    hal_float_t	looptime;
} haldata_comp_t, *haldata_comp_pointer;

/* per-slave HAL data struct */
typedef struct {
    hal_float_t	*speed_cmd; // rpm
    hal_float_t	*speed_feedback;
    hal_float_t	*load_pct;
    hal_float_t	*overload_pct;
    hal_s32_t	*current_alarm;
    hal_s32_t	*drive_state;

    hal_bit_t	*clear_current_alarm;
    hal_bit_t	*clear_former_alarms;
    hal_bit_t	*jog_enable;
    hal_bit_t	*jog_fwd;
    hal_bit_t	*jog_rev;
} haldata_t, *haldata_pointer;

/* per-slave non-HAL data struct */
typedef struct {
    hal_bit_t	last_clear_current_alarm;
    hal_bit_t	last_clear_former_alarms;
    hal_bit_t	last_jog_enable;
    hal_bit_t	last_jog_fwd;
    hal_bit_t	last_jog_rev;
    hal_s32_t	last_alarm;
} slavedata_t, *slavedata_pointer;

const char *alarms[] = {
    "no alarm",
    "Parameter breakdown", // Checksum results of parameters are abnormal.
    "A/D shift channels breakdown", // AD relevant electrical circuit damaged
    "Overspeed", // Motor is out of control
    "Overload", // Continuous running when surpass the rated torque
    "Position error counter overflow", // Internal counter overflow
    "position error pulse overflow", // Position error pulse exceeded the value set in parameter Pn-036
    "The setting of electrical gear and setting of given pulse frequency are not reasonable", // Electronic gear setting is not reasonable or pulse frequency is too high
    "Something wrong with the first tunnel of current sense", // Something wrong with inside chip of the first tunnel
    "Something wrong with the second tunnel of current sense", // Something wrong with inside chip of the second tunnel
    "Encoder PA , PB or PC disconnected", // At least one of PA, PB or PC is disconnected
    "Encoder PU , PV or PW disconnected", // At least one of PU, PV or PW is disconnected
    "Overcurrent", // An overcurrent flowed through the IPM module.
    "Overvoltage", // Main electrical circuit voltage for motor running is too high.
    "Undervoltage", // Main electrical circuit voltage for motor running is too low.
    "Bleeder resistor damaged", // Bleeder resistor is damaged
    "Regenerative error", // Regenerative circuit error
    "undefined (17)",
    "undefined (18)",
    "undefined (19)",
    "Power lines Open phase", // One phase is not connected in The main circuit power supply
    "instantaneous power off alarm", // More than one power cycle’s off in alternating current .
    "undefined (22)",
    "undefined (23)",
    "undefined (24)",
    "undefined (25)",
    "undefined (26)",
    "undefined (27)",
    "undefined (28)",
    "undefined (29)",
    "Encoder UVW illegal code", // U,V, W all “1” or “0”
    "Encoder UVW wrong code", // U,V,W code sequence is fault
    "Encoder C pulse is not correct", // C pulse appears in wrong position
    "Encoder has no C pulse", // No C pulse appeared after encoder rotated for one round.
    "undefined (34)",
    "undefined (35)",
    "undefined (36)",
    "undefined (37)",
    "undefined (38)",
    "undefined (39)",
    "undefined (40)",
    "reserved (41)",
    "The model of servo and motor is not correct.", // Servo parameter is not match with motor.
    "reserved (42)",
    "reserved (44)",
    "Absolute encoder multi-loop message", // Multi-loop message is not correct.
    "Absoluteencoder multi-loop message overflow", // Multi-loop message overflow
    "Battery voltage below 2.5V", 
    "Battery volatage below 3.1V", // Battery voltage is too low
    "undefined (49)",
    "Encoder communication over time",  // Encoder disconnect, encoder signal is disturbed, encoder damaged or encoder decode electric circuit damaged
    "No power supply absolute encoder rotated speed over 100rpm", // Multi-loop message may error.
    "Encoder absolute state is wrong", // Encoder or encoder decode electric circuit is damaged
    "Encoder count error", // Encoder or encoder decode electric circuit is damaged
    "Encoder control field parity bit, cut off bit error.", // Encoder signal is disturbed or encoder decode electric circuit is damaged
    "Encoder communication datum verify error", // Encoder signal is disturbed or encoder decode electric circuit is damaged
    "Encoder status field cut off point error", // Encoder signal is disturbed or encoder decode electric circuit is damaged
    "No serial encoder datum", // No serial encoder EEPROM datum
    "Serial encoder data form error", // Serial endoer EEPROM data form is not correct
    "Can not detect communication module", // No communication module or something wrong with communication module
    "Can not managed to connect with communication module", // Communication module CPU does not work well
    "drive can not receive circular data from communication module", // something wrong with the drive data receiving tunnel or communication module sending tunnel
    "The communication module can not receive the drive’s response data", // Something wrong with communication module
    "No connection between communication module and bus", // Bus communication is abnormal
    NULL,
};

// configuration and execution state
typedef struct params {
    int type;
    char *modname;
    int modbus_debug;
    int debug;
    int num_slaves;
    int slaves[MAXSLAVES];
    char *device;
    int baud;
    int bits;
    char parity;
    int stopbits;
    int rts_mode;
    int serial_mode;
    struct timeval response_timeout;
    struct timeval byte_timeout;
    int tcp_portno;
    char *progname;
    char *section;
    FILE *fp;
    char *inifile;
    int reconnect_delay;
    modbus_t *ctx;
    haldata_comp_pointer haldata_comp;
    haldata_pointer haldata;
    slavedata_pointer slavedata;
    int hal_comp_id;
    int modbus_ok;
    uint16_t failed_reg;		// remember register for failed modbus transaction for debugging
    int	last_errno;
    char *tcp_destip;
    int report_device;
} params_type, *param_pointer;

#define TYPE_RTU 0
#define TYPE_TCP_SERVER 1
#define TYPE_TCP_CLIENT 2

// default options; read from inifile or command line
static params_type param = {
    .type = -1,
    .modname = NULL,
    .modbus_debug = 0,
    .debug = 0,
    .num_slaves = 0,
    .slaves = {1},
    .device = "/dev/ttyS0",
    .baud = 19200,
    .bits = 8,
    .parity = 'E',
    .stopbits = 1,
    .serial_mode = -1,
    .rts_mode = -1,
    .response_timeout = { .tv_sec = 0, .tv_usec = 500000 },
    .byte_timeout = {.tv_sec = 0, .tv_usec = 500000},
    .tcp_portno = 1502, // MODBUS_TCP_DEFAULT_PORT (502) would require root privileges
    .progname = "generic",
    .section = "GENERIC",
    .fp = NULL,
    .inifile = NULL,
    .reconnect_delay = 1,
    .ctx = NULL,
    .haldata_comp = NULL,
    .haldata = NULL,
    .hal_comp_id = -1,
    .modbus_ok = 0,    // set modbus-ok bit if last MODBUS_OK transactions went well 
    .failed_reg = 0,
    .last_errno = 0,
    .tcp_destip = "127.0.0.1",
    .report_device = 0,
};

static int connection_state;
enum connstate {NOT_CONNECTED, OPENING, CONNECTING, CONNECTED, RECOVER, DONE};

static char *option_string = "dhmrn:S:I:";
static struct option long_options[] = {
    {"debug", no_argument, 0, 'd'},
    {"help", no_argument, 0, 'h'},
    {"modbus-debug", no_argument, 0, 'm'},
    {"report-device", no_argument, 0, 'r'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'}, // default section = GENERIC
    {"name", required_argument, 0, 'n'},    // generic
    {0,0,0,0}
};

// any per-device interactions before exiting
int cleanup(param_pointer p) 
{
#if 0
    int n;
    for (n = 0; n <  p->num_slaves; n++) {
    	modbus_set_slave(p->ctx, n);
	
    	if (modbus_write_register(p->ctx, REG_FOO, 0) != 1) {
    	    fprintf(stderr, "%s/%d: failed to XXX): %s\n",
    		    p->progname, n, modbus_strerror(errno));
    	} else {
    	    DBG("%s/%d: XXXXXXXXXXXXXX\n", p->progname, n);
    	}
    }
#endif
    return 0;
}

// common end-of-program cleanup
void  windup(param_pointer p) 
{
    if (p->haldata_comp && *(p->haldata_comp->errorcount)) {
	fprintf(stderr,"%s: %d modbus errors\n",p->progname, *(p->haldata_comp->errorcount));
	fprintf(stderr,"%s: last command register: 0x%.4x\n",p->progname, p->failed_reg);
	fprintf(stderr,"%s: last error: %s\n",p->progname, modbus_strerror(p->last_errno));
    }
    if (p->hal_comp_id >= 0)
	hal_exit(p->hal_comp_id);
    if (p->ctx)
	modbus_close(p->ctx);
}

static void toggle_modbus_debug(int sig)
{
    param.modbus_debug = !param.modbus_debug;
    modbus_set_debug(param.ctx, param.modbus_debug);
}

static void toggle_debug(int sig)
{
    param.debug = !param.debug;
}

static void quit(int sig) 
{
    if (param.debug)
	fprintf(stderr,"quit(connection_state=%d)\n",connection_state);
    
    switch (connection_state) {
	
    case CONNECTING:  
	// modbus_tcp_accept() or TCP modbus_connect()  were interrupted
	// these wont return to the main loop, so exit here
	windup(&param);
	exit(0);
	break;
	
    default:
	connection_state = DONE;
	break;
    }
}

enum kwdresult {NAME_NOT_FOUND, KEYWORD_INVALID, KEYWORD_FOUND};
#define MAX_KWD 10

int findkwd(param_pointer p, const char *name, int *result, const char *keyword, int value, ...)
{
    const char *word;
    va_list ap;
    const char *kwds[MAX_KWD], **s;
    int nargs = 0;

    if ((word = iniFind(p->fp, name, p->section)) == NULL)
	return NAME_NOT_FOUND;

    kwds[nargs++] = keyword;
    va_start(ap, value);

    while (keyword != NULL) {
	if (!strcasecmp(word, keyword)) {
	    *result = value;
	    va_end(ap);
	    return KEYWORD_FOUND;
	}
	keyword = va_arg(ap, const char *);
	kwds[nargs++] = keyword;
	if (keyword)
	    value = va_arg(ap, int);
    }  
    fprintf(stderr, "%s: %s:[%s]%s: found '%s' - not one of: ", 
	    p->progname, p->inifile, p->section, name, word);
    for (s = kwds; *s; s++) 
	fprintf(stderr, "%s ", *s);
    fprintf(stderr, "\n");
    va_end(ap);
    return KEYWORD_INVALID;
}

int read_ini(param_pointer p)
{
    const char *s;
    double f;
    int value,i;

    if ((p->fp = fopen(p->inifile,"r")) != NULL) {
	for (i = 0; i < MAXSLAVES; i++) {
	    if (iniFindIntN(p->fp,  "TARGET", p->section, &p->slaves[i], i+1)) {
		p->num_slaves = i;
		break;
	    }
	}
	if (!p->debug)
	    iniFindInt(p->fp, "DEBUG", p->section, &p->debug);
	if (!p->modbus_debug)
	    iniFindInt(p->fp, "MODBUS_DEBUG", p->section, &p->modbus_debug);
	iniFindInt(p->fp, "BITS", p->section, &p->bits);
	iniFindInt(p->fp, "BAUD", p->section, &p->baud);
	iniFindInt(p->fp, "STOPBITS", p->section, &p->stopbits);
	iniFindInt(p->fp, "PORT", p->section, &p->tcp_portno);
	iniFindInt(p->fp, "RECONNECT_DELAY", p->section, &p->reconnect_delay);

	if ((s = iniFind(p->fp, "TCPDEST", p->section))) {
	    p->tcp_destip = strdup(s);
	}
	if ((s = iniFind(p->fp, "DEVICE", p->section))) {
	    p->device = strdup(s);
	}
	if (iniFindDouble(p->fp, "RESPONSE_TIMEOUT", p->section, &f)) {
	    p->response_timeout.tv_sec = (int) f;
	    p->response_timeout.tv_usec = (f-p->response_timeout.tv_sec) * 1000000;
	}
	if (iniFindDouble(p->fp, "BYTE_TIMEOUT", p->section, &f)) {
	    p->byte_timeout.tv_sec = (int) f;
	    p->byte_timeout.tv_usec = (f-p->byte_timeout.tv_sec) * 1000000;
	}
	value = p->parity;
	if (findkwd(p, "PARITY", &value,
		    "even",'E', 
		    "odd", 'O', 
		    "none", 'N',
		    NULL) == KEYWORD_INVALID)
	    return -1;
	p->parity = value;
#ifdef FIXME	
	if (findkwd(p, "RTS_MODE", &p->rts_mode,
		    "up", MODBUS_RTU_RTS_UP,
		    "down", MODBUS_RTU_RTS_DOWN, 
		    "none", MODBUS_RTU_RTS_NONE,
		    NULL) == KEYWORD_INVALID)
	    return -1;
#endif
	if (findkwd(p,"SERIAL_MODE", &p->serial_mode,
		    "rs232", MODBUS_RTU_RS232,
		    "rs485", MODBUS_RTU_RS232,
		    NULL) == KEYWORD_INVALID)
	    return -1;

	if (findkwd(p, "TYPE", &p->type,
		    "rtu", TYPE_RTU, 
		    "tcpserver", TYPE_TCP_SERVER, 
		    "tcpclient", TYPE_TCP_CLIENT, 
		    NULL) == NAME_NOT_FOUND) {
	    fprintf(stderr, "%s: missing required TYPE in section %s\n", 
		    p->progname, p->section);
	    return -1;
	}
    } else {
	fprintf(stderr, "%s:cant open inifile '%s'\n", 
		p->progname, p->inifile);
	return -1;
    }
    return 0;
}

void usage(int argc, char **argv) {
    printf("Usage:  %s [options]\n", argv[0]);
    printf("This is a userspace HAL program, typically loaded using the halcmd \"loadusr\" command:\n"
	   "    loadusr %s [options]\n"
	   "Options are:\n"
	   "-I or --ini <inifile>\n"
	   "    Use <inifile> (default: take ini filename from environment variable INI_FILE_NAME)\n"
	   "-S or --section <section-name> (default 8)\n"
	   "    Read parameters from <section_name> (default 'VFS11')\n"
	   "-d or --debug\n"
	   "    Turn on debugging messages. Toggled by USR1 signal.\n"
	   "-m or --modbus-debug\n"
	   "    Turn on modbus debugging.  This will cause all modbus messages\n"
	   "    to be printed in hex on the terminal. Toggled by USR2 signal.\n"	   
	   "-r or --report-device\n"
	   "    Report device properties on console at startup\n", MODNAME);
}

const char *alarm_text( uint16_t n)
{
    if (n < sizeof(alarms)/sizeof(alarms[0]))
	return alarms[n];
    return "invalid alarm number";
}

int write_data(modbus_t *ctx, int slave, param_pointer p)
{
    uint16_t cmd[3];
    haldata_pointer h = &p->haldata[slave];
    slavedata_pointer sp = &p->slavedata[slave];

    if  ((*(h->jog_enable) != sp->last_jog_enable) ||
	 (*(h->jog_fwd) != sp->last_jog_fwd) ||
    	 (*(h->jog_rev) != sp->last_jog_rev)) {
	cmd[0] = sp->last_jog_enable = *(h->jog_enable);
	cmd[1] = sp->last_jog_fwd = *(h->jog_fwd);
	cmd[2] = sp->last_jog_rev = *(h->jog_rev);

	if ((modbus_write_registers(ctx,  RW_JOG_ENABLE, 3, cmd)) < 0) {
	    p->failed_reg = RW_JOG_ENABLE;
	    (*p->haldata_comp->errorcount)++;
	    p->last_errno = errno;
	    return errno;
	} 
    }
    if ((*(h->clear_current_alarm) != sp->last_clear_current_alarm) ||
  	(*(h->clear_former_alarms) != sp->last_clear_former_alarms)) {
	cmd[0] = sp->last_clear_former_alarms = *(h->clear_former_alarms);
	cmd[1] = sp->last_clear_current_alarm = *(h->clear_current_alarm);

	if ((modbus_write_registers(ctx,  RW_CLR_FORMER_ALARMS, 2, cmd)) < 0) {
	    p->failed_reg = RW_CLR_FORMER_ALARMS;
	    (*p->haldata_comp->errorcount)++;
	    p->last_errno = errno;
	    return errno;
	} 
    }
    return 0;
}

#define GETREG(startreg,num,into)					\
    do {							\
	curr_reg = startreg;						\
	if (modbus_read_registers(ctx, startreg, num, into) != 1)	\
	    goto failed;					\
    } while (0)

int read_initial(modbus_t *ctx, int slave, param_pointer p)
{
    uint16_t curr_reg, version, drivestate, current_alarm,
	historic_alarms[10];
    char edition[5];
    int i;
    
    if (p->report_device) {
	GETREG(RO_SW_VERSION, 1, &version);
	GETREG(RO_DRIVE_STATE, 1, &drivestate);
	GETREG(RO_CURRENT_ALARM, 1, &current_alarm);
	GETREG(RO_FORMER_ALARMS, 10, historic_alarms);

	sprintf(edition,"%4.4X", version);
	printf("%s/%d: software edition: %c-%c.%s\n", 
	       p->progname, slave, edition[0], edition[1], &edition[2]);
	printf("%s/%d: drivestate: 0x%4.4x\n", p->progname, slave, drivestate);
	printf("%s/%d: current alarm: %d - %s\n", 
	       p->progname, slave, current_alarm, alarm_text(current_alarm));
	for (i = 0; i < 10; i++) {
	    if (historic_alarms[i]) {
		printf("%s/%d: historic alarm %d: %d - %s\n", 
		       p->progname, slave, i, historic_alarms[i], alarm_text(historic_alarms[i]));
	    }
	}
    }
    return 0;

 failed:
    p->failed_reg = curr_reg;
    p->last_errno = errno;
    (*p->haldata_comp->errorcount)++;
    if (p->debug)
	fprintf(stderr, "%s: read_initial: modbus_read_registers(0x%4.4x): %s\n", 
		p->progname, curr_reg, modbus_strerror(errno));
    return p->last_errno;
}

int read_data(modbus_t *ctx, int slave, param_pointer p)
{
    uint16_t status_regs[3], curr_reg;

    haldata_pointer h = &p->haldata[slave];
    slavedata_pointer sp = &p->slavedata[slave];

    GETREG(RO_DRIVE_STATE, 1, status_regs);
    *(h->drive_state) = status_regs[0];
    
    GETREG(RO_LOAD_PCT, 3, status_regs);
    *(h->load_pct) = status_regs[0];
    *(h->overload_pct) = status_regs[1];
    *(h->current_alarm) = status_regs[2];

    if (*(h->current_alarm) != sp->last_alarm) {
	printf("%s/%d: ALARM: %d - %s\n", 
	       p->progname, slave, *(h->current_alarm), alarm_text(*(h->current_alarm)));
	sp->last_alarm = *(h->current_alarm);
    }

    GETREG(RO_SPEED_FEEDBACK, 2,status_regs);
    *(h->speed_feedback) = status_regs[0];
    *(h->speed_cmd) = status_regs[1];

    return 0;

 failed:
    p->failed_reg = curr_reg;
    p->last_errno = errno;
    (*p->haldata_comp->errorcount)++;
    if (p->debug)
	fprintf(stderr, "%s/%d: read_data: modbus_read_registers(0x%4.4x): %s\n", 
		p->progname, slave, curr_reg, modbus_strerror(errno));
    return p->last_errno;
}

#undef GETREG

#define PIN(x)					\
    do {						\
	status = (x);					\
	if ((status) != 0)				\
	    return status;				\
    } while (0)

int hal_setup(int id, param_pointer p, const char *name)
{
    int status, i;
    haldata_comp_t *hc = p->haldata_comp;

    // per-component pins & params
    PIN(hal_param_float_newf(HAL_RW, &(hc->looptime), id, "%s.loop-time", name));
    PIN(hal_pin_bit_newf(HAL_OUT, &(hc->modbus_ok), id, "%s.modbus-ok", name)); // JET
    PIN(hal_pin_s32_newf(HAL_OUT, &(hc->errorcount), id, "%s.error-count", name));

    // per-slave pins & params
    for (i = 0; i < p->num_slaves; i++) {
	haldata_t *h = &p->haldata[i];

	PIN(hal_pin_s32_newf(HAL_OUT, &(h->drive_state), id, "%s.%d.drive-state", name, i));
	PIN(hal_pin_s32_newf(HAL_OUT, &(h->current_alarm), id, "%s.%d.current-alarm", name, i));
	PIN(hal_pin_float_newf(HAL_OUT, &(h->speed_cmd), id, "%s.%d.speed-cmd", name, i));
	PIN(hal_pin_float_newf(HAL_OUT, &(h->speed_feedback), id, "%s.%d.speed-feedback", name, i));
	PIN(hal_pin_float_newf(HAL_OUT, &(h->load_pct), id, "%s.%d.load-pct", name, i));
	PIN(hal_pin_float_newf(HAL_OUT, &(h->overload_pct), id, "%s.%d.overload-pct", name, i));

	PIN(hal_pin_bit_newf(HAL_IN, &(h->clear_current_alarm), id, "%s.%d.clear-current-alarm", name, i));
	PIN(hal_pin_bit_newf(HAL_IN, &(h->clear_former_alarms), id, "%s.%d.clear-former-alarms", name, i));
	PIN(hal_pin_bit_newf(HAL_IN, &(h->jog_enable), id, "%s.%d.jog-enable", name, i));
	PIN(hal_pin_bit_newf(HAL_IN, &(h->jog_fwd), id, "%s.%d.jog-fwd", name, i));
	PIN(hal_pin_bit_newf(HAL_IN, &(h->jog_rev), id, "%s.%d.jog-rev", name, i));
    }
    return 0;
}
#undef PIN

int set_defaults(param_pointer p)
{
    int i;

    haldata_comp_t *hc = p->haldata_comp;
    *(hc->errorcount) = 0;
    *(hc->modbus_ok) = 0;
    hc->looptime = 0.1;

    for (i = 0; i < p->num_slaves; i++) {
	haldata_pointer h = &p->haldata[i];
	slavedata_pointer sp = &p->slavedata[i];

    	*(h->speed_cmd) = 0;
    	*(h->speed_feedback) = 0;
    	*(h->load_pct) = 0;
    	*(h->overload_pct) = 0;
    	*(h->current_alarm) = sp->last_alarm = 0;
    	*(h->drive_state) = 0;

    	*(h->clear_current_alarm) = sp->last_clear_current_alarm  = 0;
  	*(h->clear_former_alarms) = sp->last_clear_former_alarms = 0;
    	*(h->jog_enable) = sp->last_jog_enable = 0;
    	*(h->jog_fwd) = sp->last_jog_fwd = 0;
    	*(h->jog_rev) = sp->last_jog_rev = 0;
    }
    p->failed_reg = 0;
    return 0;
}

int main(int argc, char **argv)
{
    struct timespec loop_timespec;
    int opt, socket, n;
    param_pointer p = &param;
    int retval = 0;
    retval = -1;
    p->progname = argv[0];
    connection_state = NOT_CONNECTED;
    p->inifile = getenv("INI_FILE_NAME");

    while ((opt = getopt_long(argc, argv, option_string, long_options, NULL)) != -1) {
	switch(opt) {
	case 'n':
	    p->modname = strdup(optarg);
	    break;
	case 'm':
	    p->modbus_debug = 1;
	    break;
	case 'd':   
	    p->debug = 1;
	    break;
	case 'S':
	    p->section = optarg;
	    break;
	case 'I':
	    p->inifile = optarg;
	    break;	
	case 'r':
	    p->report_device = 1;
	    break;
	case 'h':
	default:
	    usage(argc, argv);
	exit(0);
	}
    }

    if (p->inifile) {
	if (read_ini(p))
	    goto finish;
	if (!p->modname)
	    p->modname = MODNAME;
    } else {
	fprintf(stderr, "%s: ERROR: no inifile - either use '--ini inifile' or set INI_FILE_NAME environment variable\n", p->progname);
	goto finish;
    }

    signal(SIGINT, quit);
    signal(SIGTERM, quit);
    signal(SIGUSR1, toggle_debug);
    signal(SIGUSR2, toggle_modbus_debug);

    p->hal_comp_id = hal_init(p->modname);
    if ((p->hal_comp_id < 0) || (connection_state == DONE)) {
	fprintf(stderr, "%s: ERROR: hal_init(%s) failed: HAL error code=%d\n", 
		p->progname, p->modname, p->hal_comp_id);
	retval = p->hal_comp_id;
	goto finish;
    }
    p->slavedata = (slavedata_pointer) malloc(sizeof(slavedata_t) * p->num_slaves);
    p->haldata_comp = (haldata_comp_t *) hal_malloc(sizeof(haldata_comp_t));
    p->haldata = (haldata_t *) hal_malloc(sizeof(haldata_t) * p->num_slaves);
    if ((p->haldata_comp == 0) || 
	(p->haldata == 0) || 
	(p->slavedata == 0) ) {
	fprintf(stderr, "%s: ERROR: unable to allocate memory\n", p->modname);
	retval = -1;
	goto finish;
    }
    if ((connection_state == DONE) || hal_setup(p->hal_comp_id, p, p->modname))
	goto finish;
    
    set_defaults(p);
    hal_ready(p->hal_comp_id);

    DBG("using libmodbus version %s\n", LIBMODBUS_VERSION_STRING);
	
    switch (p->type) {

    case TYPE_RTU:
	connection_state = OPENING;
	if ((p->ctx = modbus_new_rtu(p->device, p->baud, p->parity, p->bits, p->stopbits)) == NULL) {
	    fprintf(stderr, "%s: ERROR: modbus_new_rtu(%s): %s\n", 
		    p->progname, p->device, modbus_strerror(errno));
	    goto finish;
	}

	if ((retval = modbus_connect(p->ctx)) != 0) {
	    fprintf(stderr, "%s: ERROR: couldn't open serial device: %s\n", 
		    p->modname, modbus_strerror(errno));
	    goto finish;
	}
	// see https://github.com/stephane/libmodbus/issues/42
	if ((p->serial_mode != -1) && modbus_rtu_set_serial_mode(p->ctx, p->serial_mode) < 0) {
	    fprintf(stderr, "%s: ERROR: modbus_rtu_set_serial_mode(%d): %s\n", 
		    p->modname, p->serial_mode, modbus_strerror(errno));
	    goto finish;
	}
#ifdef FIXME
	if ((p->rts_mode != -1) && modbus_rtu_set_rts(p->ctx, p->rts_mode) < 0) {
	    fprintf(stderr, "%s: ERROR: modbus_rtu_set_rts(%d): %s\n", 
		    p->modname, p->rts_mode, modbus_strerror(errno));
	    goto finish;
	}
#endif
	DBG("%s: serial port %s connected\n", p->progname, p->device);
	break;

    case  TYPE_TCP_SERVER:
	if ((p->ctx = modbus_new_tcp("127.0.0.1", p->tcp_portno)) == NULL) {
	    fprintf(stderr, "%s: modbus_new_tcp(%d): %s\n", 
		    p->progname, p->tcp_portno, modbus_strerror(errno));
	    goto finish;
	}
	if ((socket = modbus_tcp_listen(p->ctx, 1)) < 0) {
	    fprintf(stderr, "%s: modbus_tcp_listen(): %s\n", 
		    p->progname, modbus_strerror(errno));
	    goto finish;
	}
	connection_state = CONNECTING;
	if (modbus_tcp_accept(p->ctx, &socket) < 0) {
	    fprintf(stderr, "%s: modbus_tcp_accept(): %s\n", 
		    p->progname, modbus_strerror(errno));
	    goto finish;
	}
	break;

    case  TYPE_TCP_CLIENT:
	if ((p->ctx = modbus_new_tcp(p->tcp_destip, p->tcp_portno)) == NULL) {
	    fprintf(stderr,"%s: Unable to allocate libmodbus TCP context: %s\n", 
		    p->progname, modbus_strerror(errno));
	    goto finish;
	}
	connection_state = CONNECTING;
	if (modbus_connect(p->ctx) < 0) {
	    fprintf(stderr, "%s: TCP connection to %s:%d failed: %s\n", 
		    p->progname, p->tcp_destip, p->tcp_portno, modbus_strerror(errno));
	    modbus_free(p->ctx);
	    goto finish;
	}
	DBG("main: TCP connected to %s:%d\n", p->tcp_destip, p->tcp_portno);
	break;

    default:
	fprintf(stderr, "%s: ERROR: invalid connection type %d\n", 
		p->progname, p->type);
	goto finish;
    }

    modbus_set_debug(p->ctx, p->modbus_debug);
    connection_state = CONNECTED;

    while (connection_state != DONE) {
	for (n = 0; n <  p->num_slaves; n++) {
	    if (modbus_set_slave(p->ctx, n) < 0) {
		fprintf(stderr, "%s: ERROR: invalid slave number: %d\n", p->modname, n);
		goto finish;
	    }
	    read_initial(p->ctx, n, p);
	}
	while (connection_state == CONNECTED) {
	    for (n = 0; n <  p->num_slaves; n++) {

		modbus_set_slave(p->ctx, n);

		p->modbus_ok = read_data(p->ctx, n, p) ? 0 : p->modbus_ok + 1;

		if ((retval = write_data(p->ctx, n, p))) {
		    p->modbus_ok = 0;
		    if ((retval == EBADF || retval == ECONNRESET || retval == EPIPE)) {
			connection_state = RECOVER;
		    }
		} else {
		    p->modbus_ok++;
		}
		*(p->haldata_comp->modbus_ok) = (p->modbus_ok > MODBUS_MIN_OK) ? 1:0;

		/* don't want to scan too fast, and shouldn't delay more than a few seconds */
		if (p->haldata_comp->looptime < 0.001) p->haldata_comp->looptime = 0.001;
		if (p->haldata_comp->looptime > 2.0) p->haldata_comp->looptime = 2.0;
		loop_timespec.tv_sec = (time_t)(p->haldata_comp->looptime);
		loop_timespec.tv_nsec = (long)((p->haldata_comp->looptime - loop_timespec.tv_sec) * 1000000000l);
	    }
	}
	switch (connection_state) {
	case DONE:
	    // cleanup actions before exiting.
	    modbus_flush(p->ctx);
	    cleanup(p);
	    break;

	case RECOVER:
	    DBG("recover\n");
	    set_defaults(p);
	    // reestablish connection to slave
	    switch (p->type) {

	    case TYPE_RTU:
	    case TYPE_TCP_CLIENT:
		modbus_flush(p->ctx);
		modbus_close(p->ctx);
		while ((connection_state != CONNECTED) &&  
		       (connection_state != DONE)) {
		    sleep(p->reconnect_delay);
		    if (!modbus_connect(p->ctx)) {
			connection_state = CONNECTED;
			DBG("rtu/tcpclient reconnect\n");
		    } else {
			fprintf(stderr, "%s: recovery: modbus_connect(): %s\n", 
				p->progname, modbus_strerror(errno));
		    }
		}
		break;

	    case TYPE_TCP_SERVER:
		while ((connection_state != CONNECTED) &&  
		       (connection_state != DONE)) {
		    connection_state = CONNECTING;
		    sleep(p->reconnect_delay);
		    if (!modbus_tcp_accept(p->ctx, &socket)) {
			fprintf(stderr, "%s: recovery: modbus_tcp_accept(): %s\n", 
				p->progname, modbus_strerror(errno));
		    } else {
			connection_state = CONNECTED;
			DBG("tcp reconnect\n");
		    }
		}
		break;

	    default:
		break;
	    }
	    break;
	default: ;
	}
    }
    retval = 0;	

 finish:
    windup(p);
    return retval;
}

