/* $NetBSD: exec.h,v 1.2 1996/03/14 23:11:12 mark Exp $ */

/*
 * Copyright (c) 1994 Mark Brinicombe.
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
 *	This product includes software developed by the RiscBSD team.
 * 4. The name "RiscBSD" nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RISCBSD ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL RISCBSD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_ARM32_EXEC_H_
#define	_ARM32_EXEC_H_

#define	__LDPGSZ	4096

/* Relocation format. */

struct relocation_info_arm6 {
        int r_address;                  /* offset in text or data segment */
        unsigned int r_symbolnum : 24,  /* ordinal number of add symbol */
        r_pcrel :  1,  /* 1 if value should be pc-relative */
        r_length :  2,  /* log base 2 of value's width */
        r_extern :  1,  /* 1 if need to add symbol to value */
        r_baserel :  1,  /* linkage table relative */
        r_jmptable :  1,  /* relocate to jump table */
        r_relative :  1,  /* load address relative */
        r_copy :  1;  /* run time copy */
};

#define relocation_info relocation_info_arm6
                                                                                                                                                                                       
/* No special executable format */
#define	cpu_exec_aout_makecmds(a, b)	ENOEXEC

#define ELF_TARG_CLASS		ELFCLASS32
#define ELF_TARG_DATA		ELFDATA2LSB
#define ELF_TARG_MACH		EM_ARM

#define _NLIST_DO_AOUT

#define _KERN_DO_AOUT

#endif	/* _ARM_EXEC_H_ */