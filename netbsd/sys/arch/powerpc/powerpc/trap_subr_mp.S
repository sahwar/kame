/*	$NetBSD: trap_subr_mp.S,v 1.4 2001/06/23 02:36:15 matt Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * NOTICE: This is not a standalone file.  to use it, #include it in
 * your port's locore.S, like so:
 *
 *	#include <powerpc/powerpc/trap_subr.S>
 */

/*
 * This code gets copied to all the trap vectors
 * (except ISI/DSI, ALI, the interrupts, and possibly the debugging
 * traps when using IPKDB).
 */
	.text
	.globl	_C_LABEL(trapcode),_C_LABEL(trapsize)
_C_LABEL(trapcode):
	mtsprg	1,1			/* save SP */
	GET_CPUINFO(1)
	stmw	28,CI_TEMPSAVE(1)	/* free r28-r31 */
	mfsprg	1,1			/* restore SP */
	mflr	28			/* save LR */
	mfcr	29			/* save CR */
/* Test whether we already had PR set */
	mfsrr1	31
	mtcr	31
	bc	4,17,1f			/* branch if PSL_PR is clear */
	GET_CPUINFO(1)
	lwz	1,CI_CURPCB(1)
	addi	1,1,USPACE		/* stack is top of user struct */
1:
	bla	s_trap
_C_LABEL(trapsize) = .-_C_LABEL(trapcode)

/*
 * For ALI: has to save DSISR and DAR
 */
	.globl	_C_LABEL(alitrap),_C_LABEL(alisize)
_C_LABEL(alitrap):
	mtsprg	1,1			/* save SP */
	GET_CPUINFO(1)
	stmw	28,CI_TEMPSAVE(1)	/* free r28-r31 */
	mfdar	30
	mfdsisr	31
	stmw	30,CI_TEMPSAVE+16(1)
	mfsprg	1,1			/* restore SP */
	mflr	28			/* save LR */
	mfcr	29			/* save CR */
/* Test whether we already had PR set */
	mfsrr1	31
	mtcr	31
	bc	4,17,1f			/* branch if PSL_PR is clear */
	GET_CPUINFO(1)
	lwz	1,CI_CURPCB(1)
	addi	1,1,USPACE		/* stack is top of user struct */
1:
	bla	s_trap
_C_LABEL(alisize) = .-_C_LABEL(alitrap)

/*
 * Similar to the above for DSI
 * Has to handle BAT spills
 * and standard pagetable spills
 */
	.globl	_C_LABEL(dsitrap),_C_LABEL(dsisize)
_C_LABEL(dsitrap):
	mtsprg	1,1
	GET_CPUINFO(1)
	stmw	28,CI_DISISAVE(1)	/* free r28-r31 */
	mfsprg	1,1
	mfcr	29			/* save CR */
	mfxer	30			/* save XER */
	mtsprg	2,30			/* in SPRG2 */
	mfsrr1	31			/* test kernel mode */
	mtcr	31
	bc	12,17,1f		/* branch if PSL_PR is set */
	mfdar	31			/* get fault address */
	rlwinm	31,31,7,25,28		/* get segment * 8 */

	/* get batu */
	addis	31,31,_C_LABEL(battable)@ha
	lwz	30,_C_LABEL(battable)@l(31)
	mtcr	30
	bc	4,30,1f			/* branch if supervisor valid is
					   false */
	/* get batl */
	lwz	31,_C_LABEL(battable)+4@l(31)
/* We randomly use the highest two bat registers here */
	mftb	28
	andi.	28,28,1
	bne	2f
	mtdbatu	2,30
	mtdbatl	2,31
	b	3f
2:
	mtdbatu	3,30
	mtdbatl	3,31
3:
	mfsprg	30,2			/* restore XER */
	mtxer	30
	mtcr	29			/* restore CR */
	mtsprg	1,1
	GET_CPUINFO(1)
	lmw	28,CI_DISISAVE(1)	/* restore r28-r31 */
	mfsprg	1,1
	rfi				/* return to trapped code */
1:
	mflr	28			/* save LR */
	bla	s_dsitrap
_C_LABEL(dsisize) = .-_C_LABEL(dsitrap)

/*
 * Similar to the above for ISI
 */
	.globl	_C_LABEL(isitrap),_C_LABEL(isisize)
