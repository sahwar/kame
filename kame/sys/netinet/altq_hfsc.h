/*
 * Copyright (c) 1997-1999 Carnegie Mellon University. All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation is hereby granted (including for commercial or
 * for-profit use), provided that both the copyright notice and this
 * permission notice appear in all copies of the software, derivative
 * works, or modified versions, and any portions thereof, and that
 * both notices appear in supporting documentation, and that credit
 * is given to Carnegie Mellon University in all publications reporting
 * on direct or indirect use of this code or its derivatives.
 *
 * THIS SOFTWARE IS EXPERIMENTAL AND IS KNOWN TO HAVE BUGS, SOME OF
 * WHICH MAY HAVE SERIOUS CONSEQUENCES.  CARNEGIE MELLON PROVIDES THIS
 * SOFTWARE IN ITS ``AS IS'' CONDITION, AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Carnegie Mellon encourages (but does not require) users of this
 * software to return any improvements or extensions that they make,
 * and to grant Carnegie Mellon the rights to redistribute these
 * changes without encumbrance.
 *
 * $Id: altq_hfsc.h,v 1.4 1999/10/01 04:38:05 kjc Exp $
 */
#ifndef _NETINET_ALTQ_HFSC_H_
#define _NETINET_ALTQ_HFSC_H_

#include <netinet/altq.h>
#include <netinet/altq_red.h>
#include <netinet/altq_rio.h>
#include <netinet/altq_classq.h>

