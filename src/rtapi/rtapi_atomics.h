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


#ifndef _RTAPI_ATOMICS_H
#define _RTAPI_ATOMICS_H

#include "config.h" // HAVE_CK
#include "rtapi_int.h"

// memory barrier primitives
// see https://www.kernel.org/doc/Documentation/memory-barriers.txt

#ifdef HAVE_CK
// use concurrencykit.org primitives
#include <ck_pr.h>


static inline uint32_t rtapi_load_32(const uint32_t *target)
{
    return ck_pr_load_32(target);
}

static inline void rtapi_store_32(uint32_t *target, uint32_t value)
{
     ck_pr_store_32(target, value);
}


// use gcc intrinsics until the x86 flavor of ck supports 64bit ops
#if !defined(CK_F_PR_LOAD_64)
static inline uint64_t rtapi_load_64(const uint64_t *target)
{
    uint64_t v;
    __atomic_load(target, &v, RTAPI_MEMORY_MODEL);
    return v;
}
#else
static inline uint64_t rtapi_load_64(const uint64_t *target)
{
    return ck_pr_load_64(target);
}
#endif

#if !defined(CK_F_PR_INC_64)
static inline void rtapi_inc_64(uint64_t *target)
{
    __atomic_add_fetch(target, 1, RTAPI_MEMORY_MODEL);
}
#else
static inline void rtapi_inc_64(uint64_t *target)
{
    ck_pr_inc_64(target);
}
#endif

#define	rtapi_smp_rmb() ck_pr_fence_load()
#define	rtapi_smp_wmb() ck_pr_fence_store()
#define	rtapi_smp_mb()  ck_pr_fence_memory()

#else

// use gcc intrinsics
static inline uint32_t rtapi_load_32(const uint32_t *target)
{
    uint32_t v;
    __atomic_load(target, &v, RTAPI_MEMORY_MODEL);
    return v;
}

static inline void rtapi_store_32(uint32_t *target, uint32_t value)
{
    __atomic_store(target, &value, RTAPI_MEMORY_MODEL);
}

static inline uint64_t rtapi_load_64(const uint64_t *target)
{
    uint64_t v;
    __atomic_load(target, &v, RTAPI_MEMORY_MODEL);
    return v;
}

static inline void rtapi_inc_64(uint64_t *target)
{
    __atomic_add_fetch(target, 1, RTAPI_MEMORY_MODEL);
}

#define	rtapi_smp_mb()  __sync_synchronize()
#define	rtapi_smp_wmb() __sync_synchronize()
#define	rtapi_smp_rmb() __sync_synchronize()
#endif

#endif // _RTAPI_ATOMICS_H