_C_LABEL(isitrap):
	mtsprg	1,1
	GET_CPUINFO(1)
	stmw	28,CI_DISISAVE(1)	/* free r28-r31 */
	mfsprg	1,1
	mflr	28			/* save LR */
	mfcr	29			/* save CR */
	mfsrr1	31			/* test kernel mode */
	mtcr	31
	bc	12,17,1f		/* branch if PSL_PR is set */
	mfsrr0	31			/* get fault address */
	rlwinm	31,31,7,25,28		/* get segment * 8 */

	/* get batu */
	addis	31,31,_C_LABEL(battable)@ha
	lwz	30,_C_LABEL(battable)@l(31)
	mtcr	30
	bc	4,30,1f			/* branch if supervisor valid is
					   false */
	mtibatu	3,30

	/* get batl */
	lwz	30,_C_LABEL(battable)+4@l(31)
	mtibatl	3,30

	mtcr	29			/* restore CR */
	mtsprg	1,1
	GET_CPUINFO(1)
	lmw	28,CI_DISISAVE(1)	/* restore r28-r31 */
	mfsprg	1,1
	rfi				/* return to trapped code */
1:
	bla	s_isitrap
_C_LABEL(isisize)= .-_C_LABEL(isitrap)

/*
 * This one for the external interrupt handler.
 */
	.globl	_C_LABEL(extint),_C_LABEL(extsize)
_C_LABEL(extint):
	mtsprg	1,1			/* save SP */
	GET_CPUINFO(1)
	stmw	28,CI_TEMPSAVE(1)	/* free r28-r31 */
	mflr	28			/* save LR */
	mfcr	29			/* save CR */
	mfxer	30			/* save XER */
	lwz	31,CI_INTRDEPTH(1)	/* were we already running on intstk? */
	addic.	31,31,1
	stw	31,CI_INTRDEPTH(1)
	lwz	1,CI_INTSTK(1)		/* get interrupt stack */
	beq	1f
	mfsprg	1,1			/* yes, get old SP */
1:
	ba	extintr
_C_LABEL(extsize) = .-_C_LABEL(extint)

/*
 * And this one for the decrementer interrupt handler.
 */
	.globl	_C_LABEL(decrint),_C_LABEL(decrsize)
_C_LABEL(decrint):
	mtsprg	1,1			/* save SP */
	GET_CPUINFO(1)
	stmw	28,CI_TEMPSAVE(1)	/* free r28-r31 */
	mflr	28			/* save LR */
	mfcr	29			/* save CR */
	mfxer	30			/* save XER */
	lwz	31,CI_INTRDEPTH(1)	/* were we already running on intstk? */
	addic.	31,31,1
	stw	31,CI_INTRDEPTH(1)
	lwz	1,CI_INTSTK(1)		/* get interrupt stack */
	beq	1f
	mfsprg	1,1			/* yes, get old SP */
1:
	ba	decrintr
_C_LABEL(decrsize) = .-_C_LABEL(decrint)

/*
 * Now the tlb software load for 603 processors:
 * (Code essentially from the 603e User Manual, Chapter 5, but
 * corrected a lot.)
 */
	.globl	_C_LABEL(tlbimiss),_C_LABEL(tlbimsize)
_C_LABEL(tlbimiss):
	mfspr	2,SPR_HASH1		/* get first pointer */
	li	1,8
	mfctr	0			/* save counter */
	mfspr	3,SPR_ICMP		/* get first compare value */
	addi	2,2,-8			/* predec pointer */
1:
	mtctr	1			/* load counter */
2:
	lwzu	1,8(2)			/* get next pte */
	cmpl	0,1,3			/* see if found pte */
	bdneq	2b			/* loop if not eq */
	bne	3f			/* not found */
	lwz	1,4(2)			/* load tlb entry lower word */
	andi.	3,1,8			/* check G-bit */
	bne	4f			/* if guarded, take ISI */
	mtctr	0			/* restore counter */
	mfspr	0,SPR_IMISS		/* get the miss address for the tlbli */
	mfsrr1	3			/* get the saved cr0 bits */
	mtcrf	0x80,3			/* and restore */
	ori	1,1,0x100		/* set the reference bit */
	mtspr	SPR_RPA,1		/* set the pte */
	srwi	1,1,8			/* get byte 7 of pte */
	tlbli	0			/* load the itlb */
	stb	1,6(2)			/* update page table */
	rfi

3:	/* not found in pteg */
	andi.	1,3,0x40		/* have we already done second hash? */
	bne	5f
	mfspr	2,SPR_HASH2		/* get the second pointer */
	ori	3,3,0x40		/* change the compare value */
	li	1,8
	addi	2,2,-8			/* predec pointer */
	b	1b
