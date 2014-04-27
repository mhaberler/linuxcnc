// extended HAL Python bindings
// group/member/change reporting HAL methods
// ringbuffer operations
// hal signal and linking support
//
// Michael Haberler 5/2013
// Ringbuffer code by Pavel Shramov 2/2013


#include <boost/python.hpp>
#include <boost/python/raw_function.hpp>
#include <boost/python/overloads.hpp>
#include <exception>
#include <map>
#include <string>

#include "hal.h"
#include "hal_priv.h"
#include "hal_group.h"
#include "hal_ring.h"
#include "hal_rcomp.h"

#define DEFAULT_RING_SIZE 4096

union halppunion {
    hal_pin_t *pin;
    hal_param_t *param;
};

struct halitem {
    bool is_pin;
    union halppunion pp;
    void *ptr;
};

typedef std::map<std::string, struct halitem> itemmap;

namespace bp = boost::python;

inline bp::object pass_through(bp::object const& o) { return o; }

bp::object hal2py(int type, hal_data_u *dp) {
    switch (type) {
    case HAL_BIT:
	return bp::object((bool) dp->b);
    case HAL_FLOAT:
	return bp::object((double)dp->f);
    case HAL_S32:
	return bp::object((int)dp->s);
    case HAL_U32:
	return bp::object((unsigned)dp->u);
    }
    PyErr_Format(PyExc_RuntimeError, "hal2py: invalid type %d", type);
    throw boost::python::error_already_set();
}

bp::object  py2hal(int type, hal_data_u *dp, bp::object value) {
    switch (type) {
    case HAL_BIT:
	if (!PyInt_Check(value.ptr()))
	    throw std::runtime_error("invalid type for HAL scalar of type bit");
	dp->b = bp::extract < int > (value) ? 1:0;
	break;
    case HAL_FLOAT:
	if (!PyFloat_Check(value.ptr()))
	    throw std::runtime_error("invalid type for HAL scalar of type float");
	dp->f =  bp::extract < double > (value);
	break;
    case HAL_S32:
	if (!PyInt_Check(value.ptr()))
	    throw std::runtime_error("invalid type for HAL scalar of type s32");
	dp->s = bp::extract < int > (value);
	break;
    case HAL_U32:
	if (!PyInt_Check(value.ptr()))
		throw std::runtime_error("invalid type for HAL scalar of type u32");
	dp->u = bp::extract < unsigned > (value);
	break;
    default:
	PyErr_Format(PyExc_RuntimeError, "py2hal: invalid type %d", type);
	throw boost::python::error_already_set();
    }
    return value;
}

class RingIter;

class Ring {

public:
    ringbuffer_t rb;

    hal_ring_t *halring;
    std::string rname;
    bool in_halmem;
    unsigned flags;

    Ring(const char *name, ringbuffer_t rbuf);
    virtual ~Ring();
    const size_t available();
    bp::object scratchpad();
    int get_reader();
    void set_reader(int r);
    int get_writer();
    void set_writer(int w);
    int get_size();
    int get_spsize();
    int get_type();
    bool get_rmutex_mode();
    bool get_wmutex_mode();
    bool get_in_halmem();
    std::string get_name();
};

class StreamRing : public Ring {
public:
    StreamRing(const char *name, ringbuffer_t &rbuffer);
    const size_t flush();
    void consume(int nbytes);
    const size_t available();
    int write(char *buf, size_t size);
    bp::object next_buffer();
};


class RecordRing : public Ring {
public:
    RecordRing(const char *name, ringbuffer_t &rbuffer);
    const size_t flush();
    int write(char *buf, size_t size);
    bp::object next_size();
    int shift();
    const size_t available();
    bp::object next_buffer();
    RingIter __iter__() const;
};

#if 0
class MultipartRing : public Ring {
    bringbuffer_t mpr;
public:
    MultipartRing(const char *name, ringbuffer_t &rbuffer);
    virtual ~MultipartRing();

    int append_frame(char *buf, size_t size, int flags); // add frame
    size_t commit(); // send off multipart message

    bp::object next_size();
    bp::list read_multipart();
};
#endif

typedef boost::shared_ptr< Ring > ring_ptr;
typedef boost::shared_ptr< StreamRing > streamring_ptr;
typedef boost::shared_ptr< RecordRing > framering_ptr;
//typedef boost::shared_ptr< MultipartRing > mpring_ptr;

class RingIter : public ringiter_t {
private:
    const RecordRing &_ring;

    int shift() {
	int r = record_iter_shift(this);
	if (r == EINVAL)
	    throw std::out_of_range("Iterator is out of date");
	return r;
    }

    bp::object read_buffer() const
    {
	const void *data;
	size_t size;

	int r = record_iter_read(this, &data, &size);
	if (r) {
	    if (r == EAGAIN) return bp::object();
	    throw std::out_of_range("Iterator is out of date");
	}
	bp::handle<> h(PyBuffer_FromMemory ((void *) data, size));
	return bp::object(h);
    }

public:
    RingIter(const RecordRing &ring) : _ring(ring) {
	record_iter_init(&ring.rb, this);
    }

    bool valid() { return !record_iter_invalid(this);}

    bp::object next()  {
	bp::object buf = read_buffer();
	if (buf.ptr() == bp::object().ptr()) {
	    PyErr_SetString(PyExc_StopIteration, "No more data.");
	    boost::python::throw_error_already_set();
	}
	shift();
	return buf;
    }
};

inline RingIter RecordRing::__iter__() const { return RingIter(*this); }

class HalComponent {
private:
    hal_comp_t *comp;
    hal_compiled_comp_t *ccomp;
    char *prefix;
    halitem *find_item(std::string name);
public:
    itemmap *items;
    HalComponent(hal_comp_t *c);
    HalComponent(char *name, int type =  TYPE_USER,
		 char *prefix = NULL, int arg1 = 0, int arg2 = 0);
    ~HalComponent();
    void newpin(const char *name, int type, int dir);
    void newparam(const char *name, int type, int dir);
    void ready();
    void exit();
    void bind();
    void unbind();
    void acquire();
    void release();
    bp::list params();
    bp::list pins();
    bp::list changed_pins();
    const char *get_name();
    int  get_id();
    int  get_type();
    int get_state();
    int get_pid();
    int get_arg1();
    int get_arg2();
    int get_last_bound();
    int get_last_unbound();
    int get_last_update();
    int set_last_update(int timestamp);
    bp::object getitem( bp::object index);
    bp::object setitem( bp::object index, bp::object value);
    ring_ptr ring_create(char *name,
			 size_t size = DEFAULT_RING_SIZE,
			 size_t spsize = 0,
			 int type = RINGTYPE_RECORD,
			 bool use_rmutex = false,
			 bool use_wmutex = false,
			 bool in_halmem = false);
    ring_ptr ring_attach(char *name,
			 int type = RINGTYPE_RECORD);
    void ring_detach(Ring &r);
};

