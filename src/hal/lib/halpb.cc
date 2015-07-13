#include "halpb.hh"

int halpr_describe_pin(hal_pin_t *pin, pb::Pin *pbpin)
{
    pbpin->set_type((pb::ValueType) pin->type);
    pbpin->set_dir((pb::HalPinDirection) pin->dir);
    pbpin->set_handle(ho_id(pin));
    pbpin->set_name(ho_name(pin));
    pbpin->set_linked(pin->signal != 0);
    assert(hal_pin2pb(pin, pbpin) == 0);
    pbpin->set_flags(pin->flags);
    if (pin->type == HAL_FLOAT)
	pbpin->set_epsilon(hal_data->epsilon[pin->eps_index]);
    return 0;
}

int halpr_describe_param(hal_param_t *param, pb::Param *pbparam)
{
    pbparam->set_name(ho_name(param));
    pbparam->set_handle(ho_id(param));
    pbparam->set_type((pb::ValueType) param->type);
    pbparam->set_dir((pb::HalParamDirection) param->dir);
    assert(hal_param2pb(param, pbparam) == 0);
    return 0;
}

int pbadd_owned(hal_object_ptr o, foreach_args_t *args)
{
    int type = hh_get_type(o.hdr);
    pb::Component *pbcomp = (pb::Component *)args->user_ptr1;
    switch (type) {
    case HAL_PARAM:
	{
	    pb::Param *pbparam = pbcomp->add_param();
	    halpr_describe_param(o.param, pbparam);
	}
	break;
    case HAL_PIN:
	{
	    pb::Pin *pbpin = pbcomp->add_pin();
	    halpr_describe_pin(o.pin, pbpin);
	}
	break;
    default: ;
    }
    return 0;
}

// transfrom a HAL component into a Component protobuf.
// does not aquire the HAL mutex.
int
halpr_describe_component(hal_comp_t *comp, pb::Component *pbcomp)
{
    pbcomp->set_name(ho_name(comp));
    pbcomp->set_comp_id(ho_id(comp));
    pbcomp->set_type(comp->type);
    pbcomp->set_state(comp->state);
    pbcomp->set_last_update(comp->last_update);
    pbcomp->set_last_bound(comp->last_bound);
    pbcomp->set_last_unbound(comp->last_unbound);
    pbcomp->set_pid(comp->pid);
    if (comp->insmod_args)
	pbcomp->set_args((const char *)SHMPTR(comp->insmod_args));
    pbcomp->set_userarg1(comp->userarg1);
    pbcomp->set_userarg2(comp->userarg2);

    foreach_args_t args;
    args.owning_comp = ho_id(comp);
    args.user_ptr1 = (void *)pbcomp;
    halg_foreach(0, &args, pbadd_owned);
    return 0;
}

int
halpr_describe_signal(hal_sig_t *sig, pb::Signal *pbsig)
{
    pbsig->set_name(ho_name(sig));
    pbsig->set_type((pb::ValueType)sig->type);
    pbsig->set_readers(sig->readers);
    pbsig->set_writers(sig->writers);
    pbsig->set_bidirs(sig->bidirs);
    pbsig->set_handle(ho_id(sig));
    return hal_sig2pb(sig, pbsig);
}

int
halpr_describe_ring(hal_ring_t *ring, pb::Ring *pbring)
{
    pbring->set_name(ring->name);
    pbring->set_handle(ring->handle);
    //FIXME use new attach function to query flags
    // XXX describing more detail would require a temporary attach.
    return 0;
}

int halpr_describe_funct(hal_funct_t *funct, pb::Function *pbfunct)
{
    int id;
    hal_comp_t *owner = halpr_find_owning_comp(ho_owner_id(funct));
    if (owner == NULL)
	id = -1;
    else
	id = ho_id(owner);
    pbfunct->set_owner_id(id);
    pbfunct->set_name(ho_name(funct));
    pbfunct->set_handle(ho_id(funct));
    pbfunct->set_users(funct->users);
    pbfunct->set_runtime(*(funct->runtime));
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
    hal_list_t *list_entry = (hal_list_t *) dlist_next(list_root);

    while (list_entry != list_root) {
	hal_funct_entry_t *fentry = (hal_funct_entry_t *) list_entry;
	hal_funct_t *funct = (hal_funct_t *) SHMPTR(fentry->funct_ptr);
	pbthread->add_function(hh_get_name(&funct->hdr));
	list_entry = (hal_list_t *)dlist_next(list_entry);
    }
    return 0;
}


int halpr_describe_member(hal_member_t *member, pb::Member *pbmember)
{
    if (member->sig_member_ptr) {
	hal_sig_t *sig = (hal_sig_t *)SHMPTR(member->sig_member_ptr);
	pbmember->set_mtype(pb::HAL_MEMBER_SIGNAL);
	pbmember->set_userarg1(member->userarg1);
	if (sig->type == HAL_FLOAT)
	    pbmember->set_epsilon(hal_data->epsilon[member->eps_index]);
	pb::Signal *pbsig = pbmember->mutable_signal();
	halpr_describe_signal(sig, pbsig);
    } else {
	hal_group_t *group = (hal_group_t *)SHMPTR(member->group_member_ptr);
	pbmember->set_mtype(pb::HAL_MEMBER_GROUP);
	pbmember->set_groupname(group->name);
	pbmember->set_handle(group->handle);
    }
    return 0;
}

static int describe_member(int level, hal_group_t **groups,
			   hal_member_t *member, void *arg)
{
    pb::Group *pbgroup =  (pb::Group *) arg;
    pb::Member *pbmember = pbgroup->add_member();
    halpr_describe_member(member, pbmember);
    return 0;
}

int halpr_describe_group(hal_group_t *g, pb::Group *pbgroup)
{
    pbgroup->set_name(g->name);
    pbgroup->set_refcount(g->refcount);
    pbgroup->set_userarg1(g->userarg1);
    pbgroup->set_userarg2(g->userarg2);
    pbgroup->set_handle(g->handle);

    halpr_foreach_member(g->name, describe_member, pbgroup, RESOLVE_NESTED_GROUPS);
    return 0;
}