4:	/* guarded */
	mfsrr1	3
	andi.	2,3,0xffff		/* clean upper srr1 */
	oris	2,2,0x8000000@h		/* set srr<4> to flag prot violation */
	b	6f
5:	/* not found anywhere */
	mfsrr1	3
	andi.	2,3,0xffff		/* clean upper srr1 */
	oris	2,2,0x40000000@h	/* set srr1<1> to flag pte not found */
6:
	mtctr	0			/* restore counter */
	mtsrr1	2
	mfmsr	0
	xoris	0,0,0x20000@h		/* flip the msr<tgpr> bit */
	mtcrf	0x80,3			/* restore cr0 */
	mtmsr	0			/* now with native gprs */
	isync
	ba	EXC_ISI
_C_LABEL(tlbimsize) = .-_C_LABEL(tlbimiss)

	.globl	_C_LABEL(tlbdlmiss),_C_LABEL(tlbdlmsize)
_C_LABEL(tlbdlmiss):
	mfspr	2,SPR_HASH1		/* get first pointer */
	li	1,8
	mfctr	0			/* save counter */
	mfspr	3,SPR_DCMP		/* get first compare value */
	addi	2,2,-8			/* predec pointer */
1:
	mtctr	1			/* load counter */
2:
	lwzu	1,8(2)			/* get next pte */
	cmpl	0,1,3			/* see if found pte */
	bdneq	2b			/* loop if not eq */
	bne	3f			/* not found */
	lwz	1,4(2)			/* load tlb entry lower word */
	mtctr	0			/* restore counter */
	mfspr	0,SPR_DMISS		/* get the miss address for the tlbld */
	mfsrr1	3			/* get the saved cr0 bits */
	mtcrf	0x80,3			/* and restore */
	ori	1,1,0x100		/* set the reference bit */
	mtspr	SPR_RPA,1		/* set the pte */
	srwi	1,1,8			/* get byte 7 of pte */
	tlbld	0			/* load the dtlb */
	stb	1,6(2)			/* update page table */
	rfi

3:	/* not found in pteg */
	andi.	1,3,0x40		/* have we already done second hash? */
	bne	5f
	mfspr	2,SPR_HASH2		/* get the second pointer */
	ori	3,3,0x40		/* change the compare value */
	li	1,8
	addi	2,2,-8			/* predec pointer */
	b	1b
5:	/* not found anywhere */
	mfsrr1	3
	lis	1,0x40000000@h		/* set dsisr<1> to flag pte not found */
	mtctr	0			/* restore counter */
	andi.	2,3,0xffff		/* clean upper srr1 */
	mtsrr1	2
	mtdsisr	1			/* load the dsisr */
	mfspr	1,SPR_DMISS		/* get the miss address */
	mtdar	1			/* put in dar */
	mfmsr	0
	xoris	0,0,0x20000@h		/* flip the msr<tgpr> bit */
	mtcrf	0x80,3			/* restore cr0 */
	mtmsr	0			/* now with native gprs */
	isync
	ba	EXC_DSI
_C_LABEL(tlbdlmsize) = .-_C_LABEL(tlbdlmiss)

	.globl	_C_LABEL(tlbdsmiss),_C_LABEL(tlbdsmsize)
_C_LABEL(tlbdsmiss):
	mfspr	2,SPR_HASH1		/* get first pointer */
	li	1,8
	mfctr	0			/* save counter */
	mfspr	3,SPR_DCMP		/* get first compare value */
	addi	2,2,-8			/* predec pointer */
1:
	mtctr	1			/* load counter */
2:
	lwzu	1,8(2)			/* get next pte */
	cmpl	0,1,3			/* see if found pte */
	bdneq	2b			/* loop if not eq */
	bne	3f			/* not found */
	lwz	1,4(2)			/* load tlb entry lower word */
	andi.	3,1,0x80		/* check the C-bit */
	beq	4f
5:
	mtctr	0			/* restore counter */
	mfspr	0,SPR_DMISS		/* get the miss address for the tlbld */
	mfsrr1	3			/* get the saved cr0 bits */
	mtcrf	0x80,3			/* and restore */
	mtspr	SPR_RPA,1		/* set the pte */
	tlbld	0			/* load the dtlb */
	rfi

3:	/* not found in pteg */
	andi.	1,3,0x40		/* have we already done second hash? */
	bne	5f
	mfspr	2,SPR_HASH2		/* get the second pointer */
	ori	3,3,0x40		/* change the compare value */
	li	1,8
	addi	2,2,-8			/* predec pointer */
	b	1b