class HalParam {
private:
    hal_param_t *param;
public:
    HalParam(hal_param_t *p) : param(p) {}

    const char *get_name() { return param->name; }
    int get_type()         { return param->type; }
    int get_dir()          { return param->dir; }
    int get_handle()       { return SHMOFF(param); }

    bp::object get_value() {
	hal_data_u *dp = (hal_data_u *)SHMPTR(param->data_ptr);
	return hal2py(param->type, dp);
    };

    void set_value(bp::object value) {
	hal_data_u *dp = (hal_data_u *)SHMPTR(param->data_ptr);
	py2hal(param->type, dp, value);
    };

    bp::object get_owner() {
	hal_comp_t *comp = (hal_comp_t *)SHMPTR(param->owner_ptr);
	return bp::object(HalComponent(comp));
    }
};

class HalPin {
private:
    //hal_comp_t *owner;
    hal_pin_t *pin;
public:
    HalPin(hal_pin_t *p) : pin(p) {}

    const char *get_name() { return pin->name; }
    int get_type()         { return pin->type; }
    int get_dir()          { return pin->dir; }
    int  get_flags()       { return pin->flags; }
    double get_epsilon()   { return pin->epsilon; }
    // just use the offset of the pin data struct
    // the handle just needs to be unique
    int get_handle()       { return SHMOFF(pin); }
    int get_linkstatus()   { return (pin->signal != 0); }
    const char *get_owner() {
	hal_comp_t *comp = (hal_comp_t *)SHMPTR(pin->owner_ptr);
	return comp->name;
    }

    bp::object get_value() {
	hal_data_u *dp;

	if (pin->signal != 0) {
	    hal_sig_t *sig;

	    sig = (hal_sig_t *) SHMPTR(pin->signal);
	    dp = (hal_data_u *) SHMPTR(sig->data_ptr);
	} else {
	    dp = (hal_data_u *)(hal_shmem_base + SHMOFF(&(pin->dummysig)));
	}
	return hal2py(pin->type, dp);
    };

    void set_value(bp::object value) {
	hal_data_u *dp;
	if (pin->signal != 0) {
	    hal_sig_t *sig;

	    sig = (hal_sig_t *)SHMPTR(pin->signal);
	    dp = (hal_data_u *)SHMPTR(sig->data_ptr);
	} else {
	    dp = (hal_data_u *)(hal_shmem_base + SHMOFF(&(pin->dummysig)));
	}
	py2hal(pin->type, dp, value);
    };
};

//------------------- ring generic operations --------------------------

Ring::~Ring() {
    int retval;
    if ((retval = hal_ring_detach(rname.c_str(), &rb))) {
	PyErr_Format(PyExc_NameError, "hal_ring_detach() failed: '%s': %s",
		     rname.c_str(), strerror(-retval));
	throw boost::python::error_already_set();
    }
}

Ring::Ring(const char *name, ringbuffer_t rbuf) : rb(rbuf), rname(name)
{
}

bp::object Ring::scratchpad()
{
    size_t ss = ring_scratchpad_size(&rb);
    if (ss == 0)
	return bp::object();
    bp::handle<> h(PyString_FromStringAndSize((const char *)rb.scratchpad,
					      ss));
    return bp::object(h);
}

int Ring::get_reader()       { return rb.header->reader; }
void Ring::set_reader(int r) { rb.header->reader = r; }
int Ring::get_writer()       { return rb.header->writer; }
void Ring::set_writer(int w) { rb.header->writer = w; }
int Ring::get_size()         { return rb.header->size; }
int Ring::get_spsize()       { return ring_scratchpad_size(&rb); }

int  Ring::get_type()        { return rb.header->type; }
bool Ring::get_rmutex_mode() { return ring_use_rmutex(&rb); }
bool Ring::get_wmutex_mode() { return ring_use_wmutex(&rb); }
bool Ring::get_in_halmem()   { return in_halmem; }
std::string Ring::get_name() { return rname; }

//------------------- frame ring operations --------------------------

RecordRing::RecordRing(const char *name,
		       ringbuffer_t &rbuffer)
    : Ring(name,rbuffer) {}

bp::object RecordRing::next_size() {
    int retval;
    if ((retval = record_next_size(&rb)) > -1)
	return bp::object(retval);
    return bp::object();
}

int RecordRing::shift()               { return record_shift(&rb); }
const size_t RecordRing::available()  { return record_write_space(rb.header); }
const size_t RecordRing::flush()      { return record_flush(&rb); }

int RecordRing::write(char *buf, size_t size) {
    int retval;

    if ((retval = record_write(&rb, buf, size)) == ERANGE) {

	PyErr_Format(PyExc_IOError,
		     "write: frame size %zu greater than buffer size %zu",
		     size, rb.header->size);
	throw boost::python::error_already_set();
    }
    // may return EAGAIN:  currently not enough space in ring buffer (temporary)
    return retval;
}

bp::object RecordRing::next_buffer()
{
    ring_size_t size = record_next_size(&rb);
    if (size < 0)
	return bp::object();
    bp::handle<> h(PyString_FromStringAndSize((const char *)record_next(&rb), size));
    return bp::object(h);
}

#if 0
//------------------- multipart ring operations --------------------------

MultipartRing::MultipartRing(const char *name, ringbuffer_t &rbuffer)
    :  Ring(name,rbuffer) {
    bring_init(&mpr, &rb);
}


MultipartRing::~MultipartRing() {
}

int MultipartRing::append_frame(char *buf, size_t size, int flags) {

    ringvec_t arg =  { .rv_base = buf, .rv_len = size, .rv_flags = flags};

    int retval =  bring_write(&mpr, &arg);

    switch (retval) {
    case EINVAL:
	PyErr_Format(PyExc_IOError, "append: invalid value (EINVAL)");
	throw boost::python::error_already_set();
	break;
    case EAGAIN:
	// from frame_write_begin()
	PyErr_Format(PyExc_IOError, "append: currently insufficient space (EAGAIN)");
	throw boost::python::error_already_set();
	break;
    case ERANGE:
	// from frame_write_begin()
	PyErr_Format(PyExc_IOError,
		     "append: size exceeds ringbuffer size %d/%d (ERANGE)",
		     size, mpr.ring->header->size);
	throw boost::python::error_already_set();
	break;
    default: ;
    }
    return retval;
}

size_t MultipartRing::commit() {
    return bring_write_flush(&mpr); // XXX can this fail?
}

bp::list MultipartRing::read_multipart() {
    ringvec_t frame;
    bp::list msg;
    do {
	if (bring_read(&mpr, &frame))
	    break;
	bp::handle<> h(PyString_FromStringAndSize((const char *)frame.rv_base, frame.rv_len));
	msg.append(make_tuple(bp::object(h), (int) frame.rv_flags));
    } while(1);
    return msg;
}

