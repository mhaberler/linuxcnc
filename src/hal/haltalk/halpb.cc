#include "halpb.h"
// transfrom a HAL component into a Component protobuf.
// does not aquire the HAL mutex.
int
halpr_describe_component(hal_comp_t *comp, pb::Component *c)
{
    c->set_name(comp->name);
    c->set_comp_id(comp->comp_id);
    c->set_type(comp->type);
    c->set_state(comp->state);
    c->set_last_update(comp->last_update);
    c->set_last_bound(comp->last_bound);
    c->set_last_unbound(comp->last_unbound);
    c->set_pid(comp->pid);
    if (comp->insmod_args)
	c->set_args((const char *)SHMPTR(comp->insmod_args));
    c->set_userarg1(comp->userarg1);
    c->set_userarg2(comp->userarg2);

    int next = hal_data->pin_list_ptr;
    while (next != 0) {
	hal_pin_t *pin = (hal_pin_t *)SHMPTR(next);
	hal_comp_t *owner = (hal_comp_t *) SHMPTR(pin->owner_ptr);
	if (owner->comp_id == comp->comp_id) {
	    pb::Pin *p = c->add_pin();
	    p->set_type((pb::ValueType) pin->type);
	    p->set_dir((pb::HalPinDirection) pin->dir);
	    p->set_handle(pin->handle);
	    p->set_name(pin->name);
	    p->set_linked(pin->signal != 0);
	    assert(hal_pin2pb(pin, p) == 0);
#ifdef USE_PIN_USER_ATTRIBUTES
	    p->set_flags(pin->flags);
	    if (pin->type == HAL_FLOAT)
		p->set_epsilon(pin->epsilon);
#endif
	}
	next = pin->next_ptr;
    }
    next = hal_data->param_list_ptr;
    while (next != 0) {
	hal_param_t *param = (hal_param_t *)SHMPTR(next);
	hal_comp_t *owner = (hal_comp_t *) SHMPTR(param->owner_ptr);
	if (owner->comp_id == comp->comp_id) {
	    pb::Param *p = c->add_param();
	    p->set_name(param->name);
	    p->set_type((pb::ValueType) param->type);
	    p->set_dir((pb::HalParamDirection) param->dir);
	    p->set_handle(param->handle);
	    assert(hal_param2pb(param, p) == 0);
	}
	next = param->next_ptr;
    }
    return 0;
}

int
halpr_describe_signal(hal_sig_t *sig, pb::Signal *pbsig)
{
    pbsig->set_name(sig->name);
    pbsig->set_type((pb::ValueType)sig->type);
    pbsig->set_readers(sig->readers);
    pbsig->set_writers(sig->writers);
    pbsig->set_bidirs(sig->bidirs);
    pbsig->set_handle(sig->handle);
    return hal_sig2pb(sig, pbsig);
}

int
halpr_describe_ring(hal_ring_t *ring, pb::Ring *pbring)
{
    pbring->set_name(ring->name);
    pbring->set_handle(ring->handle);
    pbring->set_owner(ring->owner);
    // XXX describing more detail would require a temporary attach.
    return 0;
}

int halpr_describe_funct(hal_funct_t *funct, pb::Function *pbfunct)
{
    hal_comp_t *owner = (hal_comp_t *) SHMPTR(funct->owner_ptr);

    pbfunct->set_name(funct->name);
    pbfunct->set_handle(funct->handle);
    pbfunct->set_owner_id(owner->comp_id);
    pbfunct->set_users(funct->users);
    pbfunct->set_runtime(funct->runtime);
    pbfunct->set_maxtime(funct->maxtime);
    pbfunct->set_reentrant(funct->reentrant);
    return 0;
}

int halpr_describe_thread(hal_thread_t *thread, pb::Thread *pbthread)
{
    pbthread->set_name(thread->name);
    pbthread->set_handle(thread->handle);
    pbthread->set_uses_fp(thread->uses_fp);
    pbthread->set_period(thread->period);
    pbthread->set_priority(thread->priority);
    pbthread->set_task_id(thread->task_id);
    pbthread->set_cpu_id(thread->cpu_id);
    pbthread->set_task_id(thread->task_id);

    hal_list_t *list_root = &(thread->funct_list);
    hal_list_t *list_entry = list_next(list_root);

    while (list_entry != list_root) {
	hal_funct_entry_t *fentry = (hal_funct_entry_t *) list_entry;
	hal_funct_t *funct = (hal_funct_t *) SHMPTR(fentry->funct_ptr);
	pbthread->add_function(funct->name);
	list_entry = list_next(list_entry);
    }
    return 0;
}
