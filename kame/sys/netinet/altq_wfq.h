/*
 * Copyright (C) 1997-1999
 *	Sony Computer Science Laboratories Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY SONY CSL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL SONY CSL OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: altq_wfq.h,v 1.3 1999/10/02 05:59:02 itojun Exp $
 */
/*
 *  March 27, 1997.  Written by Hiroshi Kyusojin of Keio University
 *  (kyu@mt.cs.keio.ac.jp). 
 */

#ifndef _NETINET_ALTQ_WFQ_H_
#define _NETINET_ALTQ_WFQ_H_

#include <netinet/altq.h>

#define DEFAULT_QSIZE	256
#define MAX_QSIZE	2048

#if defined(KERNEL) || defined(_KERNEL)

#define	HWM			(64 * 1024)
#define WFQ_QUOTA		512	/* quota bytes to send at a time */
#define WFQ_ADDQUOTA(q)		((q)->quota += WFQ_QUOTA * (q)->weight / 100)
#define ENABLE			0
#define DISABLE			1

typedef struct weighted_fair_queue{
	struct weighted_fair_queue *next, *prev;
	struct mbuf *head, *tail;
	int bytes;			/* bytes in this queue */
	int quota;			/* bytes sent in this round */
	int weight;			/* weight in percent */

	u_int sent_packets;		/* packets sent in this queue */
	u_int drop_packets;		/* dropped packets */
	u_quad_t sent_bytes;		/* bytes sent in this queue */
	u_quad_t drop_bytes;		/* bytes dropped in this queue */
} wfq;


typedef struct wfqstate {
	struct wfqstate *next;		/* for wfqstate list */
	struct ifnet *ifp;
	int nums;			/* number of queues */
	int hwm;			/* high water mark */
	int bytes;			/* total bytes in all the queues */
	wfq *rrp;			/* round robin pointer */
	wfq *queue;			/* pointer to queue list */
	u_long (*hash_func)(struct flowinfo *, int);
	u_int32_t fbmask;		/* filter bitmask */
} wfq_state_t;

#endif /* KERNEL */

#ifndef IFNAMSIZ
#define IFNAMSIZ        16
#endif

typedef struct wfq_interface{
	char wfq_ifacename[IFNAMSIZ];
	u_int wfq_ifacelen;
} wfq_iface_t;

#define WFQ_ENABLE		_IOW('Q', 1, struct wfq_interface)
#define WFQ_DISABLE		_IOW('Q', 2, struct wfq_interface)

#define WFQ_IF_ATTACH		_IOW('Q', 3, struct wfq_interface)
#define WFQ_IF_DETACH		_IOW('Q', 4, struct wfq_interface)

struct wfq_getqid{
	wfq_iface_t 	iface;
	struct flowinfo flow;
	u_long		qid;
};

#define WFQ_GET_QID		_IOWR('Q', 5, struct wfq_getqid)

struct wfq_setweight {
	wfq_iface_t iface;
	int qid;
	int weight;
};

#define WFQ_SET_WEIGHT		_IOWR('Q', 6, struct wfq_setweight)

typedef struct each_queue_stats {
	int bytes;		/* bytes in this queue */
	int weight;		/* weight in percent */
	u_int sent_packets;	/* number of packets sent in this queue */
	u_int drop_packets;	/* number of packets dropped in this queue */
	u_quad_t sent_bytes;	/* bytes sent in this queue */
	u_quad_t drop_bytes;	/* bytes dropped in this queue */
} queue_stats;

struct wfq_getstats{
	wfq_iface_t iface;
	int qid;
	queue_stats stats;
};

#define WFQ_GET_STATS		_IOWR('Q', 7, struct wfq_getstats)

struct wfq_conf {
	wfq_iface_t iface;
	int hash_policy;	/* hash policy */
	int nqueues;		/* number of queues */
	int qlimit;		/* queue size in bytes */
};

#define WFQ_HASH_DSTADDR	0	/* hash by dst address */
#define WFQ_HASH_SRCPORT	1	/* hash by src port */
#define WFQ_HASH_FULL		2	/* hash by all fields */

#define WFQ_CONFIG		_IOWR('Q', 8, struct wfq_conf)

#endif /* __NETINET_WFQ_H */