bp::object MultipartRing::next_size() {
    int retval;
    if ((retval = frame_next_size(mpr.ring)) > -1)
	return bp::object(retval);
    return bp::object();
}
#endif

//------------------- stream ring operations --------------------------

StreamRing::StreamRing(const char *name,
		       ringbuffer_t &rbuffer)
    : Ring(name,rbuffer) {}

const size_t StreamRing::available()  { return stream_write_space(rb.header); }
const size_t StreamRing::flush()      { return stream_flush(&rb); }

void StreamRing::consume(int nbytes) {
    size_t avail;

    avail = stream_read_space(rb.header);
    if (nbytes > (int) avail) {
	PyErr_Format(PyExc_IOError,
		     "shift(%d): argument larger than bytes available (%zu)",
		     nbytes, avail);
	throw boost::python::error_already_set();
    }
    stream_read_advance(&rb, nbytes);
}

int StreamRing::write(char *buf, size_t size) {
    unsigned rsize = stream_write(&rb, buf, size);
    return  (rsize != size) ? rsize : 0;
}

bp::object StreamRing::next_buffer()
{
    ringvec_t vec[2];

    stream_get_read_vector(&rb, vec);
    if (vec[0].rv_len) {
	bp::handle<> h(PyString_FromStringAndSize((const char *)vec[0].rv_base,
						  vec[0].rv_len));
	if (vec[1].rv_len == 0) {
	    stream_read_advance(&rb, vec[0].rv_len);
	    return bp::object(h);
	} else {
	    bp::handle<> h2(PyString_FromStringAndSize((const char *)vec[1].rv_base,
						       vec[1].rv_len));
	    stream_read_advance(&rb, vec[0].rv_len + vec[1].rv_len);
	    return bp::object(h) + bp::object(h2);
	}
    } else
	return bp::object();
}

//----------------------- end ring ops ----------------------

halitem *HalComponent::find_item(std::string name) {
    itemmap::iterator i = items->find(name);
    if (i == items->end()) {
	PyErr_Format(PyExc_RuntimeError, "Pin '%s' does not exist", name.c_str());
	throw boost::python::error_already_set();
    }
    return &(i->second);
}

// this must be called with the hal_data mutex held
HalComponent::HalComponent(hal_comp_t *c) : comp(c),ccomp(NULL),prefix(NULL) {
    items = new itemmap();
    hal_pin_t *pin;
    hal_param_t *param;
    hal_comp_t *owner;
    int next;
    bp::list result;
    halitem pinitem;

    next = hal_data->pin_list_ptr;
    while (next != 0) {
	pin = (hal_pin_t *)SHMPTR(next);
	owner = (hal_comp_t *) SHMPTR(pin->owner_ptr);
	if (owner->comp_id == comp->comp_id) {
	    pinitem.is_pin = true;
	    pinitem.pp.pin = pin;
	    (*items)[pin->name] = pinitem;
	}
	next = pin->next_ptr;
    }
    next = hal_data->param_list_ptr;
    while (next != 0) {
	param = (hal_param_t *)SHMPTR(next);
	owner = (hal_comp_t *) SHMPTR(param->owner_ptr);
	if (owner->comp_id == comp->comp_id) {
	    pinitem.is_pin = false;
	    pinitem.pp.param = param;
	    (*items)[param->name] = pinitem;
	}
	next = param->next_ptr;
    }
}

HalComponent::HalComponent(char *name, int type, char *prefix, int arg1, int arg2) :
    ccomp(NULL), prefix(prefix)  {
    items = new itemmap();
    int comp_id = hal_init_mode(name, type, arg1, arg2);
    if (comp_id < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_init(%s, %d) failed: %s",
		     name, type, strerror(-comp_id));
	throw boost::python::error_already_set();
    }
    {
	int dummy __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));
	comp = halpr_find_comp_by_id(comp_id);
	assert(comp != NULL);
    }
}

HalComponent::~HalComponent()
{
    hal_ccomp_free(ccomp);
}

void HalComponent::newpin(const char *name, int type, int dir)  {
    int result;
    halitem pinitem;

    if (type < HAL_BIT || type > HAL_U32) {
	PyErr_Format(PyExc_RuntimeError,
		     "Invalid pin type %d", type);
	throw boost::python::error_already_set();
    }
    pinitem.is_pin = true;
    pinitem.ptr =  hal_malloc(sizeof(void *));
    if (!pinitem.ptr)
	throw std::runtime_error("hal_malloc failed");

    result = hal_pin_new(name, (hal_type_t) type, (hal_pin_dir_t) dir,
			 (void **) pinitem.ptr, comp->comp_id);
    if (result < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_pin_new(%s, %d) failed: %s",
		     name, type, strerror(-result));
	throw boost::python::error_already_set();
    }

    pinitem.pp.pin = halpr_find_pin_by_name(name);
    assert(pinitem.pp.pin != NULL);
    (*items)[name] = pinitem;
}

void HalComponent::newparam(const char *name, int type, int dir)  {
    int result;
    halitem paramitem;

    if (type < HAL_BIT || type > HAL_U32) {
	PyErr_Format(PyExc_RuntimeError,
		     "Invalid param type %d", type);
	throw boost::python::error_already_set();
    }
    paramitem.is_pin = false;
    paramitem.ptr = hal_malloc(sizeof(void *));
    if (!paramitem.ptr)
	throw std::runtime_error("hal_malloc failed");

    result = hal_param_new(name, (hal_type_t) type, (hal_param_dir_t) dir,
			   (void *) paramitem.ptr, comp->comp_id);
    if (result < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_param_new(%s, %d) failed: %s",
		     name, type, strerror(-result));
	throw boost::python::error_already_set();
    }
    paramitem.pp.param = halpr_find_param_by_name(name);
    assert(pinitem.pp.param != NULL);
    (*items)[name] = paramitem;
}

void HalComponent::ready() {
    int result = hal_ready(comp->comp_id);
    if (result < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_ready(%d) failed: %s",
		     comp->comp_id, strerror(-result));
	throw boost::python::error_already_set();
    }
}

void HalComponent::exit() {
    int result = hal_exit(comp->comp_id);
    if (result < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_exit(%d) failed: %s",
		     comp->comp_id, strerror(-result));
	throw boost::python::error_already_set();
    }
}

int HalComponent::get_id() {
    return comp->comp_id;
}

int HalComponent::get_type() {
    return comp->type;
}

void HalComponent::bind() {
    int result = hal_bind(comp->name);
    if (result < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_bind(%s) failed: %s",
		     comp->name, strerror(-result));
	throw boost::python::error_already_set();
    }
}

