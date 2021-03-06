/*	$NetBSD: ioc_fiq_util.S,v 1.1 2002/03/24 15:47:18 bjh21 Exp $	*/

/*-
 * Copyright (c) 2001 Ben Harris
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>

RCSID("$NetBSD: ioc_fiq_util.S,v 1.1 2002/03/24 15:47:18 bjh21 Exp $")

#include <machine/memcreg.h>
#include <arch/acorn26/iobus/iocreg.h>

/*
 * fiq_downgrade should be called in FIQ mode.  It uses R12_fiq and R13_fiq
 * as scratch registers.  When finished, it returns from the FIQ.
 */
ENTRY(fiq_downgrade)
	ldr	r12, Lioc_base
	ldrb	r13, [r12, #(IOC_IRQMSKA << 2)]
	orr	r13, r13, #(1 <<IOC_IRQ_1)
	strb	r13, [r12, #(IOC_IRQMSKA << 2)]
	mov	r13, #0
	strb	r13, [r12, #(IOC_FIQMSK << 2)]
	ldr	r12, Lfiq_want_downgrade
	mov	r13, #1
	str	r13, [r12]	
	subs	pc, r14, #4

ENTRY(fiq_downgrade_dont_disable)
	ldr	r12, Lioc_base
	ldrb	r13, [r12, #(IOC_IRQMSKA << 2)]
	orr	r13, r13, #(1 <<IOC_IRQ_1)
	strb	r13, [r12, #(IOC_IRQMSKA << 2)]
	ldr	r12, Lfiq_want_downgrade
	mov	r13, #1
	str	r13, [r12]	
	subs	pc, r14, #4


Lioc_base:
	.word	0x03200000
Lfiq_want_downgrade:
	.word	_C_LABEL(fiq_want_downgrade)