/*	$NetBSD: reg.h,v 1.1 2002/06/05 01:04:23 fredette Exp $	*/

/*	$OpenBSD: reg.h,v 1.7 2000/06/15 17:00:37 mickey Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
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
/* 
 * Copyright (c) 1990,1994 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 * 	Utah $Hdr: regs.h 1.6 94/12/14$
 *	Author: Bob Wheeler, University of Utah CSL
 */

#ifndef _HPPA_REG_H_
#define _HPPA_REG_H_

/*
 * constants for registers for use with the following routines:
 * 
 *     void mtctl(reg, value)	- move to control register
 *     int mfctl(reg)		- move from control register
 *     int mtsp(sreg, value)	- move to space register
 *     int mfsr(sreg)		- move from space register
 */

#define	CR_RCTR		0
#define	CR_PIDR1	8
#define	CR_PIDR2	9
#define	CR_CCR		10
#define	CR_SAR		11
#define	CR_PIDR3	12
#define	CR_PIDR4	13
#define	CR_IVA		14
#define	CR_EIEM		15
#define	CR_ITMR		16
#define	CR_PCSQ		17
#define	CR_PCOQ		18
#define	CR_IIR		19
#define	CR_ISR		20
#define	CR_IOR		21
#define	CR_IPSW		22
#define	CR_EIRR		23
#define	CR_HPTMASK	24
#define	CR_VTOP		25
#define	CR_TR2		26
#define	CR_TR3		27
#define	CR_HVTP		28	/* points to a faulted HVT slot on LC cpus */
#define	CR_TR5		29
#define	CR_UPADDR	30	/* paddr of U-area of curproc */
#define	CR_TR7		31

/*
 * Diagnostic registers and bit positions
 */
#define	DR_CPUCFG		0

#define	DR0_PCXS_DHPMC		10	/* r/c D-cache error flag */
#define	DR0_PCXS_ILPMC		14	/* r/c I-cache error flag */
#define	DR0_PCXS_EQWSTO		16	/* r/w enable quad-word stores */
#define	DR0_PCXS_IHE		18	/* r/w I-cache sid hash enable */
#define	DR0_PCXS_DOMAIN		19
#define	DR0_PCXS_DHE		20	/* r/w D-cache sid hash enable */

#define	DR0_PCXT_DHPMC		10	/* r/c L1 D-cache error flag */
#define	DR0_PCXT_ILPMC		14	/* r/c L1 I-cache error flag */
#define	DR0_PCXT_IHE		18	/* r/w I-cache sid hash enable */
#define	DR0_PCXT_DHE		20	/* r/w D-cache sid hash enable */

#define	DR0_PCXL_L2IHPMC	6	/* r/c L2 I-cache error flag */
#define	DR0_PCXL_L2IHPMC_DIS	7	/* r/w L2 I-cache hpmc disable mask */
#define	DR0_PCXL_L2DHPMC	8	/* r/c L2 D-cache error flag */
#define	DR0_PCXL_L2DHPMC_DIS	9	/* r/w L2 D-cache hpmc disable mask */
#define	DR0_PCXL_L1IHPMC	10	/* r/c L1 I-cache error flag */
#define	DR0_PCXL_L1IHPMC_DIS	11	/* r/w L1 I-cache hpmc disable mask */
#define	DR0_PCXL_L2PARERR	15	/* r/c L2 Cache parity error (4 bit) */
#define	DR0_PCXL_STORE0		16	/* r/w scratch space */
#define	DR0_PCXL_PFMASK		17	/* r/w power-fail trap mask */
#define	DR0_PCXL_STORE1		18	/* r/w scratch */
#define	DR0_PCXL_FASTMODE	19	/* r   0-fast, 1-slow */
#define	DR0_PCXL_ISTRM_EN	20	/* r/w I-cache streaming enable */
#define	DR0_PCXL_DUAL_DIS	22	/* r/w disable dual-issue (2 bit) */
#define	DR0_PCXL_ENDIAN		23	/* r/w little endian traps */
#define	DR0_PCXL_SOU_EN		24	/* r/w stall-on-use on dc misses */
#define	DR0_PCXL_SHINT_EN	25	/* r/w no-fill on miss store hints */
#define	DR0_PCXL_IPREF_EN	26	/* r/w L2 to L1 I-cache prefetch */
#define	DR0_PCXL_L2DHASH_EN	27	/* r/w L2 D-cache hash enable */
#define	DR0_PCXL_L2IHASH_EN	28	/* r/w L2 I-cache hash enable */
#define	DR0_PCXL_L1ICACHE_EN	29	/* r/w L1 I-cache enable */
#define	DR0_PCXL_HIT		30	/* r   Diag cache read hit indication */
#define	DR0_PCXL_PARERR		31	/* r   Diag cache read parity error */

#define	DR0_PCXL2_L1DHPMC	8	/* r/c L1 D-cache error flag */
#define	DR0_PCXL2_L1DHPMC_DIS	9	/* r/w L1 D-cache hpmc disable */
#define	DR0_PCXL2_L2DHPMC	10	/* r/c L1 I-cache error flag */
#define	DR0_PCXL2_L2DHPMC_DIS	11	/* r/w L1 I-cache hpmc disable */
#define	DR0_PCXL2_STORE0	16	/* r/w scratch space */
#define	DR0_PCXL2_PFMASK	17	/* r/w power-fail trap mask */
#define	DR0_PCXL2_STORE1	18	/* r/w scratch */
#define	DR0_PCXL2_DCSAFE	19	/* r/w serialize all data cache hangs */
#define	DR0_PCXL2_ISTRM_EN	20	/* r/w I-cache streaming enable */
#define	DR0_PCXL2_DUAL_DIS	22	/* r/w disable dual-issue (2 bit) */
#define	DR0_PCXL2_ENDIAN	23	/* r/w little endian traps */
#define	DR0_PCXL2_SOU_EN	24	/* r/w stall-on-use on dc misses */
#define	DR0_PCXL2_SHINT_EN	25	/* r/w no-fill on miss store hints */
#define	DR0_PCXL2_IPREF_EN	26	/* r/w L2 to L1 I-cache prefetch */
#define	DR0_PCXL2_LMIN_EN	27	/* r/w minor ill insn traps on LIH */
#define	DR0_PCXL2_RMIN_EN	28	/* r/w major ill insn traps on RIH */
#define	DR0_PCXL2_L1CACHE_EN	29	/* r/w L1 I-cache enable */

#define	DR_DTLB			8

#define	DR_ITLB			9

#define	DR_ITLB_SIZE_1		24
#define	DR_ITLB_SIZE_0		25

#define	DR_DTLB_SIZE_1		26
#define	DR_DTLB_SIZE_0		27

#define CCR_MASK 0xff

#define	HPPA_NREGS	(32)
#define	HPPA_NFPREGS	(33)	/* 33rd is used for r0 in fpemul */

#ifndef _LOCORE

struct reg {
	u_int32_t r_regs[HPPA_NREGS];
	/* p'bably some cr* ? */
};

struct fpreg {
	u_int64_t fpr_regs[HPPA_NFPREGS];
};
#endif /* !_LOCORE */

#endif /* _HPPA_REG_H_ */