void HalComponent::unbind() {
    int result = hal_unbind(comp->name);
    if (result < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_unbind(%s) failed: %s",
		     comp->name, strerror(-result));
	throw boost::python::error_already_set();
    }
}

void HalComponent::acquire() {
    int result = hal_acquire(comp->name, getpid());
    if (result < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_acquire(%s) failed: %s",
		     comp->name, strerror(-result));
	throw boost::python::error_already_set();
    }
}

void HalComponent::release() {
    int result = hal_release(comp->name);
    if (result < 0) {
	PyErr_Format(PyExc_RuntimeError,
		     "hal_release(%s) failed: %s",
		     comp->name, strerror(-result));
	throw boost::python::error_already_set();
    }
}

bp::list HalComponent::params() {
    hal_param_t *param __attribute__((cleanup(halpr_autorelease_mutex)));
    hal_comp_t *owner;
    int next;
    bp::list result;

    rtapi_mutex_get(&(hal_data->mutex));

    next = hal_data->param_list_ptr;
    while (next != 0) {
	param = (hal_param_t *)SHMPTR(next);
	owner = (hal_comp_t *) SHMPTR(param->owner_ptr);
	if (owner->comp_id == comp->comp_id)
	    result.append(HalParam(param));
	next = param->next_ptr;
    }
    return result;
};

bp::list HalComponent::pins() {
    hal_pin_t *pin __attribute__((cleanup(halpr_autorelease_mutex)));
    hal_comp_t *owner;
    int next;
    bp::list result;

    rtapi_mutex_get(&(hal_data->mutex));

    next = hal_data->pin_list_ptr;
    while (next != 0) {
	pin = (hal_pin_t *)SHMPTR(next);
	owner = (hal_comp_t *) SHMPTR(pin->owner_ptr);
	if (owner->comp_id == comp->comp_id)
	    result.append(HalPin(pin));
	next = pin->next_ptr;
    }
    return result;
};

bp::list HalComponent::changed_pins() {
    int retval;
    bp::list result;

    if (ccomp == NULL)
	retval = hal_compile_comp(comp->name, &ccomp);
    assert(ccomp != NULL);
    if (!hal_ccomp_match(ccomp))
	return bp::list();
    for (int i = 0; i < ccomp->n_pins; i++) {
	if (RTAPI_BIT_TEST(ccomp->changed, i))
	    result.append(HalPin(ccomp->pin[i]));
    }
    return result;
}

const char *HalComponent::get_name() { return comp->name; }
int HalComponent::get_state()        { return comp->state; }
int HalComponent::get_pid()          { return comp->pid; }
int HalComponent::get_arg1()          { return comp->userarg1; }
int HalComponent::get_arg2()          { return comp->userarg2; }
int HalComponent::get_last_bound()   { return comp->last_bound; }
int HalComponent::get_last_unbound() { return comp->last_unbound; }
int HalComponent::get_last_update()  { return comp->last_update; }
int HalComponent::set_last_update(int timestamp) { return (comp->last_update = timestamp); }

bp::object HalComponent::getitem( bp::object index) {
    if (PyObject_IsInstance(index.ptr(),
			    (PyObject*)&PyString_Type)) {
	halitem *hi = find_item(bp::extract<std::string>(index));
	hal_data_u *dp;

	if (hi->is_pin) {
	    if (hi->pp.pin->signal != 0) {
		hal_sig_t *sig = (hal_sig_t *) SHMPTR(hi->pp.pin->signal);
		dp = (hal_data_u *) SHMPTR(sig->data_ptr);
	    } else {
		dp = (hal_data_u *)(hal_shmem_base + SHMOFF(&(hi->pp.pin->dummysig)));
	    }
	    return hal2py(hi->pp.pin->type, dp);
	} else
	    return hal2py(hi->pp.param->type,
			  ((hal_data_u*) SHMPTR(hi->pp.param->data_ptr)));
    } else
	throw std::runtime_error("component subscript type must be string");
}

bp::object HalComponent::setitem( bp::object index, bp::object value) {
    if (PyObject_IsInstance(index.ptr(),
			    (PyObject*)&PyString_Type)) {
	halitem *hi = find_item(bp::extract<std::string>(index));
	hal_data_u *dp;

	if (hi->is_pin) {
	    if (hi->pp.pin->signal != 0) {
		hal_sig_t *sig = (hal_sig_t *) SHMPTR(hi->pp.pin->signal);
		dp = (hal_data_u *) SHMPTR(sig->data_ptr);
	    } else {
		dp = (hal_data_u *)(hal_shmem_base + SHMOFF(&(hi->pp.pin->dummysig)));
	    }
	    return py2hal(hi->pp.pin->type, dp, value);
	} else
	    return py2hal(hi->pp.param->type,
			  ((hal_data_u*) SHMPTR(hi->pp.param->data_ptr)),
			  value);
    } else
	throw std::runtime_error("component subscript type must be string");
}

ring_ptr HalComponent::ring_create(char *name,
				   size_t size,
				   size_t spsize,
				   int type,
				   bool use_rmutex,
				   bool use_wmutex,
				   bool in_halmem)
{
    int arg = 0;
    ringbuffer_t ringbuf;
    int retval;
    ring_ptr r;

    arg = (type & RINGTYPE_MASK);
    if (use_rmutex)
	arg |= USE_RMUTEX;
    if (use_wmutex)
	arg |= USE_RMUTEX;
    if (in_halmem)
	arg |= ALLOC_HALMEM;

    if ((retval = hal_ring_new(name, size, spsize, arg)) < 0) {
	PyErr_Format(PyExc_NameError, "hal_ring_new(): can't create ring '%s': %s",
		     name, strerror(-retval));
	throw boost::python::error_already_set();
    }
    unsigned flags; // FIXME make Ring member
    if (hal_ring_attach(name, &ringbuf,  &flags) < 0) {
	PyErr_Format(PyExc_NameError, "hal_ring_attach(): no such ring: '%s': %s",
		       name, strerror(-retval));
	throw boost::python::error_already_set();
    }
    switch (type) {
    case RINGTYPE_RECORD:
	r = boost::shared_ptr<RecordRing>(new RecordRing(name, ringbuf));
	break;
    // case RINGTYPE_MULTIPART:
    // 	r = boost::shared_ptr<MultipartRing>(new MultipartRing(name, ringbuf));
    // 	break;
    case RINGTYPE_STREAM:
	r = boost::shared_ptr<StreamRing>(new StreamRing(name, ringbuf));
    }
    {
	int dummy __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));
	r->halring = halpr_find_ring_by_name(name);
	r->in_halmem = (r->halring->flags & ALLOC_HALMEM) != 0;
    }
    return r;
}

