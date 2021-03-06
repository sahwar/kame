/*-
 * Copyright (c) 1998 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: ipl_funcs.c,v 1.9.2.1 1999/05/07 09:01:37 dfr Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/interrupt.h>
#include <machine/ipl.h>
#include <machine/cpu.h>
#include <net/netisr.h>

#include "sio.h"

unsigned int bio_imask;		/* XXX */
unsigned int cam_imask;		/* XXX */
unsigned int net_imask;		/* XXX */
unsigned int tty_imask;		/* XXX */

static void swi_net(void);

void	(*netisrs[32]) __P((void));
swihand_t *ihandlers[32] = {	/* software interrupts */
	swi_null,	swi_net,	swi_null,	swi_null,
	swi_null,	softclock,	swi_null,	swi_null,
	swi_null,	swi_null,	swi_null,	swi_null,
	swi_null,	swi_null,	swi_null,	swi_null,
	swi_null,	swi_null,	swi_null,	swi_null,
	swi_null,	swi_null,	swi_null,	swi_null,
	swi_null,	swi_null,	swi_null,	swi_null,
	swi_null,	swi_null,	swi_null,	swi_null,
};

u_int32_t netisr;
u_int32_t ipending;
u_int32_t idelayed;

#define getcpl()	(alpha_pal_rdps() & ALPHA_PSL_IPL_MASK)


static void atomic_setbit(u_int32_t* p, u_int32_t bit)
{
    u_int32_t temp;
    __asm__ __volatile__ (
	"1:\tldl_l %0,%2\n\t"	/* load current mask value, asserting lock */
	"or %3,%0,%0\n\t"	/* add our bits */
	"stl_c %0,%1\n\t"	/* attempt to store */
	"beq %0,2f\n\t"		/* if the store failed, spin */
	"br 3f\n"		/* it worked, exit */
	"2:\tbr 1b\n"		/* *p not updated, loop */
	"3:\tmb\n"		/* it worked */
	: "=&r"(temp), "=m" (*p)
	: "m"(*p), "r"(bit)
	: "memory");
}

static u_int32_t atomic_readandclear(u_int32_t* p)
{
    u_int32_t v, temp;
    __asm__ __volatile__ (
	"wmb\n"			/* ensure pending writes have drained */
	"1:\tldl_l %0,%3\n\t"	/* load current value, asserting lock */
	"ldiq %1,0\n\t"		/* value to store */
	"stl_c %1,%2\n\t"	/* attempt to store */
	"beq %1,2f\n\t"		/* if the store failed, spin */
	"br 3f\n"		/* it worked, exit */
	"2:\tbr 1b\n"		/* *p not updated, loop */
	"3:\tmb\n"		/* it worked */
	: "=&r"(v), "=&r"(temp), "=m" (*p)
	: "m"(*p)
	: "memory");
    return v;
}

void
swi_null()
{
    /* No interrupt registered, do nothing */
}

void
swi_generic()
{
    /* Just a placeholder, we call swi_dispatcher directly */
    panic("swi_generic() called");
}

static void
swi_net()
{
    u_int32_t bits = atomic_readandclear(&netisr);
    int i;

    for (i = 0; i < 32; i++) {
	if (bits & 1)
	    netisrs[i]();
	bits >>= 1;
    }
}

void
do_sir()
{
    u_int32_t pend;
    int i;

    splsoft();
    while ((pend = atomic_readandclear(&ipending)) != 0) {
	for (i = 0; pend && i < 32; i++) {
	    if (pend & (1 << i)) {
		if (ihandlers[i] == swi_generic)
		    swi_dispatcher(i);
		else
		    ihandlers[i]();
		pend &= ~(1 << i);
	    }
	}
    }
}

#define GENSET(name, ptr, bit)			\
						\
void name(void)					\
{						\
    atomic_setbit(ptr, bit);			\
}

GENSET(setdelayed,	&ipending,	atomic_readandclear(&idelayed))
GENSET(setsofttty,	&ipending,	1 << SWI_TTY)
GENSET(setsoftnet,	&ipending,	1 << SWI_NET)
GENSET(setsoftcamnet,	&ipending,	1 << SWI_CAMNET)
GENSET(setsoftcambio,	&ipending,	1 << SWI_CAMBIO)
GENSET(setsoftvm,	&ipending,	1 << SWI_VM)
GENSET(setsoftclock,	&ipending,	1 << SWI_CLOCK)

GENSET(schedsofttty,	&idelayed,	1 << SWI_TTY)
GENSET(schedsoftnet,	&idelayed,	1 << SWI_NET)
GENSET(schedsoftcamnet,	&idelayed,	1 << SWI_CAMNET)
GENSET(schedsoftcambio,	&idelayed,	1 << SWI_CAMBIO)
GENSET(schedsoftvm,	&idelayed,	1 << SWI_VM)
GENSET(schedsoftclock,	&idelayed,	1 << SWI_CLOCK)

#define SPLDOWN(name, pri)			\
						\
int name(void)					\
{						\
    int s;					\
    s = alpha_pal_swpipl(ALPHA_PSL_IPL_##pri);	\
    return s;					\
}

SPLDOWN(splsoftclock, SOFT)
SPLDOWN(splsoft, SOFT)

#define SPLUP(name, pri)				\
							\
int name(void)						\
{							\
    int cpl = getcpl();					\
    if (ALPHA_PSL_IPL_##pri > cpl) {			\
	int s = alpha_pal_swpipl(ALPHA_PSL_IPL_##pri);	\
	return s;					\
    } else						\
	return cpl;					\
}

SPLUP(splsoftcam, SOFT)
SPLUP(splsoftnet, SOFT)
SPLUP(splsoftvm, SOFT)
SPLUP(splnet, IO)
SPLUP(splbio, IO)
SPLUP(splcam, IO)
SPLUP(splimp, IO)
SPLUP(spltty, IO)
SPLUP(splvm, IO)
SPLUP(splclock, CLOCK)
SPLUP(splstatclock, CLOCK)
SPLUP(splhigh, HIGH)

void
spl0()
{
    if (ipending)
	do_sir();		/* lowers ipl to SOFT */

    alpha_pal_swpipl(ALPHA_PSL_IPL_0);
}

void
splx(int s)
{
    if (s)
	alpha_pal_swpipl(s);
    else
	spl0();
}