4:	/* found, but C-bit = 0 */
	rlwinm.	3,1,30,0,1		/* test PP */
	bge-	7f
	andi.	3,1,1
	beq+	8f
9:	/* found, but protection violation (PP==00)*/
	mfsrr1	3
	lis	1,0xa000000@h		/* indicate protection violation
					   on store */
	b	1f
7:	/* found, PP=1x */
	mfspr	3,SPR_DMISS		/* get the miss address */
	mfsrin	1,3			/* get the segment register */
	mfsrr1	3
	rlwinm	3,3,18,31,31		/* get PR-bit */
	rlwnm.	2,2,3,1,1		/* get the key */
	bne-	9b			/* protection violation */
8:	/* found, set reference/change bits */
	lwz	1,4(2)			/* reload tlb entry */
	ori	1,1,0x180
	sth	1,6(2)
	b	5b
5:	/* not found anywhere */
	mfsrr1	3
	lis	1,0x42000000@h		/* set dsisr<1> to flag pte not found */
					/* dsisr<6> to flag store */
1:
	mtctr	0			/* restore counter */
	andi.	2,3,0xffff		/* clean upper srr1 */
	mtsrr1	2
	mtdsisr	1			/* load the dsisr */
	mfspr	1,SPR_DMISS		/* get the miss address */
	mtdar	1			/* put in dar */
	mfmsr	0
	xoris	0,0,0x20000@h		/* flip the msr<tgpr> bit */
	mtcrf	0x80,3			/* restore cr0 */
	mtmsr	0			/* now with native gprs */
	isync
	ba	EXC_DSI
_C_LABEL(tlbdsmsize) = .-_C_LABEL(tlbdsmiss)

#ifdef DDB
#define	ddbsave	0xde0		/* primary save area for DDB */
/*
 * In case of DDB we want a separate trap catcher for it
 */
	.local	ddbstk
	.comm	ddbstk,INTSTK,8		/* ddb stack */

	.globl	_C_LABEL(ddblow),_C_LABEL(ddbsize)
_C_LABEL(ddblow):
	mtsprg	1,1			/* save SP */
	GET_CPUINFO(1)
	stmw	28,CI_DDBSAVE(1)	/* free r28-r31 */
	mflr	28			/* save LR */
	mfcr	29			/* save CR */
	lis	1,ddbstk+INTSTK@ha	/* get new SP */
	addi	1,1,ddbstk+INTSTK@l
	bla	ddbtrap
_C_LABEL(ddbsize) = .-_C_LABEL(ddblow)
#endif	/* DDB */

#ifdef IPKDB
#define	ipkdbsave	0xde0		/* primary save area for IPKDB */
/*
 * In case of IPKDB we want a separate trap catcher for it
 */

	.local	ipkdbstk
	.comm	ipkdbstk,INTSTK,8		/* ipkdb stack */

	.globl	_C_LABEL(ipkdblow),_C_LABEL(ipkdbsize)
_C_LABEL(ipkdblow):
	mtsprg	1,1			/* save SP */
	GET_CPUINFO(1)
	stmw	28,CI_IPKDBSAVE(1)	/* free r28-r31 */
	mflr	28			/* save LR */
	mfcr	29			/* save CR */
	lis	1,ipkdbstk+INTSTK@ha	/* get new SP */
	addi	1,1,ipkdbstk+INTSTK@l
	bla	ipkdbtrap
_C_LABEL(ipkdbsize) = .-_C_LABEL(ipkdblow)
#endif	/* IPKDB */

/*
 * FRAME_SETUP assumes:
 *	SPRG1		SP (1)
 *	savearea	r28-r31,DAR,DSISR	(DAR & DSISR only for DSI traps)
 *	28		LR
 *	29		CR
 *	1		kernel stack
 *	LR		trap type
 *	SRR0/1		as at start of trap
 */