ring_ptr HalComponent::ring_attach(char *name, int type)
{
    int retval;
    ringbuffer_t rbuf;
    ring_ptr r;
    unsigned flags; // FIXME make Ring member

    if ((retval = hal_ring_attach(name, &rbuf, &flags))) {
	PyErr_Format(PyExc_NameError, "hal_ring_attach(): no such ring: '%s': %s",
		     name, strerror(-retval));
	throw boost::python::error_already_set();
    }

    switch (type) {
    case RINGTYPE_RECORD:
	r = boost::shared_ptr<RecordRing>(new RecordRing(name, rbuf));
	break;
    // case RINGTYPE_MULTIPART:
    // 	if (rbuf.header->type == RINGTYPE_STREAM) {
    // 	    PyErr_Format(PyExc_NameError, "ring_attach(%s): cant multipart-attach to a stream ring",
    // 			 name);
    // 	    throw boost::python::error_already_set();
    // 	}
    // 	r = boost::shared_ptr<MultipartRing>(new MultipartRing(name, rbuf));
    // 	break;
    case RINGTYPE_STREAM:
	r = boost::shared_ptr<StreamRing>(new StreamRing(name, rbuf));
    }
    rbuf.header->type = type;
    {
	int dummy __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));
	r->halring = halpr_find_ring_by_name(name);
	r->in_halmem = (r->halring->flags & ALLOC_HALMEM) != 0;
    }
    return r;
}

void HalComponent::ring_detach(Ring &r)
{
    int retval;
    if ((retval = hal_ring_detach(r.rname.c_str(), &r.rb))) {
	PyErr_Format(PyExc_NameError, "hal_ring_detach(): no such ring: '%s': %s",
		     r.rname.c_str(), strerror(-retval));
	throw boost::python::error_already_set();
    }
}


class HalSignal {

public:
    hal_sig_t *sig;
    HalSignal(hal_sig_t *s) : sig(s) {}
    HalSignal(const char *name, int type)  {
	int result;

	if ((result = hal_signal_new(name, (hal_type_t) type)) < 0) {
	    PyErr_Format(PyExc_RuntimeError,
			 "hal_signal_new(%s, %d) failed: %s",
			 name, type, strerror(-result));
	    throw boost::python::error_already_set();
	}
	{
	    int dummy  __attribute__((cleanup(halpr_autorelease_mutex)));
	    rtapi_mutex_get(&(hal_data->mutex));
	    sig = halpr_find_sig_by_name(name);
	    assert(sig != NULL);
	}
    }
    const char *get_name() { return sig->name; }
    int get_type()         { return sig->type; }
    int get_readers()      { return sig->readers; }
    int get_writers()      { return sig->writers; }
    int get_bidirs()       { return sig->bidirs; }
    int get_handle()       { return SHMOFF(sig); }

    bp::object get_value() {
	hal_data_u *dp = (hal_data_u *) SHMPTR(sig->data_ptr);
	return hal2py(sig->type, dp);
    };

    void set_value(bp::object value) {
	hal_data_u *dp = (hal_data_u *) SHMPTR(sig->data_ptr);
	py2hal(sig->type, dp, value);
    };
};

static HalSignal *newsig_factory(const char *name, int type)
{
    return new HalSignal(name, type);
}

class HalGroup;

class HalMember {
    hal_member_t *hm;
public:
    HalMember(hal_member_t *member) : hm(member) {}
    int  get_userarg1()          { return hm->userarg1; }
    double get_epsilon()         { return hm->epsilon; }
    void set_epsilon(double eps) { hm->epsilon = eps; }
    void set_userarg1(int arg)   { hm->userarg1 = arg; }
    int get_handle()             { return SHMOFF(hm); }
    int get_type() {
	if (hm->sig_member_ptr)
	    return (int) HAL_SIGNAL;
	if (hm->group_member_ptr)
	    return (int) HAL_GROUP;
	throw std::runtime_error("HalMember.type() neither group nor signal");
    }
};

static int list_members_cb(int level, hal_group_t **groups,
			   hal_member_t *member,
			   void *cb_data)
{
    bp::list *result = static_cast<bp::list *>(cb_data);
    result->append(HalSignal((hal_sig_t *)SHMPTR(member->sig_member_ptr)));
    return 0;
}

class HalGroup  {
private:
    hal_group_t *hg;
    hal_compiled_group_t *cg;
public:
    HalGroup(const char *name, int arg1, int arg2 ) :
	hg(NULL), cg(NULL) {
	int result = hal_group_new(name, arg1, arg2);
	if (result < 0) {
	    PyErr_Format(PyExc_RuntimeError,
			 "HalGroup(%s,%d,%d) failed: %s",
			 name,arg1,arg2,strerror(-result) );
	    throw boost::python::error_already_set();
	}
	{
	    int dummy  __attribute__((cleanup(halpr_autorelease_mutex)));
	    rtapi_mutex_get(&(hal_data->mutex));
	    hg = halpr_find_group_by_name(name);
	}
    }
    HalGroup(hal_group_t *group) : hg(group), cg(NULL) {}
    const char *get_name()   { return hg->name;}
    int get_refcount()       { return hg->refcount; }
    int get_userarg1()       { return hg->userarg1; }
    int get_userarg2()       { return hg->userarg2; }
    int get_handle()         { return SHMOFF(hg); }
    void set_refcount(int r) { hg->refcount = r; }
    void set_userarg1(int r) { hg->userarg1 = r; }
    void set_userarg2(int r) { hg->userarg2 = r; }
    bp::list members()     {
	bp::list m;
	halpr_foreach_member(hg->name, list_members_cb,
				  static_cast<void*>(&m), RESOLVE_NESTED_GROUPS);
	return m;
    }
    bp::list changed() {
	if (cg == NULL)
	    compile();
	if (cg->n_monitored == 0)
	    return bp::list();

	if (hal_cgroup_match(cg) == 0)
	    return bp::list();

	bp::list changed;
	for (int i = 0; i < cg->n_members; i++)
	    if (RTAPI_BIT_TEST(cg->changed, i))
		changed.append(HalSignal((hal_sig_t *)
					 SHMPTR(cg->member[i]->sig_member_ptr)));
	return changed;
    }
    void compile() {
	int result  __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));

	hal_cgroup_free(cg);
	if ((result = halpr_group_compile(hg->name, &cg)) < 0) {
	    PyErr_Format(PyExc_RuntimeError,
			 "hal_group_compile(%s) failed: %s",
			 hg->name, strerror(-result) );
	    throw boost::python::error_already_set();
	}
    }
};


using namespace boost::python;

static  int group_cb(hal_group_t *group, void *cb_data)
{
    bp::dict *result = static_cast<bp::dict *>(cb_data);
    (*result)[(const char *)group->name] = HalGroup(group);
    return 0;
}

static bp::object groups(void)
{
    bp::dict msgs;
    int n __attribute__((cleanup(halpr_autorelease_mutex)));
    rtapi_mutex_get(&(hal_data->mutex));
    n = halpr_foreach_group(NULL,group_cb, static_cast<void*>(&msgs));
    return msgs;
}

