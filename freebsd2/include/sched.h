/*
 * Copyright (c) 1998 John Birrell <jb@cimlogic.com.au>.
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
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: sched.h,v 1.2.2.1 1998/04/30 06:59:23 tg Exp $
 *
 * POSIX 1003.1c scheduling definitions.
 *
 */

#ifndef _SCHED_H_
#define _SCHED_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <time.h>

/*
 * Scheduling policies (unimplemented)
 */
#define	SCHED_FIFO	0	/* First in-first out scheduling policy.  */
#define	SCHED_RR	1	/* Round robin scheduling policy.         */
#define	SCHED_OTHER	2	/* Another scheduling policy.             */

/*
 * POSIX 1003.1 scheduling parameter structure.
 */
struct sched_param {
	int	sched_priority;	/* Process execution scheduling priority. */
};

/*
 * Scheduling function prototype definitions.
 */
__BEGIN_DECLS
int	sched_getparam __P((pid_t, const struct sched_param *));
int	sched_getscheduler __P((pid_t));
int	sched_get_priority_max __P((int));
int	sched_get_priority_min __P((int));
int	sched_rr_get_interval __P((pid_t, struct timespec *));
int	sched_setparam __P((pid_t, const struct sched_param *));
int	sched_setscheduler __P((pid_t, int, const struct sched_param *));
int	sched_yield __P((void));
__END_DECLS

#endif /* _SCHED_H_ */