#define	FRAME_SETUP(savearea)						\
/* Have to enable translation to allow access of kernel stack: */	\
	GET_CPUINFO(31);						\
	mfsrr0	30;							\
	stw	30,savearea+24(31);					\
	mfsrr1	30;							\
	stw	30,savearea+28(31);					\
	mfmsr	30;							\
	ori	30,30,(PSL_DR|PSL_IR);					\
	mtmsr	30;							\
	isync;								\
	mfsprg	31,1;							\
	stwu	31,-FRAMELEN(1);					\
	stw	0,FRAME_0+8(1);						\
	stw	31,FRAME_1+8(1);					\
	stw	28,FRAME_LR+8(1);					\
	stw	29,FRAME_CR+8(1);					\
	stw	2,FRAME_2+8(1);						\
	GET_CPUINFO(2);							\
	lmw	28,savearea(2);						\
	stmw	3,FRAME_3+8(1);						\
	lmw	28,savearea+16(2);					\
	mfxer	3;							\
	mfctr	4;							\
	mflr	5;							\
	andi.	5,5,0xff00;						\
	stw	3,FRAME_XER+8(1);					\
	stw	4,FRAME_CTR+8(1);					\
	stw	5,FRAME_EXC+8(1);					\
	stw	28,FRAME_DAR+8(1);					\
	stw	29,FRAME_DSISR+8(1);					\
	stw	30,FRAME_SRR0+8(1);					\
	stw	31,FRAME_SRR1+8(1)

#define	FRAME_LEAVE(savearea)						\
/* Now restore regs: */							\
	lwz	2,FRAME_SRR0+8(1);					\
	lwz	3,FRAME_SRR1+8(1);					\
	lwz	4,FRAME_CTR+8(1);					\
	lwz	5,FRAME_XER+8(1);					\
	lwz	6,FRAME_LR+8(1);					\
	GET_CPUINFO(7);							\
	stw	2,savearea(7);						\
	stw	3,savearea+4(7);					\
	lwz	7,FRAME_CR+8(1);					\
	mtctr	4;							\
	mtxer	5;							\
	mtlr	6;							\
	mtsprg	1,7;			/* save cr */			\
	lmw	2,FRAME_2+8(1);						\
	lwz	0,FRAME_0+8(1);						\
	lwz	1,FRAME_1+8(1);						\
	mtsprg	2,2;			/* save r2 & r3 */		\
	mtsprg	3,3;							\
/* Disable translation, machine check and recoverability: */		\
	mfmsr	2;							\
	andi.	2,2,~(PSL_DR|PSL_IR|PSL_ME|PSL_RI)@l;			\
	mtmsr	2;							\
	isync;								\
/* Decide whether we return to user mode: */				\
	GET_CPUINFO(2);							\
	lwz	3,savearea+4(2);					\
	mtcr	3;							\
	bc	4,17,1f;		/* branch if PSL_PR is false */	\
/* Restore user & kernel access SR: */					\
	GET_CPUINFO(2);							\
	lwz	2,CI_CURPM(2);		/* get real address of pmap */	\
	lwz	3,PM_USRSR(2);						\
	mtsr	USER_SR,3;						\
	lwz	3,PM_KERNELSR(2);					\
	mtsr	KERNEL_SR,3;						\
1:	mfsprg	2,1;			/* restore cr */		\
	mtcr	2;							\
	GET_CPUINFO(2);							\
	lwz	3,savearea(2);						\
	mtsrr0	3;							\
	lwz	3,savearea+4(2);					\
	mtsrr1	3;							\
	mfsprg	2,2;			/* restore r2 & r3 */		\
	mfsprg	3,3

/*
 * Preamble code for DSI/ISI traps
 */
disitrap:
	GET_CPUINFO(1)
	lmw	30,CI_DISISAVE(1)
	stmw	30,CI_TEMPSAVE(1)
	lmw	30,CI_DISISAVE+8(1)
	stmw	30,CI_TEMPSAVE+8(1)
	mfdar	30
	mfdsisr	31
	stmw	30,CI_TEMPSAVE+16(1)
realtrap:
/* Test whether we already had PR set */
	mfsrr1	1
	mtcr	1
	mfsprg	1,1			/* restore SP (might have been
					   overwritten) */
	bc	4,17,s_trap		/* branch if PSL_PR is false */
	GET_CPUINFO(1)
	lwz	1,CI_CURPCB(1)
	addi	1,1,USPACE		/* stack is top of user struct */

/*
 * Now the common trap catching code.
 */
s_trap:
/* First have to enable KERNEL mapping */
	lis	31,KERNEL_SEGMENT@h
	ori	31,31,KERNEL_SEGMENT@l
	mtsr	KERNEL_SR,31
	FRAME_SETUP(CI_TEMPSAVE)
/* Now we can recover interrupts again: */
	mfmsr	7
	ori	7,7,(PSL_EE|PSL_ME|PSL_RI)@l
	mtmsr	7
	isync
/* Call C trap code: */
trapagain:
	addi	3,1,8
	bl	_C_LABEL(trap)
	.globl	trapexit