static bp::dict components(void)
{
    hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));
    int next;
    bp::dict result;

    rtapi_mutex_get(&(hal_data->mutex));

    next = hal_data->comp_list_ptr;
    while (next != 0) {
	comp = (hal_comp_t *)SHMPTR(next);
	result[comp->name] = HalComponent(comp);
	next = comp->next_ptr;
    }
    return result;
}

static bp::dict signals(void)
{
    hal_sig_t *sig __attribute__((cleanup(halpr_autorelease_mutex)));
    int next;
    bp::dict result;

    rtapi_mutex_get(&(hal_data->mutex));

    next = hal_data->sig_list_ptr;
    while (next != 0) {
	sig = (hal_sig_t *)SHMPTR(next);
	result[sig->name] = HalSignal(sig);
	next = sig->next_ptr;
    }
    return result;
}

static bp::list ring_names (void)
{
    bp::list result;
    hal_ring_t *ring __attribute__((cleanup(halpr_autorelease_mutex)));
    int next;

    rtapi_mutex_get(&(hal_data->mutex));
    next = hal_data->ring_list_ptr;
    while (next != 0) {
	ring = (hal_ring_t *) SHMPTR(next);
	result.append(ring->name);
	next = ring->next_ptr;
    }
    return result;
}

static int halmutex(void) { return hal_data->mutex; }

static bp::object net(bp::tuple args, bp::dict kwargs);

//BOOST_PYTHON_MEMBER_FUNCTION_OVERLOADS(append_overloads, append_frame, 2,3);

