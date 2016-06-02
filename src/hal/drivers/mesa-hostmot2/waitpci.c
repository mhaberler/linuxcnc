// 1:1 transliteration of https://www.kernel.org/doc/htmldocs/uio-howto/uio_pci_generic_example.html

#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

struct wait_uio_irq {
    int uio_fd, config_fd;
    hal_u32_t *timer_period;
    hal_u32_t *irq_count;
    hal_u32_t *irq_missed;
    hal_u32_t *read_errors;
    hal_u32_t *write_errors;
    unsigned char command_high;
};

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("instantiable wait-for-uio-IRQ thread function");
RTAPI_TAG(HAL,HC_INSTANTIABLE);

static char *device = "uio0";
RTAPI_IP_STRING(device, "uio device");

static int comp_id;
static char *compname = "waitpci";

static int waitirq(void *arg, const hal_funct_args_t *fa)
{
    struct wait_uio_irq *this = arg;

    // Re-enable interrupts.
    int err = pwrite(this->config_fd, &this->command_high, 1, 5);
    if (err != 1) {
	*(this->write_errors) += 1;
    }

    // wait for IRQ
    uint32_t icount = 0;
    size_t nb = read(this->uio_fd, &icount, sizeof(icount));
    if (nb != sizeof(icount)) {
	*(this->read_errors) += 1;
    }
    *(this->irq_count) += 1;
    *(this->irq_missed) = icount - *(this->irq_count);

    return 0;
}

static int instantiate_waitirq(const char *name,
			       const int argc,
			       const char**argv)
{
    struct wait_uio_irq *this;
    int inst_id, retval;
    char buf[100];
    int uiofd;
    int configfd;
    unsigned char command_high;

    rtapi_snprintf(buf, sizeof(buf), "/dev/%s", device);
    uiofd = open(buf, O_RDONLY);
    if (uiofd < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s/%s: can't open %s: %d - %s",
			compname, name, buf, errno, strerror(errno));
	return -errno;
    }
    rtapi_snprintf(buf, sizeof(buf), "/sys/class/uio/%s/device/config", device);

    configfd = open(buf, O_RDWR);
    if (configfd < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s/%s: can't open %s: %d - %s",
			compname, name, buf, errno, strerror(errno));
	return -errno;
    }
    // Read and cache command value
    retval = pread(configfd, &command_high, 1, 5);
    if (retval != 1) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s/%s: can't read from %s: %d - %s",
			compname, name, buf, errno, strerror(errno));
	return -errno;
    }
    command_high &= ~0x4; // 0x4 - where does this come from?

    if ((inst_id = hal_inst_create(name, comp_id,
				   sizeof(struct wait_uio_irq),
				   (void **)&this)) < 0)
	return -1;

    this->uio_fd = uiofd;
    this->config_fd = configfd;
    this->command_high = command_high;

    if (hal_pin_u32_newf(HAL_OUT, &this->irq_count, inst_id, "%s.irq-count", name) ||
	hal_pin_u32_newf(HAL_IO,  &this->timer_period, inst_id, "%s.timer-period", name) ||
	hal_pin_u32_newf(HAL_OUT, &this->irq_missed, inst_id, "%s.irq-missed", name) ||
	hal_pin_u32_newf(HAL_OUT, &this->write_errors, inst_id, "%s.write-errors", name) ||
	hal_pin_u32_newf(HAL_OUT, &this->read_errors, inst_id, "%s.read-errors", name))
	return -1;

    hal_export_xfunct_args_t xa = {
	.type = FS_XTHREADFUNC,
	.funct.x = waitirq,
	.arg = this,
	.uses_fp = 0,
	.reentrant = 0,
	.owner_id = inst_id
    };
    return hal_export_xfunctf(&xa, "%s.waitirq", name);
}


int rtapi_app_main(void)
{
    comp_id = hal_xinit(TYPE_RT, 0, 0, instantiate_waitirq, NULL, compname);
    if (comp_id < 0)
	return comp_id;

    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id);
}
