static inline hal_data_u *hal_sig2u(const hal_sig_t *sig)
{
    return (hal_data_u *)SHMPTR(sig->data_ptr);
}

static inline hal_data_u *hal_pin2u(const hal_pin_t *pin)
{
    const hal_sig_t *sig;
    if (pin->signal != 0) {
	sig = (const hal_sig_t *) SHMPTR(pin->signal);
	return (hal_data_u *)SHMPTR(sig->data_ptr);
    } else
	return (hal_data_u *)(hal_shmem_base + SHMOFF(&(pin->dummysig)));
}

static inline int hal_pin2pb(const hal_pin_t *hp, pb::Pin *p)
{
    hal_data_u *vp = hal_pin2u(hp);
    switch (hp->type) {
    default:
	return -1;
    case HAL_BIT:
	p->set_halbit(vp->b);
	break;
    case HAL_FLOAT:
	p->set_halfloat(vp->f);
	break;
    case HAL_S32:
	p->set_hals32(vp->s);
	break;
    case HAL_U32:
	p->set_halu32(vp->u);
	break;
    }
    return 0;
}

static inline int hal_sig2pb(const hal_sig_t *sp, pb::Signal *s)
{
    hal_data_u *vp = hal_sig2u(sp);
    switch (sp->type) {
    default:
	return -1;
    case HAL_BIT:
	s->set_halbit(vp->b);
	break;
    case HAL_FLOAT:
	s->set_halfloat(vp->f);
	break;
    case HAL_S32:
	s->set_hals32(vp->s);
	break;
    case HAL_U32:
	s->set_halu32(vp->u);
	break;
    }
    return 0;
}