BOOST_PYTHON_MODULE(halext) {

    scope().attr("HAL_PIN")    = (int) HAL_PIN;
    scope().attr("HAL_SIGNAL") = (int) HAL_SIGNAL;
    scope().attr("HAL_PARAM") = (int)   HAL_PARAM;
    scope().attr("HAL_THREAD") = (int)  HAL_THREAD;
    scope().attr("HAL_FUNCT") = (int)  HAL_FUNCT;
    scope().attr("HAL_ALIAS") = (int)  HAL_ALIAS;
    scope().attr("HAL_COMP_RT") = (int) HAL_COMP_RT;
    scope().attr("HAL_COMP_USER") = (int) HAL_COMP_USER;
    scope().attr("HAL_COMP_REMOTE") = (int) HAL_COMP_REMOTE;
    scope().attr("HAL_RING") = (int)   HAL_RING;
    scope().attr("HAL_GROUP") = (int)  HAL_GROUP;
    scope().attr("HAL_MEMBER_SIGNAL") = (int) HAL_MEMBER_SIGNAL;
    scope().attr("HAL_MEMBER_GROUP") = (int) HAL_MEMBER_GROUP;
    scope().attr("HAL_MEMBER_PIN") = (int)  HAL_MEMBER_PIN;

    scope().attr("RINGTYPE_RECORD") = (int) RINGTYPE_RECORD;
    scope().attr("RINGTYPE_STREAM") = (int) RINGTYPE_STREAM;
    scope().attr("RINGTYPE_MULTIPART") = (int) RINGTYPE_MULTIPART;

    scope().attr("HAL_FLOAT") = (int) HAL_FLOAT;
    scope().attr("HAL_BIT") = (int) HAL_BIT;
    scope().attr("HAL_S32") = (int) HAL_S32;
    scope().attr("HAL_U32") = (int) HAL_U32;
    scope().attr("HAL_GROUP") = (int) HAL_GROUP;

    scope().attr("HAL_IN") = (int) HAL_IN;
    scope().attr("HAL_OUT") = (int) HAL_OUT;
    scope().attr("HAL_IO") = (int) HAL_IO;

    scope().attr("HAL_RO") = (int) HAL_RO;
    scope().attr("HAL_RW") = (int) HAL_RW;

    scope().attr("TYPE_RT") = (int) TYPE_RT;
    scope().attr("TYPE_USER") = (int) TYPE_USER;
    scope().attr("TYPE_REMOTE") = (int) TYPE_REMOTE;

    scope().attr("COMP_INITIALIZING") = (int) COMP_INITIALIZING;
    scope().attr("COMP_UNBOUND") = (int) COMP_UNBOUND;
    scope().attr("COMP_BOUND") = (int) COMP_BOUND;
    scope().attr("COMP_READY") = (int) COMP_READY;

    //    scope().attr("MODE_STREAM") = MODE_STREAM;
    scope().attr("USE_RMUTEX") = USE_RMUTEX;
    scope().attr("USE_WMUTEX") = USE_WMUTEX;
    scope().attr("ALLOC_HALMEM") = ALLOC_HALMEM;

    scope().attr("__doc__") = "extended HAL bindings";

    class_<Ring,boost::noncopyable, ring_ptr>("Ring", no_init)
	.add_property("writer", &Ring::get_writer, &Ring::set_writer,
		      "ring writer attribute. Advisory in nature.")
	.add_property("reader", &Ring::get_reader, &Ring::set_reader,
		      "ring reader attribute. Advisory in nature.")
	.add_property("size",  &Ring::get_size,
		      "buffer size allocated for this ring.")
	.add_property("scratchpad_size",  &Ring::get_spsize,
		      "return the scratchpad size")
	.add_property("scratchpad",  &Ring::scratchpad,
		      "return the scratchpad buffer. None if this "
		      "ring doesnt have a scratchpad buffer.")
	.add_property("name",  &Ring::get_name,
		      "HAL name of this ring")
	.add_property("type",  &Ring::get_type,
		      "returns the type of ring.")
	.add_property("use_rmutex",  &Ring::get_rmutex_mode,
		      "'use reader mutex' atrribute. "
		      "Advisory in nature.")
	.add_property("use_wmutex",  &Ring::get_wmutex_mode,
		      "'use writer mutex' atrribute. "
		      "Advisory in nature.")
	.add_property("in_halmem",  &Ring::get_in_halmem,
		      "'ring allocated in HAL shared memory if true. ")
	;

    class_<RecordRing,boost::noncopyable, framering_ptr,
	bp::bases<Ring> >("RecordRing", no_init)
	.def("__iter__", &RecordRing::__iter__)
	.def("next", &RecordRing::next_buffer,
	     "returns the size of the next frame, "
	     "or -1 if no data is available. "
	     "Note in Frame mode, 0 is a legit frame size.")
	.def("write", &RecordRing::write,
	     "write to ring. Returns 0 on success."
	     "a non-zero return value indicates the write failed due to lack of "
	     "buffer space, and should be retried later. An oversized "
	     "frame (larger than buffer size) will raise an IOError "
	     "exception.")
	.def("flush", &RecordRing::flush,
	     "clear the buffer contents. Note this is not thread-safe"
	     " unless all readers and writers use a r/w mutex.")
	.def("available", &RecordRing::available,
	     "return the size of the largest frame which can"
	     " safely be written.")
	.def("next_buffer", &RecordRing::next_buffer,
	     "Return the next frame, or None. "
	     "this is a 'peek read' - "
	     "data is not actually removed from the buffer "
	     "until shift is executed.")
	.def("next_size", &RecordRing::next_size,
	     "Return size of the next frame. Int. "
	     "Zero is a valid frame length. "
	     "If the buffer is empty, return None.")

	.def("shift", &RecordRing::shift,
	     "consume the current frame.")
	;

    // class_<MultipartRing,boost::noncopyable, mpring_ptr,
    // 	bp::bases<Ring> >("MultipartRing", no_init)

    // 	.def("append", &MultipartRing::append_frame, //append_overloads(),
    // 	     "add a frame to a multipart message. Returns 0 on success.")
    // 	.def("commit", &MultipartRing::commit,
    // 	     "finish a multipart message")
    // 	.def("next_size", &MultipartRing::next_size,
    // 	     "Return size of the next message, including"
    // 	     "all overhead."
    // 	     "If the buffer is empty, return None.")
    // 	.def("read", &MultipartRing::read_multipart,
    // 	     "read a multipart message."
    // 	     "returns a list of tuples (frame, flags)")
    // 	;

    class_<StreamRing,boost::noncopyable, streamring_ptr,
	bp::bases<Ring> >("StreamRing", no_init)

	.def("next", &StreamRing::next_buffer,
	     "returns the number of bytes readable"
	     "or -1 if no data is available.")
	.def("write", &StreamRing::write,
	     "write to ring. Returns 0 on success."
	     "nozero return value indicates the number"
	     " of bytes actually written. An oversized "
	     "write (larger than buffer size) will raise an IOError "
	     "exception.")
	.def("flush", &StreamRing::flush,
	     "clear the buffer contents. Note this is not thread-safe"
	     " unless all readers and writers use a r/w mutex.")
	.def("available", &StreamRing::available,
	     "return number of bytes available to write.")
	.def("next_buffer", &StreamRing::next_buffer,
	     "Return all available bytes"
	     " as a Buffer  object, or None. "
	     "this is a 'peek read' - "
	     "data is not actually removed from the buffer "
	     "until shift(number of bytes) is executed.")
	.def("consume", &StreamRing::consume,
	     "remove argument number of bytes from stream. "
	     "May raise IOError if more than the number of "
	     "available bytes are consumed,")
	;

    class_<RingIter>("RingIter", init<const RecordRing &>())
	.def("valid", &RingIter::valid,
	     "determine if iterator still valid;",
	     "a concurrent read operation will invalidate the iterator.")
	.def("__iter__", &pass_through)
	.def("next", &RingIter::next);
    ;

    class_<HalComponent>("HalComponent", init<char *, optional<int, char *, int, int> >())
	.add_property("state", &HalComponent::get_state)
	.add_property("pid", &HalComponent::get_pid)
	.add_property("arg1", &HalComponent::get_arg1)
	.add_property("arg2", &HalComponent::get_arg2)
	.add_property("name", &HalComponent::get_name)
	.add_property("id", &HalComponent::get_id)
	.add_property("type", &HalComponent::get_type)
	.def("bind", &HalComponent::bind)
	.def("unbind", &HalComponent::unbind)
	.def("acquire", &HalComponent::acquire)
	.def("release", &HalComponent::release)
	.def("ready", &HalComponent::ready)
	.def("exit", &HalComponent::exit)
	.def("newpin", &HalComponent::newpin)
	.def("newparam", &HalComponent::newparam)
	.def("pins", &HalComponent::pins)
	.def("params", &HalComponent::params)
	.def("changed_pins", &HalComponent::changed_pins)
	.add_property("last_update", &HalComponent::get_last_update,
		      &HalComponent::set_last_update)
	.add_property("last_bound", &HalComponent::get_last_bound)
	.add_property("last_unbound", &HalComponent::get_last_unbound)

	.def("__getitem__", &HalComponent::getitem)
	.def("__setitem__", &HalComponent::setitem)

	.def("create",  &HalComponent::ring_create,
	     (bp::arg("size") = DEFAULT_RING_SIZE,
	      bp::arg("scratchpad") = 0,
	      bp::arg("type") = RINGTYPE_RECORD,
	      bp::arg("use_rmutex") = false,
	      bp::arg("use_wmutex") = false,
	      bp::arg("in_halmem") = false
	      ))

	.def("attach",  &HalComponent::ring_attach)
	.def("detach",  &HalComponent::ring_detach)
	;

    class_<HalPin>("HalPin",no_init)
	.add_property("name", &HalPin::get_name)
	.add_property("value", &HalPin::get_value, &HalPin::set_value)
	.add_property("type", &HalPin::get_type)
	.add_property("dir", &HalPin::get_dir)
	.add_property("flags", &HalPin::get_flags)
	.add_property("epsilon", &HalPin::get_epsilon)
	.add_property("handle", &HalPin::get_handle)
	.add_property("linked", &HalPin::get_linkstatus)
	.add_property("owner", &HalPin::get_owner)
	;

    class_<HalParam>("HalParam",no_init)
	.add_property("name", &HalParam::get_name)
	.add_property("value", &HalParam::get_value, &HalParam::set_value)
	.add_property("type", &HalParam::get_type)
	.add_property("dir", &HalParam::get_dir)
	.add_property("owner", &HalParam::get_owner)
	.add_property("handle", &HalParam::get_handle)
	;

    class_<HalSignal>("HalSignal",no_init)
	.add_property("type", &HalSignal::get_type)
	.add_property("readers", &HalSignal::get_readers)
	.add_property("writers", &HalSignal::get_writers)
	.add_property("bidirs",  &HalSignal::get_bidirs)
	.add_property("name", &HalSignal::get_name)
	.add_property("value", &HalSignal::get_value,&HalSignal::set_value)
	.add_property("handle", &HalSignal::get_handle)
	;

    class_<HalMember>("Member",no_init)
	.add_property("userarg1", &HalMember::get_userarg1, &HalMember::set_userarg1)
	.add_property("epsilon", &HalMember::get_epsilon, &HalMember::set_epsilon)
	.add_property("type", &HalMember::get_type)
	.add_property("handle", &HalMember::get_handle)
	;

    class_<HalGroup>("HalGroup",no_init)
	.add_property("name", &HalGroup::get_name)
	.add_property("refcount", &HalGroup::get_refcount, &HalGroup::set_refcount)
	.add_property("userarg1", &HalGroup::get_userarg1, &HalGroup::set_userarg1)
	.add_property("userarg2", &HalGroup::get_userarg2, &HalGroup::set_userarg2)
	.add_property("handle", &HalGroup::get_handle)
	.def("compile", &HalGroup::compile)
	.def("changed", &HalGroup::changed)
	.def("members", &HalGroup::members)
	;

    def("rings", ring_names);
    def("groups", groups);
    def("components", components);
    def("link", hal_link);
    def("unlink", hal_unlink);
    def("net", bp::raw_function(&net));
    def("signals", signals);
    def("newsig", newsig_factory, return_value_policy<manage_new_object>());
    def("halmutex", halmutex); // debugging aid
}