trapexit:
/* Disable interrupts: */
	mfmsr	3
	andi.	3,3,~PSL_EE@l
	mtmsr	3
/* Test AST pending: */
	lwz	5,FRAME_SRR1+8(1)
	mtcr	5
	bc	4,17,1f			/* branch if PSL_PR is false */
	GET_CPUINFO(3)
	lwz	4,CI_ASTPENDING(3)
	andi.	4,4,1
	beq	1f
	li	6,EXC_AST
	stw	6,FRAME_EXC+8(1)
	b	trapagain
1:
	FRAME_LEAVE(CI_TEMPSAVE)
	rfi

/*
 * DSI second stage fault handler
 */
s_dsitrap:
	mfdsisr	31			/* test whether this may be a
					   spill fault */
	mtcr	31
	mtsprg	1,1			/* save SP */
	bc	4,1,disitrap		/* branch if table miss is false */
	GET_CPUINFO(1)
	lwz	1,CI_SPILLSTK(1)	/* get spill stack */
	stwu	1,-52(1)
	stw	0,48(1)			/* save non-volatile registers */
	stw	3,44(1)
	stw	4,40(1)
	stw	5,36(1)
	stw	6,32(1)
	stw	7,28(1)
	stw	8,24(1)
	stw	9,20(1)
	stw	10,16(1)
	stw	11,12(1)
	stw	12,8(1)
	mflr	30			/* save trap type */
	mfctr	31			/* & CTR */
	mfdar	3
s_pte_spill:
	bl	_C_LABEL(pmap_pte_spill) /* try a spill */
	or.	3,3,3
	mtctr	31			/* restore CTR */
	mtlr	30			/* and trap type */
	mfsprg	31,2			/* get saved XER */
	mtxer	31			/* restore XER */
	lwz	12,8(1)			/* restore non-volatile registers */
	lwz	11,12(1)
	lwz	10,16(1)
	lwz	9,20(1)
	lwz	8,24(1)
	lwz	7,28(1)
	lwz	6,32(1)
	lwz	5,36(1)
	lwz	4,40(1)
	lwz	3,44(1)
	lwz	0,48(1)
	beq	disitrap
	mtcr	29			/* restore CR */
	mtlr	28			/* restore LR */
	GET_CPUINFO(1)
	lmw	28,CI_DISISAVE(1)	/* restore r28-r31 */
	mfsprg	1,1			/* restore SP */
	rfi				/* return to trapped code */

/*
 * ISI second stage fault handler
 */
s_isitrap:
	mfsrr1	31			/* test whether this may be a
					   spill fault */
	mtcr	31
	mtsprg	1,1			/* save SP */
	bc	4,1,disitrap		/* branch if table miss is false */
	GET_CPUINFO(1)
	lwz	1,CI_SPILLSTK(1)	/* get spill stack */
	stwu	1,-52(1)
	stw	0,48(1)			/* save non-volatile registers */
	stw	3,44(1)
	stw	4,40(1)
	stw	5,36(1)
	stw	6,32(1)
	stw	7,28(1)
	stw	8,24(1)
	stw	9,20(1)
	stw	10,16(1)
	stw	11,12(1)
	stw	12,8(1)
	mfxer	30			/* save XER */
	mtsprg	2,30
	mflr	30			/* save trap type */
	mfctr	31			/* & ctr */
	mfsrr0	3
	b	s_pte_spill		/* above */

/*
 * External interrupt second level handler
 */
#define	INTRENTER							\
/* Save non-volatile registers: */					\
	stwu	1,-88(1);		/* temporarily */		\
	stw	0,84(1);						\
	mfsprg	0,1;			/* get original SP */		\
	stw	0,0(1);			/* and store it */		\
	stw	3,80(1);						\
	stw	4,76(1);						\
	stw	5,72(1);						\
	stw	6,68(1);						\
	stw	7,64(1);						\
	stw	8,60(1);						\
	stw	9,56(1);						\
	stw	10,52(1);						\
	stw	11,48(1);						\
	stw	12,44(1);						\
	stw	28,40(1);		/* saved LR */			\
	stw	29,36(1);		/* saved CR */			\
	stw	30,32(1);		/* saved XER */			\
	GET_CPUINFO(4);							\
	lmw	28,CI_TEMPSAVE(4);	/* restore r28-r31 */		\
	mfctr	6;							\
	lwz	5,CI_INTRDEPTH(4);					\
	mfsrr0	4;							\
	mfsrr1	3;							\
	stw	6,28(1);						\
	stw	5,20(1);						\
	stw	4,12(1);						\
	stw	3,8(1);							\