#ifdef __cplusplus
extern "C" {
#endif 

struct hfsc_interface {
	char	hfsc_ifname[IFNAMSIZ];  /* interface name (e.g., fxp0) */
};

struct hfsc_attach {
	struct hfsc_interface	iface;
	u_int			bandwidth;  /* link bandwidth in bits/sec */
};

struct service_curve {
	u_int	m1;	/* slope of the first segment in bits/sec */
	u_int	d;	/* the x-projection of the first segment in msec */
	u_int	m2;	/* slope of the second segment in bits/sec */
};

struct hfsc_add_class {
	struct hfsc_interface	iface;
	u_long			parent_handle;
	struct service_curve	service_curve;
	int			qlimit;
	int			flags;

	u_long			class_handle;  /* return value */
};

/* special class handles */
#define HFSC_ROOTCLASS_HANDLE	0
#define HFSC_NULLCLASS_HANDLE	0

/* hfsc class flags */
#define HFCF_RED		0x0001	/* use RED */
#define HFCF_ECN		0x0002  /* use RED/ECN */
#define HFCF_RIO		0x0004  /* use RIO */
#define HFCF_CLEARDSCP		0x0010  /* clear diffserv codepoint */
#define HFCF_DEFAULTCLASS	0x1000	/* default class */

struct hfsc_delete_class {
	struct hfsc_interface	iface;
	u_long			class_handle;
};

/* service curve types */
#define HFSC_REALTIMESC		1
#define HFSC_LINKSHARINGSC	2
#define HFSC_DEFAULTSC		(HFSC_REALTIMESC|HFSC_LINKSHARINGSC)

struct hfsc_modify_class {
	struct hfsc_interface	iface;
	u_long			class_handle;
	struct service_curve	service_curve;
	int			sctype;
};

struct hfsc_add_filter {
	struct hfsc_interface	iface;
	u_long			class_handle;
	struct flow_filter	filter;

	u_long			filter_handle;  /* return value */
};

struct hfsc_delete_filter {
	struct hfsc_interface	iface;
	u_long			filter_handle;
};

struct class_stats {
	u_int			class_id;
	u_int			class_handle;
	struct service_curve	rsc;
	struct service_curve	fsc;

	u_int64_t		total;	/* total work in bytes */
	u_int64_t		cumul;	/* cumulative work in bytes
					   done by real-time criteria */
	u_int64_t		d;		/* deadline */
	u_int64_t		e;		/* eligible time */
	u_int64_t		vt;		/* virtual time */

	u_int			qlength;
	u_int			npackets;
	u_int			drops;
	u_int 			period;

	/* red and rio related info */
	int		qtype;
	struct redstats	red[3];
};

struct hfsc_class_stats {
	struct hfsc_interface	iface;
	int			nskip;		/* skip # of classes */
	int			nclasses;	/* # of class stats (WR) */
	u_int64_t		cur_time;	/* current time */
	u_int			hif_classes;	/* # of classes in the tree */
	u_int			hif_packets;	/* # of packets in the tree */
	struct class_stats	*stats;		/* pointer to stats array */
};

#define	HFSC_IF_ATTACH		_IOW('Q', 1, struct hfsc_attach)
#define	HFSC_IF_DETACH		_IOW('Q', 2, struct hfsc_interface)
#define HFSC_ENABLE		_IOW('Q', 3, struct hfsc_interface)
#define HFSC_DISABLE		_IOW('Q', 4, struct hfsc_interface)
#define	HFSC_ACC_ENABLE		_IOW('Q', 5, struct hfsc_interface)
#define	HFSC_ACC_DISABLE	_IOW('Q', 6, struct hfsc_interface)
#define HFSC_ADD_CLASS		_IOWR('Q', 7, struct hfsc_add_class)
#define HFSC_DEL_CLASS		_IOW('Q', 8, struct hfsc_delete_class)
#define HFSC_MOD_CLASS		_IOW('Q', 9, struct hfsc_modify_class)
#define HFSC_CLEAR_HIERARCHY	_IOW('Q', 10, struct hfsc_interface)
#define	HFSC_ADD_FILTER		_IOWR('Q', 11, struct hfsc_add_filter)
#define	HFSC_DEL_FILTER		_IOW('Q', 12, struct hfsc_delete_filter)
#define	HFSC_GETSTATS		_IOWR('Q', 13, struct hfsc_class_stats)

#if defined(KERNEL) || defined(_KERNEL)
/*
 * kernel internal service curve representation
 *	coordinates are given by 64 bit unsigned integers.
 *	x-axis: unit is clock count.  for the intel x86 architecture,
 *		the raw Pentium TSC (Timestamp Counter) value is used.
 *		virtual time is also calculated in this time scale.
 *	y-axis: unit is byte.
 *
 *	the service curve parameters are converted to the internal
 *	representation.
 *	the slope values are scaled to avoid overflow.
 *	the inverse slope values as well as the y-projection of the 1st
 *	segment are kept in order to to avoid 64-bit divide operations
 *	that are expensive on 32-bit architectures.
 *
 *  note: Intel Pentium TSC never wraps around in several thousands of years.
 *	x-axis doesn't wrap around for 1089 years with 1GHz clock.
 *      y-axis doesn't wrap around for 4358 years with 1Gbps bandwidth.
 */

/* kernel internal representation of a service curve */
struct internal_sc {
	u_int64_t	sm1;	/* scaled slope of the 1st segment */
	u_int64_t	ism1;	/* scaled inverse-slope of the 1st segment */
	u_int64_t	dx;	/* the x-projection of the 1st segment */
	u_int64_t	dy;	/* the y-projection of the 1st segment */
	u_int64_t	sm2;	/* scaled slope of the 2nd segment */
	u_int64_t	ism2;	/* scaled inverse-slope of the 2nd segment */
};

/* runtime service curve */
struct runtime_sc {
	u_int64_t	x;	/* current starting position on x-axis */
	u_int64_t	y;	/* current starting position on x-axis */
	u_int64_t	sm1;	/* scaled slope of the 1st segment */
	u_int64_t	ism1;	/* scaled inverse-slope of the 1st segment */
	u_int64_t	dx;	/* the x-projection of the 1st segment */
	u_int64_t	dy;	/* the y-projection of the 1st segment */
	u_int64_t	sm2;	/* scaled slope of the 2nd segment */
	u_int64_t	ism2;	/* scaled inverse-slope of the 2nd segment */
};

/* for TAILQ based ellist and actlist implementation */
struct hfsc_class;
typedef TAILQ_HEAD(_eligible, hfsc_class) ellist_t;
typedef TAILQ_ENTRY(hfsc_class) elentry_t;
typedef TAILQ_HEAD(_active, hfsc_class) actlist_t;
typedef TAILQ_ENTRY(hfsc_class) actentry_t;
#define ellist_first(s)		TAILQ_FIRST(s)
#define actlist_first(s)	TAILQ_FIRST(s)
#define actlist_last(s)		TAILQ_LAST(s, _active)

struct hfsc_class {
	u_int		cl_id;		/* class id (just for debug) */
	u_long		cl_handle;	/* class handle */
	struct hfsc_if	*cl_hif;	/* back pointer to struct hfsc_if */
	int		cl_flags;	/* misc flags */

	struct hfsc_class *cl_parent;	/* parent class */
	struct hfsc_class *cl_siblings;	/* sibling classes */
	struct hfsc_class *cl_children;	/* child classes */

	class_queue_t	*cl_q;		/* class queue structure */
	struct red	*cl_red;	/* RED state */
	struct pr_hdr	*cl_prhdr;	/* saved protocol header used by ECN */

	u_int64_t	cl_total;	/* total work in bytes */
	u_int64_t	cl_cumul;	/* cumulative work in bytes
					   done by real-time criteria */
	u_int64_t	cl_d;		/* deadline */
	u_int64_t	cl_e;		/* eligible time */
	u_int64_t	cl_vt;		/* virtual time */

	struct internal_sc *cl_rsc;	/* internal real-time service curve */
	struct internal_sc *cl_fsc;	/* internal fair service curve */
	struct runtime_sc  cl_deadline;	/* deadline curve */
	struct runtime_sc  cl_eligible;	/* eligible curve */
	struct runtime_sc  cl_virtual;	/* virtual curve */

	u_int		cl_vtperiod;	/* vt period sequence no */
	u_int		cl_parentperiod;  /* parent's vt period seqno */
	int		cl_nactive;	/* number of active children */
	actlist_t	*cl_actc;	/* active children list */

	actentry_t	cl_actlist;	/* active children list entry */
	elentry_t	cl_ellist;	/* eligible list entry */

	struct {
		u_int npackets;
		u_int drops;
		u_int period;
	} cl_stats;
};

/*
 * hfsc interface state
 */
struct hfsc_if {
	struct hfsc_if		*hif_next;	/* interface state list */
	struct ifnet		*hif_ifp;	/* backpointer to ifnet */
	struct hfsc_class	*hif_rootclass;		/* root class */
	struct hfsc_class	*hif_defaultclass;	/* default class */
	struct hfsc_class	*hif_peekcache;	/* cache for peek operation */

	struct callout_handle	hif_callout;	/* handle for timeouts */
	int			hif_armed;	/* timeout is set */

	u_int	hif_classes;			/* # of classes in the tree */
	u_int	hif_packets;			/* # of packets in the tree */
	u_int	hif_classid;			/* class id sequence number */
	
	ellist_t *hif_eligible;			/* eligible list */

	struct acc_classifier	hif_classifier;
};

#endif /* KERNEL */

#ifdef __cplusplus
}
#endif 

#endif /* _NETINET_ALTQ_HFSC_H_ */