static const char *data_type2(int type)
{
    const char *type_str;

    switch (type) {
    case HAL_BIT:
	type_str = "bit";
	break;
    case HAL_FLOAT:
	type_str = "float";
	break;
    case HAL_S32:
	type_str = "s32";
	break;
    case HAL_U32:
	type_str = "u32";
	break;
    default:
	/* Shouldn't get here, but just in case... */
	type_str = "undef";
    }
    return type_str;
}

/* Switch function for pin direction for the print_*_list functions  */
static const char *pin_data_dir(int dir)
{
    const char *pin_dir;

    switch (dir) {
    case HAL_IN:
	pin_dir = "IN";
	break;
    case HAL_OUT:
	pin_dir = "OUT";
	break;
    case HAL_IO:
	pin_dir = "I/O";
	break;
    default:
	/* Shouldn't get here, but just in case... */
	pin_dir = "???";
    }
    return pin_dir;
}

static int preflight_net_cmd(const char *signal, hal_sig_t *sig, const char *pins[]) {
    int i, type=-1, writers=0, bidirs=0, pincnt=0;
    char *writer_name=0, *bidir_name=0;
    /* if signal already exists, use its info */
    if (sig) {
	type = sig->type;
	writers = sig->writers;
	bidirs = sig->bidirs;
    }

    if (writers || bidirs)
    {
        hal_pin_t *pin;
        int next;
        for(next = hal_data->pin_list_ptr; next; next=pin->next_ptr)
        {
            pin = (hal_pin_t *) SHMPTR(next);
            if(SHMPTR(pin->signal) == sig && pin->dir == HAL_OUT)
                writer_name = pin->name;
            if(SHMPTR(pin->signal) == sig && pin->dir == HAL_IO)
                bidir_name = writer_name = pin->name;
        }
    }

    for(i=0; pins[i] && *pins[i]; i++) {
        hal_pin_t *pin = 0;
        pin = halpr_find_pin_by_name(pins[i]);
        if(!pin) {
	    PyErr_Format(PyExc_RuntimeError, "net: pin '%s' does not exist", pins[i]);
	    throw boost::python::error_already_set();
        }
        if(SHMPTR(pin->signal) == sig) {
	     /* Already on this signal */
	    pincnt++;
	    continue;
	} else if(pin->signal != 0) {
            hal_sig_t *osig = (hal_sig_t *)SHMPTR(pin->signal);
	    PyErr_Format(PyExc_RuntimeError,
			 "net: pin '%s'  was already linked to signal '%s'",
                    pin->name, osig->name);
	    throw boost::python::error_already_set();
	}
	if (type == -1) {
	    /* no pre-existing type, use this pin's type */
	    type = pin->type;
	}
        if(type != pin->type) {
             PyErr_Format(PyExc_RuntimeError,
                "net: signal '%s' of type '%s' cannot add pin '%s' of type '%s'\n",
                signal, data_type2(type), pin->name, data_type2(pin->type));
	    throw boost::python::error_already_set();
        }
        if(pin->dir == HAL_OUT) {
            if(writers || bidirs) {
            dir_error:
		PyErr_Format(PyExc_RuntimeError,
                    "net: signal '%s' can not add %s pin '%s', "
                    "it already has %s pin '%s'\n",
                        signal, pin_data_dir(pin->dir), pin->name,
                        bidir_name ? pin_data_dir(HAL_IO):pin_data_dir(HAL_OUT),
                        bidir_name ? bidir_name : writer_name);
		throw boost::python::error_already_set();
            }
            writer_name = pin->name;
            writers++;
        }
	if(pin->dir == HAL_IO) {
            if(writers) {
                goto dir_error;
            }
            bidir_name = pin->name;
            bidirs++;
        }
        pincnt++;
    }
    if (pincnt)
        return 0;
    throw std::runtime_error("'net' requires at least one pin, none given");
}


#define MAX_PINS 100
static bp::object net(bp::tuple args, bp::dict kwargs)
{
    hal_sig_t *sig;
    int i, retval = 0;
    const char *signal;
    const char *pins[MAX_PINS];

    rtapi_mutex_get(&(hal_data->mutex));
    if (len(args) < 2) {
        rtapi_mutex_give(&(hal_data->mutex));
	throw std::runtime_error("net: at least 2 arguments required");
    }

    signal = bp::extract< char * >(args[0]);
    for (i = 1; i < len(args); i++) {
	if (i > MAX_PINS-1) {
	    rtapi_mutex_give(&(hal_data->mutex));
	    throw std::runtime_error("net: too many arguments");
	}
	pins[i-1] = bp::extract<char *>(args[i]);
    }
    pins[i-1] = NULL;

    /* see if signal already exists */
    sig = halpr_find_sig_by_name(signal);

    /* verify that everything matches up (pin types, etc) */
    preflight_net_cmd(signal, sig, pins);
    {
	hal_pin_t *pin = halpr_find_pin_by_name(signal);
	if (pin) {
	    rtapi_mutex_give(&(hal_data->mutex));
	    PyErr_Format(PyExc_RuntimeError,
                    "net: signal name '%s' must not be the same as a pin.  "
			 "Did you omit the signal name?\n",
			 signal);
	    throw boost::python::error_already_set();
	}
    }
    if (!sig && !strchr(signal, ':')) { // dont create signal if its remote (?)
        /* Create the signal with the type of the first pin */
        hal_pin_t *pin = halpr_find_pin_by_name(pins[0]);
        rtapi_mutex_give(&(hal_data->mutex));
        if (!pin) {
	    PyErr_Format(PyExc_RuntimeError,
			 "net: pin '%s' does not exist",pins[0]);
	    throw boost::python::error_already_set();
        }
        retval = hal_signal_new(signal, pin->type);
    } else {
	/* signal already exists */
	rtapi_mutex_give(&(hal_data->mutex));
    }
    /* add pins to signal */
    for(i=0; retval == 0 && pins[i] && *pins[i]; i++) {
        if ((retval = hal_link(pins[i], signal)) < 0) {
	    PyErr_Format(PyExc_RuntimeError,
			 "net: hal_link(%s,%s) failed: %s",signal, pins[i],
			 strerror(-retval));
	    throw boost::python::error_already_set();
	}
    }
    return bp::object(retval);
}