/* interrupts are recoverable here, and enable translation */		\
	lis	3,(KERNEL_SEGMENT|SR_SUKEY|SR_PRKEY)@h;			\
	ori	3,3,(KERNEL_SEGMENT|SR_SUKEY|SR_PRKEY)@l;		\
	mtsr	KERNEL_SR,3;						\
	mfmsr	5;							\
	ori	5,5,(PSL_IR|PSL_DR|PSL_RI);				\
	mtmsr	5;							\
	isync

	.globl	_C_LABEL(extint_call)
extintr:
	INTRENTER
_C_LABEL(extint_call):
	bl	_C_LABEL(extint_call)	/* to be filled in later */

intr_exit:
/* Disable interrupts (should already be disabled) and MMU here: */
	mfmsr	3
	andi.	3,3,~(PSL_EE|PSL_ME|PSL_RI|PSL_DR|PSL_IR)@l
	mtmsr	3
	isync
/* restore possibly overwritten registers: */
	lwz	12,44(1)
	lwz	11,48(1)
	lwz	10,52(1)
	lwz	9,56(1)
	lwz	8,60(1)
	lwz	7,64(1)
	lwz	6,8(1)
	lwz	5,12(1)
	lwz	4,28(1)
	lwz	3,32(1)
	mtsrr1	6
	mtsrr0	5
	mtctr	4
	mtxer	3
/* Returning to user mode? */
	mtcr	6			/* saved SRR1 */
	bc	4,17,1f			/* branch if PSL_PR is false */
	GET_CPUINFO(4)
	lwz	3,CI_CURPM(4)
	lwz	3,PM_KERNELSR(3)
	mtsr	KERNEL_SR,3		/* Restore kernel SR */
	lwz	4,CI_ASTPENDING(4)	 /* Test AST pending */
	andi.	4,4,1
	beq	1f
/* Setup for entry to realtrap: */
	lwz	3,0(1)			/* get saved SP */
	mtsprg	1,3
	li	6,EXC_AST
	GET_CPUINFO(3)
	stmw	28,CI_TEMPSAVE(3)	/* establish tempsave again */
	mtlr	6
	lwz	28,40(1)		/* saved LR */
	lwz	29,36(1)		/* saved CR */
	lwz	6,68(1)
	lwz	5,72(1)
	lwz	4,76(1)
	lwz	3,80(1)
	lwz	0,84(1)
	GET_CPUINFO(30)
	lwz	31,CI_INTRDEPTH(30)
	addi	31,31,-1		/* adjust reentrancy count */
	stw	31,CI_INTRDEPTH(30)
	b	realtrap
1:
/* Here is the normal exit of extintr: */
	lwz	5,36(1)
	lwz	6,40(1)
	mtcr	5
	mtlr	6
	lwz	6,68(1)
	lwz	5,72(1)
	GET_CPUINFO(3)
	lwz	4,CI_INTRDEPTH(3)
	addi	4,4,-1			/* adjust reentrancy count */
	stw	4,CI_INTRDEPTH(3)
	lwz	4,76(1)
	lwz	3,80(1)
	lwz	0,84(1)
	lwz	1,0(1)
	rfi

/*
 * Decrementer interrupt second level handler
 */
decrintr:
	INTRENTER
	addi	3,1,8			/* intr frame */
	bl	_C_LABEL(decr_intr)
	b	intr_exit

#ifdef DDB
/*
 * Deliberate entry to ddbtrap
 */
	.globl	_C_LABEL(ddb_trap)
_C_LABEL(ddb_trap):
	mtsprg	1,1
	mfmsr	3
	mtsrr1	3
	andi.	3,3,~(PSL_EE|PSL_ME)@l
	mtmsr	3			/* disable interrupts */
	isync
	GET_CPUINFO(3)
	stmw	28,CI_DDBSAVE(3)
	mflr	28
	li	29,EXC_BPT
	mtlr	29
	mfcr	29
	mtsrr0	28

/*
 * Now the ddb trap catching code.
 */
ddbtrap:
	FRAME_SETUP(CI_DDBSAVE)
/* Call C trap code: */
	addi	3,1,8
	bl	_C_LABEL(ddb_trap_glue)
	or.	3,3,3
	bne	ddbleave
/* This wasn't for DDB, so switch to real trap: */
	lwz	3,FRAME_EXC+8(1)	/* save exception */
	GET_CPUINFO(4)
	stw	3,CI_DDBSAVE+8(4)
	FRAME_LEAVE(CI_DDBSAVE)
	mtsprg	1,1			/* prepare for entrance to realtrap */
	GET_CPUINFO(1)
	stmw	28,CI_TEMPSAVE(1)
	mflr	28
	mfcr	29
	lwz	31,CI_DDBSAVE+8(1)
	mtlr	31
	mfsprg	1,1
	b	realtrap
ddbleave:
	FRAME_LEAVE(CI_DDBSAVE)
	rfi
#endif /* DDB */

#ifdef IPKDB
/*
 * Deliberate entry to ipkdbtrap
 */
	.globl	_C_LABEL(ipkdb_trap)
_C_LABEL(ipkdb_trap):
	mtsprg	1,1
	mfmsr	3
	mtsrr1	3
	andi.	3,3,~(PSL_EE|PSL_ME)@l
	mtmsr	3			/* disable interrupts */
	isync
	GET_CPUINFO(3)
	stmw	28,CI_IPKDBSAVE(3)
	mflr	28
	li	29,EXC_BPT
	mtlr	29
	mfcr	29
	mtsrr0	28

/*
 * Now the ipkdb trap catching code.
 */
ipkdbtrap:
	FRAME_SETUP(ipkdbsave)
/* Call C trap code: */
	addi	3,1,8
	bl	_C_LABEL(ipkdb_trap_glue)
	or.	3,3,3
	bne	ipkdbleave
/* This wasn't for IPKDB, so switch to real trap: */
	lwz	3,FRAME_EXC+8(1)	/* save exception */
	GET_CPUINFO(4)
	stw	3,CI_IPKDBSAVE+8(4)
	FRAME_LEAVE(CI_IPKDBSAVE)
	mtsprg	1,1			/* prepare for entrance to realtrap */
	GET_CPUINFO(1)
	stmw	28,CI_TEMPSAVE(1)
	mflr	28
	mfcr	29
	lwz	31,CI_IPKDBSAVE+8(1)
	mtlr	31
	mfsprg	1,1
	b	realtrap
ipkdbleave:
	FRAME_LEAVE(CI_IPKDBSAVE)
	rfi

ipkdbfault:
	ba	_ipkdbfault
_ipkdbfault:
	mfsrr0	3
	addi	3,3,4
	mtsrr0	3
	li	3,-1
	rfi

/*
 * int ipkdbfbyte(unsigned char *p)
 */
	.globl	_C_LABEL(ipkdbfbyte)
_C_LABEL(ipkdbfbyte):
	li	9,EXC_DSI		/* establish new fault routine */
	lwz	5,0(9)
	lis	6,ipkdbfault@ha
	lwz	6,ipkdbfault@l(6)
	stw	6,0(9)
#ifdef	IPKDBUSERHACK
	lis	8,_C_LABEL(ipkdbsr)@ha
	lwz	8,_C_LABEL(ipkdbsr)@l(8)
	mtsr	USER_SR,8
	isync
#endif
	dcbst	0,9			/* flush data... */
	sync
	icbi	0,9			/* and instruction caches */
	lbz	3,0(3)			/* fetch data */
	stw	5,0(9)			/* restore previous fault handler */
	dcbst	0,9			/* and flush data... */
	sync
	icbi	0,9			/* and instruction caches */
	blr

/*
 * int ipkdbsbyte(unsigned char *p, int c)
 */
	.globl	_C_LABEL(ipkdbsbyte)
_C_LABEL(ipkdbsbyte):
	li	9,EXC_DSI		/* establish new fault routine */
	lwz	5,0(9)
	lis	6,ipkdbfault@ha
	lwz	6,ipkdbfault@l(6)
	stw	6,0(9)
#ifdef	IPKDBUSERHACK
	lis	8,_C_LABEL(ipkdbsr)@ha
	lwz	8,_C_LABEL(ipkdbsr)@l(8)
	mtsr	USER_SR,8
	isync
#endif
	dcbst	0,9			/* flush data... */
	sync
	icbi	0,9			/* and instruction caches */
	mr	6,3
	xor	3,3,3
	stb	4,0(6)
	dcbst	0,6			/* Now do appropriate flushes
					   to data... */
	sync
	icbi	0,6			/* and instruction caches */
	stw	5,0(9)			/* restore previous fault handler */
	dcbst	0,9			/* and flush data... */
	sync
	icbi	0,9			/* and instruction caches */
	blr
#endif	/* IPKDB */
