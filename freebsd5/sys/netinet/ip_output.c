/*
 * Copyright (c) 2002 INRIA. All rights reserved.
 *
 * Implementation of Internet Group Management Protocol, Version 3.
 * Developed by Hitoshi Asaeda, INRIA, February 2002.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of INRIA nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 *	@(#)ip_output.c	8.3 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/netinet/ip_output.c,v 1.225.2.11 2005/03/02 19:50:12 andre Exp $
 */

#ifdef __FreeBSD__
#include "opt_mpath.h"
#endif
#include "opt_inet.h"
#include "opt_ipfw.h"
#include "opt_ipsec.h"
#include "opt_mac.h"
#include "opt_mbuf_stress_test.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mac.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/pfil.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef IGMPV3
#include <netinet/in_msf.h>
#endif
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>

#include <machine/in_cksum.h>

#if !(defined(__FreeBSD__) && __FreeBSD_version >= 503000)
#include "pf.h"
#endif
#if NPF > 0
#include <net/pfvar.h>
#endif

MALLOC_DEFINE(M_IPMOPTS, "ip_moptions", "internet multicast options");

#ifdef IPSEC
#include <netinet6/ipsec.h>
#include <netkey/key.h>
#ifdef IPSEC_DEBUG
#include <netkey/key_debug.h>
#else
#define	KEYDEBUG(lev,arg)
#endif
#endif /*IPSEC*/

#ifdef FAST_IPSEC
#include <netipsec/ipsec.h>
#include <netipsec/xform.h>
#include <netipsec/key.h>
#endif /*FAST_IPSEC*/

#define print_ip(x, a, y)	 printf("%s %d.%d.%d.%d%s",\
				x, (ntohl(a.s_addr)>>24)&0xFF,\
				  (ntohl(a.s_addr)>>16)&0xFF,\
				  (ntohl(a.s_addr)>>8)&0xFF,\
				  (ntohl(a.s_addr))&0xFF, y);

u_short ip_id;

#ifdef MBUF_STRESS_TEST
int mbuf_frag_size = 0;
SYSCTL_INT(_net_inet_ip, OID_AUTO, mbuf_frag_size, CTLFLAG_RW,
	&mbuf_frag_size, 0, "Fragment outgoing mbufs to this size");
#endif

static struct mbuf *ip_insertoptions(struct mbuf *, struct mbuf *, int *);
static int ip_getmopt_ifargs
	(struct sockopt *, struct ifnet **, struct in_addr *, struct in_addr *);
#ifdef IGMPV3
static int in_getmopt_ifargs
	(struct sockopt *, struct ifnet **, struct in_addr *, u_int32_t);
static int ip_getmopt_sgaddr
	(struct sockopt *, struct ifnet **, struct sockaddr_storage *, struct sockaddr_storage *);
#endif
static void	ip_mloopback
	(struct ifnet *, struct mbuf *, struct sockaddr_in *, int);
static int	ip_getmoptions(struct inpcb *, struct sockopt *);
static int	ip_pcbopts(struct inpcb *, int, struct mbuf *);
static int	ip_setmoptions(struct inpcb *, struct sockopt *);

int	ip_optcopy(struct ip *, struct ip *);


extern	struct protosw inetsw[];

/*
 * IP output.  The packet in mbuf chain m contains a skeletal IP
 * header (with len, off, ttl, proto, tos, src, dst).
 * The mbuf chain containing the packet will be freed.
 * The mbuf opt, if present, will not be freed.
 * In the IP forwarding case, the packet will arrive with options already
 * inserted, so must have a NULL opt pointer.
 */
int
ip_output(struct mbuf *m, struct mbuf *opt, struct route *ro,
	int flags, struct ip_moptions *imo, struct inpcb *inp)
{
	struct ip *ip;
	struct ifnet *ifp = NULL;	/* keep compiler happy */
	struct mbuf *m0;
	int hlen = sizeof (struct ip);
	int len, error = 0;
	struct sockaddr_in *dst = NULL;	/* keep compiler happy */
	struct in_ifaddr *ia = NULL;
	int isbroadcast, sw_csum;
	struct route iproute;
	struct in_addr odst;
#ifdef IPFIREWALL_FORWARD
	struct m_tag *fwd_tag = NULL;
#endif
#ifdef IPSEC
	struct secpolicy *sp = NULL;
#endif
#ifdef FAST_IPSEC
	struct secpolicy *sp = NULL;
	struct tdb_ident *tdbi;
	struct m_tag *mtag;
	int s;
#endif /* FAST_IPSEC */
	int mtu = 0;

	M_ASSERTPKTHDR(m);
	
	if (ro == NULL) {
		ro = &iproute;
		bzero(ro, sizeof (*ro));
	}

	if (inp != NULL)
		INP_LOCK_ASSERT(inp);

	if (opt) {
		len = 0;
		m = ip_insertoptions(m, opt, &len);
		if (len != 0)
			hlen = len;
	}
	ip = mtod(m, struct ip *);

	/*
	 * Fill in IP header.  If we are not allowing fragmentation,
	 * then the ip_id field is meaningless, but we don't set it
	 * to zero.  Doing so causes various problems when devices along
	 * the path (routers, load balancers, firewalls, etc.) illegally
	 * disable DF on our packet.  Note that a 16-bit counter
	 * will wrap around in less than 10 seconds at 100 Mbit/s on a
	 * medium with MTU 1500.  See Steven M. Bellovin, "A Technique
	 * for Counting NATted Hosts", Proc. IMW'02, available at
	 * <http://www.research.att.com/~smb/papers/fnat.pdf>.
	 */
	if ((flags & (IP_FORWARDING|IP_RAWOUTPUT)) == 0) {
		ip->ip_v = IPVERSION;
		ip->ip_hl = hlen >> 2;
		ip->ip_id = ip_newid();
		ipstat.ips_localout++;
	} else {
		hlen = ip->ip_hl << 2;
	}

	dst = (struct sockaddr_in *)&ro->ro_dst;
again:
	/*
	 * If there is a cached route,
	 * check that it is to the same destination
	 * and is still up.  If not, free it and try again.
	 * The address family should also be checked in case of sharing the
	 * cache with IPv6.
	 */
	if (ro->ro_rt && ((ro->ro_rt->rt_flags & RTF_UP) == 0 ||
			  dst->sin_family != AF_INET ||
			  dst->sin_addr.s_addr != ip->ip_dst.s_addr)) {
		RTFREE(ro->ro_rt);
		ro->ro_rt = (struct rtentry *)0;
	}
#ifdef IPFIREWALL_FORWARD
	if (ro->ro_rt == NULL && fwd_tag == NULL) {
#else
	if (ro->ro_rt == NULL) {
#endif
		bzero(dst, sizeof(*dst));
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = ip->ip_dst;
	}
	/*
	 * If routing to interface only,
	 * short circuit routing lookup.
	 */
	if (flags & IP_ROUTETOIF) {
		if ((ia = ifatoia(ifa_ifwithdstaddr(sintosa(dst)))) == NULL &&
		    (ia = ifatoia(ifa_ifwithnet(sintosa(dst)))) == NULL) {
			ipstat.ips_noroute++;
			error = ENETUNREACH;
			goto bad;
		}
		ifp = ia->ia_ifp;
		ip->ip_ttl = 1;
		isbroadcast = in_broadcast(dst->sin_addr, ifp);
		mtu = ifp->if_mtu;
	} else if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) &&
	    imo != NULL && imo->imo_multicast_ifp != NULL) {
		/*
		 * Bypass the normal routing lookup for multicast
		 * packets if the interface is specified.
		 */
		ifp = imo->imo_multicast_ifp;
		IFP_TO_IA(ifp, ia);
		isbroadcast = 0;	/* fool gcc */
		mtu = ifp->if_mtu;
	} else {
		/*
		 * We want to do any cloning requested by the link layer,
		 * as this is probably required in all cases for correct
		 * operation (as it is for ARP).
		 */
		if (ro->ro_rt == NULL)
#ifdef RADIX_MPATH
			rtalloc_mpath(ro,
			    ntohl(ip->ip_src.s_addr ^ ip->ip_dst.s_addr));
#else
			rtalloc_ign(ro, 0);
#endif
		if (ro->ro_rt == NULL) {
			ipstat.ips_noroute++;
			error = EHOSTUNREACH;
			goto bad;
		}
		ia = ifatoia(ro->ro_rt->rt_ifa);
		ifp = ro->ro_rt->rt_ifp;
		if ((mtu = ro->ro_rt->rt_rmx.rmx_mtu) == 0)
			mtu = ifp->if_mtu;
		ro->ro_rt->rt_rmx.rmx_pksent++;
		if (ro->ro_rt->rt_flags & RTF_GATEWAY)
			dst = (struct sockaddr_in *)ro->ro_rt->rt_gateway;
		if (ro->ro_rt->rt_flags & RTF_HOST)
			isbroadcast = (ro->ro_rt->rt_flags & RTF_BROADCAST);
		else
			isbroadcast = in_broadcast(dst->sin_addr, ifp);
	}
	if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
		struct in_multi *inm;

		m->m_flags |= M_MCAST;
		/*
		 * IP destination address is multicast.  Make sure "dst"
		 * still points to the address in "ro".  (It may have been
		 * changed to point to a gateway address, above.)
		 */
		dst = (struct sockaddr_in *)&ro->ro_dst;
		/*
		 * See if the caller provided any multicast options
		 */
		if (imo != NULL) {
			ip->ip_ttl = imo->imo_multicast_ttl;
			if (imo->imo_multicast_vif != -1)
				ip->ip_src.s_addr =
				    ip_mcast_src ?
				    ip_mcast_src(imo->imo_multicast_vif) :
				    INADDR_ANY;
		} else
			ip->ip_ttl = IP_DEFAULT_MULTICAST_TTL;
		/*
		 * Confirm that the outgoing interface supports multicast.
		 */
		if ((imo == NULL) || (imo->imo_multicast_vif == -1)) {
			if ((ifp->if_flags & IFF_MULTICAST) == 0) {
				ipstat.ips_noroute++;
				error = ENETUNREACH;
				goto bad;
			}
		}
		/*
		 * If source address not specified yet, use address
		 * of outgoing interface.
		 */
		if (ip->ip_src.s_addr == INADDR_ANY) {
			/* Interface may have no addresses. */
			if (ia != NULL)
				ip->ip_src = IA_SIN(ia)->sin_addr;
		}

		/*
		 * XXXRW: Should the in_multi_mtx be held over
		 * ip_mloopback() or ip_mforward()?
		 */
		IN_MULTI_LOCK();
		IN_LOOKUP_MULTI(ip->ip_dst, ifp, inm);
		if (inm != NULL &&
		   (imo == NULL || imo->imo_multicast_loop)) {
			IN_MULTI_UNLOCK();
			/*
			 * If we belong to the destination multicast group
			 * on the outgoing interface, and the caller did not
			 * forbid loopback, loop back a copy.
			 */
			ip_mloopback(ifp, m, dst, hlen);
		}
		else {
			IN_MULTI_UNLOCK();
			/*
			 * If we are acting as a multicast router, perform
			 * multicast forwarding as if the packet had just
			 * arrived on the interface to which we are about
			 * to send.  The multicast forwarding function
			 * recursively calls this function, using the
			 * IP_FORWARDING flag to prevent infinite recursion.
			 *
			 * Multicasts that are looped back by ip_mloopback(),
			 * above, will be forwarded by the ip_input() routine,
			 * if necessary.
			 */
			if (ip_mrouter && (flags & IP_FORWARDING) == 0) {
				/*
				 * If rsvp daemon is not running, do not
				 * set ip_moptions. This ensures that the packet
				 * is multicast and not just sent down one link
				 * as prescribed by rsvpd.
				 */
				if (!rsvp_on)
					imo = NULL;
				if (ip_mforward &&
				    ip_mforward(ip, ifp, m, imo) != 0) {
					m_freem(m);
					goto done;
				}
			}
		}

		/*
		 * Multicasts with a time-to-live of zero may be looped-
		 * back, above, but must not be transmitted on a network.
		 * Also, multicasts addressed to the loopback interface
		 * are not sent -- the above call to ip_mloopback() will
		 * loop back a copy if this host actually belongs to the
		 * destination group on the loopback interface.
		 */
		if (ip->ip_ttl == 0 || ifp->if_flags & IFF_LOOPBACK) {
			m_freem(m);
			goto done;
		}

		goto sendit;
	}
#ifndef notdef
	/*
	 * If the source address is not specified yet, use the address
	 * of the outoing interface.
	 */
	if (ip->ip_src.s_addr == INADDR_ANY) {
		/* Interface may have no addresses. */
		if (ia != NULL) {
			ip->ip_src = IA_SIN(ia)->sin_addr;
		}
	}
#endif /* notdef */
#ifdef ALTQ
	/*
	 * disable packet drop hack.
	 * packetdrop should be done by queueing.
	 */
#else /* !ALTQ */
	/*
	 * Verify that we have any chance at all of being able to queue
	 *      the packet or packet fragments
	 */
	if ((ifp->if_snd.ifq_len + ip->ip_len / ifp->if_mtu + 1) >=
		ifp->if_snd.ifq_maxlen) {
			error = ENOBUFS;
			ipstat.ips_odropped++;
			goto bad;
	}
#endif /* !ALTQ */

	/*
	 * Look for broadcast address and
	 * verify user is allowed to send
	 * such a packet.
	 */
	if (isbroadcast) {
		if ((ifp->if_flags & IFF_BROADCAST) == 0) {
			error = EADDRNOTAVAIL;
			goto bad;
		}
		if ((flags & IP_ALLOWBROADCAST) == 0) {
			error = EACCES;
			goto bad;
		}
		/* don't allow broadcast messages to be fragmented */
		if (ip->ip_len > ifp->if_mtu) {
			error = EMSGSIZE;
			goto bad;
		}
		if (flags & IP_SENDONES)
			ip->ip_dst.s_addr = INADDR_BROADCAST;
		m->m_flags |= M_BCAST;
	} else {
		m->m_flags &= ~M_BCAST;
	}

sendit:
#ifdef IPSEC
	/* get SP for this packet */
	if (inp == NULL)
		sp = ipsec4_getpolicybyaddr(m, IPSEC_DIR_OUTBOUND,
		    flags, &error);
	else
		sp = ipsec4_getpolicybypcb(m, IPSEC_DIR_OUTBOUND, inp, &error);

	if (sp == NULL) {
		ipsecstat.out_inval++;
		goto bad;
	}

	error = 0;

	/* check policy */
	switch (sp->policy) {
	case IPSEC_POLICY_DISCARD:
		/*
		 * This packet is just discarded.
		 */
		ipsecstat.out_polvio++;
		goto bad;

	case IPSEC_POLICY_BYPASS:
	case IPSEC_POLICY_NONE:
	case IPSEC_POLICY_TCP:
		/* no need to do IPsec. */
		goto skip_ipsec;
	
	case IPSEC_POLICY_IPSEC:
		if (sp->req == NULL) {
			/* acquire a policy */
			error = key_spdacquire(sp);
			goto bad;
		}
		break;

	case IPSEC_POLICY_ENTRUST:
	default:
		printf("ip_output: Invalid policy found. %d\n", sp->policy);
	}
    {
	struct ipsec_output_state state;
	bzero(&state, sizeof(state));
	state.m = m;
	if (flags & IP_ROUTETOIF) {
		state.ro = &iproute;
		bzero(&iproute, sizeof(iproute));
	} else
		state.ro = ro;
	state.dst = (struct sockaddr *)dst;

	ip->ip_sum = 0;

	/*
	 * XXX
	 * delayed checksums are not currently compatible with IPsec
	 */
	if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		in_delayed_cksum(m);
		m->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}

	ip->ip_len = htons(ip->ip_len);
	ip->ip_off = htons(ip->ip_off);

	error = ipsec4_output(&state, sp, flags);

	m = state.m;
	if (flags & IP_ROUTETOIF) {
		/*
		 * if we have tunnel mode SA, we may need to ignore
		 * IP_ROUTETOIF.
		 */
		if (state.ro != &iproute || state.ro->ro_rt != NULL) {
			flags &= ~IP_ROUTETOIF;
			ro = state.ro;
		}
	} else
		ro = state.ro;
	dst = (struct sockaddr_in *)state.dst;
	if (error) {
		/* mbuf is already reclaimed in ipsec4_output. */
		m = NULL;
		switch (error) {
		case EHOSTUNREACH:
		case ENETUNREACH:
		case EMSGSIZE:
		case ENOBUFS:
		case ENOMEM:
			break;
		default:
			printf("ip4_output (ipsec): error code %d\n", error);
			/*fall through*/
		case ENOENT:
			/* don't show these error codes to the user */
			error = 0;
			break;
		}
		goto bad;
	}

	/* be sure to update variables that are affected by ipsec4_output() */
	ip = mtod(m, struct ip *);
	hlen = ip->ip_hl << 2;
	if (ro->ro_rt == NULL) {
		if ((flags & IP_ROUTETOIF) == 0) {
			printf("ip_output: "
				"can't update route after IPsec processing\n");
			error = EHOSTUNREACH;	/*XXX*/
			goto bad;
		}
	} else {
		if (state.encap) {
			ia = ifatoia(ro->ro_rt->rt_ifa);
			ifp = ro->ro_rt->rt_ifp;
		}
	}
    }

	/* make it flipped, again. */
	ip->ip_len = ntohs(ip->ip_len);
	ip->ip_off = ntohs(ip->ip_off);
skip_ipsec:
#endif /*IPSEC*/
#ifdef FAST_IPSEC
	/*
	 * Check the security policy (SP) for the packet and, if
	 * required, do IPsec-related processing.  There are two
	 * cases here; the first time a packet is sent through
	 * it will be untagged and handled by ipsec4_checkpolicy.
	 * If the packet is resubmitted to ip_output (e.g. after
	 * AH, ESP, etc. processing), there will be a tag to bypass
	 * the lookup and related policy checking.
	 */
	mtag = m_tag_find(m, PACKET_TAG_IPSEC_PENDING_TDB, NULL);
	s = splnet();
	if (mtag != NULL) {
		tdbi = (struct tdb_ident *)(mtag + 1);
		sp = ipsec_getpolicy(tdbi, IPSEC_DIR_OUTBOUND);
		if (sp == NULL)
			error = -EINVAL;	/* force silent drop */
		m_tag_delete(m, mtag);
	} else {
		sp = ipsec4_checkpolicy(m, IPSEC_DIR_OUTBOUND, flags,
					&error, inp);
	}
	/*
	 * There are four return cases:
	 *    sp != NULL	 	    apply IPsec policy
	 *    sp == NULL, error == 0	    no IPsec handling needed
	 *    sp == NULL, error == -EINVAL  discard packet w/o error
	 *    sp == NULL, error != 0	    discard packet, report error
	 */
	if (sp != NULL) {
		/* Loop detection, check if ipsec processing already done */
		KASSERT(sp->req != NULL, ("ip_output: no ipsec request"));
		for (mtag = m_tag_first(m); mtag != NULL;
		     mtag = m_tag_next(m, mtag)) {
			if (mtag->m_tag_cookie != MTAG_ABI_COMPAT)
				continue;
			if (mtag->m_tag_id != PACKET_TAG_IPSEC_OUT_DONE &&
			    mtag->m_tag_id != PACKET_TAG_IPSEC_OUT_CRYPTO_NEEDED)
				continue;
			/*
			 * Check if policy has an SA associated with it.
			 * This can happen when an SP has yet to acquire
			 * an SA; e.g. on first reference.  If it occurs,
			 * then we let ipsec4_process_packet do its thing.
			 */
			if (sp->req->sav == NULL)
				break;
			tdbi = (struct tdb_ident *)(mtag + 1);
			if (tdbi->spi == sp->req->sav->spi &&
			    tdbi->proto == sp->req->sav->sah->saidx.proto &&
			    bcmp(&tdbi->dst, &sp->req->sav->sah->saidx.dst,
				 sizeof (union sockaddr_union)) == 0) {
				/*
				 * No IPsec processing is needed, free
				 * reference to SP.
				 *
				 * NB: null pointer to avoid free at
				 *     done: below.
				 */
				KEY_FREESP(&sp), sp = NULL;
				splx(s);
				goto spd_done;
			}
		}

		/*
		 * Do delayed checksums now because we send before
		 * this is done in the normal processing path.
		 */
		if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
			in_delayed_cksum(m);
			m->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
		}

		ip->ip_len = htons(ip->ip_len);
		ip->ip_off = htons(ip->ip_off);

		/* NB: callee frees mbuf */
		error = ipsec4_process_packet(m, sp->req, flags, 0);
		/*
		 * Preserve KAME behaviour: ENOENT can be returned
		 * when an SA acquire is in progress.  Don't propagate
		 * this to user-level; it confuses applications.
		 *
		 * XXX this will go away when the SADB is redone.
		 */
		if (error == ENOENT)
			error = 0;
		splx(s);
		goto done;
	} else {
		splx(s);

		if (error != 0) {
			/*
			 * Hack: -EINVAL is used to signal that a packet
			 * should be silently discarded.  This is typically
			 * because we asked key management for an SA and
			 * it was delayed (e.g. kicked up to IKE).
			 */
			if (error == -EINVAL)
				error = 0;
			goto bad;
		} else {
			/* No IPsec processing for this packet. */
		}
#ifdef notyet
		/*
		 * If deferred crypto processing is needed, check that
		 * the interface supports it.
		 */
		mtag = m_tag_find(m, PACKET_TAG_IPSEC_OUT_CRYPTO_NEEDED, NULL);
		if (mtag != NULL && (ifp->if_capenable & IFCAP_IPSEC) == 0) {
			/* notify IPsec to do its own crypto */
			ipsp_skipcrypto_unmark((struct tdb_ident *)(mtag + 1));
			error = EHOSTUNREACH;
			goto bad;
		}
#endif
	}
spd_done:
#endif /* FAST_IPSEC */

	/* Jump over all PFIL processing if hooks are not active. */
	if (inet_pfil_hook.ph_busy_count == -1)
		goto passout;

	/* Run through list of hooks for output packets. */
	odst.s_addr = ip->ip_dst.s_addr;
	error = pfil_run_hooks(&inet_pfil_hook, &m, ifp, PFIL_OUT, inp);
	if (error != 0 || m == NULL)
		goto done;

	ip = mtod(m, struct ip *);

	/* See if destination IP address was changed by packet filter. */
	if (odst.s_addr != ip->ip_dst.s_addr) {
		m->m_flags |= M_SKIP_FIREWALL;
		if (in_localip(ip->ip_dst)) {
			m->m_flags |= M_FASTFWD_OURS;
			if (m->m_pkthdr.rcvif == NULL)
				m->m_pkthdr.rcvif = loif;
			if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
				m->m_pkthdr.csum_flags |=
				    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
			m->m_pkthdr.csum_flags |=
			    CSUM_IP_CHECKED | CSUM_IP_VALID;

			error = netisr_queue(NETISR_IP, m);
			goto done;
		} else
			goto again;
	}

#ifdef IPFIREWALL_FORWARD
	/* See if local, if yes, send it to netisr with IP_FASTFWD_OURS. */
	if (m->m_flags & M_FASTFWD_OURS) {
		if (m->m_pkthdr.rcvif == NULL)
			m->m_pkthdr.rcvif = loif;
		if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
			m->m_pkthdr.csum_flags |=
			    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
			m->m_pkthdr.csum_data = 0xffff;
		}
		m->m_pkthdr.csum_flags |=
			    CSUM_IP_CHECKED | CSUM_IP_VALID;

		error = netisr_queue(NETISR_IP, m);
		goto done;
	}
	/* Or forward to some other address? */
	fwd_tag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL);
	if (fwd_tag) {
#ifndef IPFIREWALL_FORWARD_EXTENDED
		if (!in_localip(ip->ip_src) && !in_localaddr(ip->ip_dst)) {
#endif
			dst = (struct sockaddr_in *)&ro->ro_dst;
			bcopy((fwd_tag+1), dst, sizeof(struct sockaddr_in));
			m->m_flags |= M_SKIP_FIREWALL;
			m_tag_delete(m, fwd_tag);
			goto again;
#ifndef IPFIREWALL_FORWARD_EXTENDED
		} else {
			m_tag_delete(m, fwd_tag);
			/* Continue. */
		}
#endif
	}
#endif /* IPFIREWALL_FORWARD */

passout:
	/* 127/8 must not appear on wire - RFC1122. */
	if ((ntohl(ip->ip_dst.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET ||
	    (ntohl(ip->ip_src.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) {
		if ((ifp->if_flags & IFF_LOOPBACK) == 0) {
			ipstat.ips_badaddr++;
			error = EADDRNOTAVAIL;
			goto bad;
		}
	}

	m->m_pkthdr.csum_flags |= CSUM_IP;
	sw_csum = m->m_pkthdr.csum_flags & ~ifp->if_hwassist;
	if (sw_csum & CSUM_DELAY_DATA) {
		in_delayed_cksum(m);
		sw_csum &= ~CSUM_DELAY_DATA;
	}
	m->m_pkthdr.csum_flags &= ifp->if_hwassist;

	/*
	 * If small enough for interface, or the interface will take
	 * care of the fragmentation for us, can just send directly.
	 */
	if (mtu == 0)
		mtu = ifp->if_mtu;
	if (ip->ip_len <= mtu || (ifp->if_hwassist & CSUM_FRAGMENT &&
	    ((ip->ip_off & IP_DF) == 0))) {
		ip->ip_len = htons(ip->ip_len);
		ip->ip_off = htons(ip->ip_off);
		ip->ip_sum = 0;
		if (sw_csum & CSUM_DELAY_IP)
			ip->ip_sum = in_cksum(m, hlen);

		/* Record statistics for this interface address. */
		if (!(flags & IP_FORWARDING) && ia) {
			ia->ia_ifa.if_opackets++;
			ia->ia_ifa.if_obytes += m->m_pkthdr.len;
		}

#ifdef IPSEC
		/* clean ipsec history once it goes out of the node */
		ipsec_delaux(m);
#endif

#ifdef MBUF_STRESS_TEST
		if (mbuf_frag_size && m->m_pkthdr.len > mbuf_frag_size)
			m = m_fragment(m, M_DONTWAIT, mbuf_frag_size);
#endif
		error = (*ifp->if_output)(ifp, m,
				(struct sockaddr *)dst, ro->ro_rt);
		goto done;
	}

	if (ip->ip_off & IP_DF) {
		error = EMSGSIZE;
		/*
		 * This case can happen if the user changed the MTU
		 * of an interface after enabling IP on it.  Because
		 * most netifs don't keep track of routes pointing to
		 * them, there is no way for one to update all its
		 * routes when the MTU is changed.
		 */
		if ((ro->ro_rt->rt_flags & (RTF_UP | RTF_HOST)) &&
		    (ro->ro_rt->rt_rmx.rmx_mtu > ifp->if_mtu)) {
			ro->ro_rt->rt_rmx.rmx_mtu = ifp->if_mtu;
		}
		ipstat.ips_cantfrag++;
		goto bad;
	}

	/*
	 * Too large for interface; fragment if possible. If successful,
	 * on return, m will point to a list of packets to be sent.
	 */
	error = ip_fragment(ip, &m, ifp->if_mtu, ifp->if_hwassist, sw_csum);
	if (error)
		goto bad;
	for (; m; m = m0) {
		m0 = m->m_nextpkt;
		m->m_nextpkt = 0;
#ifdef IPSEC
		/* clean ipsec history once it goes out of the node */
		ipsec_delaux(m);
#endif
		if (error == 0) {
			/* Record statistics for this interface address. */
			if (ia != NULL) {
				ia->ia_ifa.if_opackets++;
				ia->ia_ifa.if_obytes += m->m_pkthdr.len;
			}
			
			error = (*ifp->if_output)(ifp, m,
			    (struct sockaddr *)dst, ro->ro_rt);
		} else
			m_freem(m);
	}

	if (error == 0)
		ipstat.ips_fragmented++;

done:
	if (ro == &iproute && ro->ro_rt) {
		RTFREE(ro->ro_rt);
	}
#ifdef IPSEC
	if (sp != NULL) {
		KEYDEBUG(KEYDEBUG_IPSEC_STAMP,
			printf("DP ip_input call free SP:%p\n", sp));
		key_freesp(sp);
	}
#endif
#ifdef FAST_IPSEC
	if (sp != NULL)
		KEY_FREESP(&sp);
#endif /* FAST_IPSEC */
	return (error);
bad:
	m_freem(m);
	goto done;
}

/*
 * Create a chain of fragments which fit the given mtu. m_frag points to the
 * mbuf to be fragmented; on return it points to the chain with the fragments.
 * Return 0 if no error. If error, m_frag may contain a partially built
 * chain of fragments that should be freed by the caller.
 *
 * if_hwassist_flags is the hw offload capabilities (see if_data.ifi_hwassist)
 * sw_csum contains the delayed checksums flags (e.g., CSUM_DELAY_IP).
 */
int
ip_fragment(struct ip *ip, struct mbuf **m_frag, int mtu,
	    u_long if_hwassist_flags, int sw_csum)
{
	int error = 0;
	int hlen = ip->ip_hl << 2;
	int len = (mtu - hlen) & ~7;	/* size of payload in each fragment */
	int off;
	struct mbuf *m0 = *m_frag;	/* the original packet		*/
	int firstlen;
	struct mbuf **mnext;
	int nfrags;

	if (ip->ip_off & IP_DF) {	/* Fragmentation not allowed */
		ipstat.ips_cantfrag++;
		return EMSGSIZE;
	}

	/*
	 * Must be able to put at least 8 bytes per fragment.
	 */
	if (len < 8)
		return EMSGSIZE;

	/*
	 * If the interface will not calculate checksums on
	 * fragmented packets, then do it here.
	 */
	if (m0->m_pkthdr.csum_flags & CSUM_DELAY_DATA &&
	    (if_hwassist_flags & CSUM_IP_FRAGS) == 0) {
		in_delayed_cksum(m0);
		m0->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}

	if (len > PAGE_SIZE) {
		/* 
		 * Fragment large datagrams such that each segment 
		 * contains a multiple of PAGE_SIZE amount of data, 
		 * plus headers. This enables a receiver to perform 
		 * page-flipping zero-copy optimizations.
		 *
		 * XXX When does this help given that sender and receiver
		 * could have different page sizes, and also mtu could
		 * be less than the receiver's page size ?
		 */
		int newlen;
		struct mbuf *m;

		for (m = m0, off = 0; m && (off+m->m_len) <= mtu; m = m->m_next)
			off += m->m_len;

		/*
		 * firstlen (off - hlen) must be aligned on an 
		 * 8-byte boundary
		 */
		if (off < hlen)
			goto smart_frag_failure;
		off = ((off - hlen) & ~7) + hlen;
		newlen = (~PAGE_MASK) & mtu;
		if ((newlen + sizeof (struct ip)) > mtu) {
			/* we failed, go back the default */
smart_frag_failure:
			newlen = len;
			off = hlen + len;
		}
		len = newlen;

	} else {
		off = hlen + len;
	}

	firstlen = off - hlen;
	mnext = &m0->m_nextpkt;		/* pointer to next packet */

	/*
	 * Loop through length of segment after first fragment,
	 * make new header and copy data of each part and link onto chain.
	 * Here, m0 is the original packet, m is the fragment being created.
	 * The fragments are linked off the m_nextpkt of the original
	 * packet, which after processing serves as the first fragment.
	 */
	for (nfrags = 1; off < ip->ip_len; off += len, nfrags++) {
		struct ip *mhip;	/* ip header on the fragment */
		struct mbuf *m;
		int mhlen = sizeof (struct ip);

		MGETHDR(m, M_DONTWAIT, MT_HEADER);
		if (m == NULL) {
			error = ENOBUFS;
			ipstat.ips_odropped++;
			goto done;
		}
		m->m_flags |= (m0->m_flags & M_MCAST) | M_FRAG;
		/*
		 * In the first mbuf, leave room for the link header, then
		 * copy the original IP header including options. The payload
		 * goes into an additional mbuf chain returned by m_copy().
		 */
		m->m_data += max_linkhdr;
		mhip = mtod(m, struct ip *);
		*mhip = *ip;
		if (hlen > sizeof (struct ip)) {
			mhlen = ip_optcopy(ip, mhip) + sizeof (struct ip);
			mhip->ip_v = IPVERSION;
			mhip->ip_hl = mhlen >> 2;
		}
		m->m_len = mhlen;
		/* XXX do we need to add ip->ip_off below ? */
		mhip->ip_off = ((off - hlen) >> 3) + ip->ip_off;
		if (off + len >= ip->ip_len) {	/* last fragment */
			len = ip->ip_len - off;
			m->m_flags |= M_LASTFRAG;
		} else
			mhip->ip_off |= IP_MF;
		mhip->ip_len = htons((u_short)(len + mhlen));
		m->m_next = m_copy(m0, off, len);
		if (m->m_next == NULL) {	/* copy failed */
			m_free(m);
			error = ENOBUFS;	/* ??? */
			ipstat.ips_odropped++;
			goto done;
		}
		m->m_pkthdr.len = mhlen + len;
		m->m_pkthdr.rcvif = (struct ifnet *)0;
#ifdef MAC
		mac_create_fragment(m0, m);
#endif
		m->m_pkthdr.csum_flags = m0->m_pkthdr.csum_flags;
		mhip->ip_off = htons(mhip->ip_off);
		mhip->ip_sum = 0;
		if (sw_csum & CSUM_DELAY_IP)
			mhip->ip_sum = in_cksum(m, mhlen);
		*mnext = m;
		mnext = &m->m_nextpkt;
	}
	ipstat.ips_ofragments += nfrags;

	/* set first marker for fragment chain */
	m0->m_flags |= M_FIRSTFRAG | M_FRAG;
	m0->m_pkthdr.csum_data = nfrags;

	/*
	 * Update first fragment by trimming what's been copied out
	 * and updating header.
	 */
	m_adj(m0, hlen + firstlen - ip->ip_len);
	m0->m_pkthdr.len = hlen + firstlen;
	ip->ip_len = htons((u_short)m0->m_pkthdr.len);
	ip->ip_off |= IP_MF;
	ip->ip_off = htons(ip->ip_off);
	ip->ip_sum = 0;
	if (sw_csum & CSUM_DELAY_IP)
		ip->ip_sum = in_cksum(m0, hlen);

done:
	*m_frag = m0;
	return error;
}

void
in_delayed_cksum(struct mbuf *m)
{
	struct ip *ip;
	u_short csum, offset;

	ip = mtod(m, struct ip *);
	offset = ip->ip_hl << 2 ;
	csum = in_cksum_skip(m, ip->ip_len, offset);
	if (m->m_pkthdr.csum_flags & CSUM_UDP && csum == 0)
		csum = 0xffff;
	offset += m->m_pkthdr.csum_data;	/* checksum offset */

	if (offset + sizeof(u_short) > m->m_len) {
		printf("delayed m_pullup, m->len: %d  off: %d  p: %d\n",
		    m->m_len, offset, ip->ip_p);
		/*
		 * XXX
		 * this shouldn't happen, but if it does, the
		 * correct behavior may be to insert the checksum
		 * in the existing chain instead of rearranging it.
		 */
		m = m_pullup(m, offset + sizeof(u_short));
	}
	*(u_short *)(m->m_data + offset) = csum;
}

/*
 * Insert IP options into preformed packet.
 * Adjust IP destination as required for IP source routing,
 * as indicated by a non-zero in_addr at the start of the options.
 *
 * XXX This routine assumes that the packet has no options in place.
 */
static struct mbuf *
ip_insertoptions(m, opt, phlen)
	register struct mbuf *m;
	struct mbuf *opt;
	int *phlen;
{
	register struct ipoption *p = mtod(opt, struct ipoption *);
	struct mbuf *n;
	register struct ip *ip = mtod(m, struct ip *);
	unsigned optlen;

	optlen = opt->m_len - sizeof(p->ipopt_dst);
	if (optlen + ip->ip_len > IP_MAXPACKET) {
		*phlen = 0;
		return (m);		/* XXX should fail */
	}
	if (p->ipopt_dst.s_addr)
		ip->ip_dst = p->ipopt_dst;
	if (m->m_flags & M_EXT || m->m_data - optlen < m->m_pktdat) {
		MGETHDR(n, M_DONTWAIT, MT_HEADER);
		if (n == NULL) {
			*phlen = 0;
			return (m);
		}
		M_MOVE_PKTHDR(n, m);
		n->m_pkthdr.rcvif = (struct ifnet *)0;
#ifdef MAC
		mac_create_mbuf_from_mbuf(m, n);
#endif
		n->m_pkthdr.len += optlen;
		m->m_len -= sizeof(struct ip);
		m->m_data += sizeof(struct ip);
		n->m_next = m;
		m = n;
		m->m_len = optlen + sizeof(struct ip);
		m->m_data += max_linkhdr;
		bcopy(ip, mtod(m, void *), sizeof(struct ip));
	} else {
		m->m_data -= optlen;
		m->m_len += optlen;
		m->m_pkthdr.len += optlen;
		bcopy(ip, mtod(m, void *), sizeof(struct ip));
	}
	ip = mtod(m, struct ip *);
	bcopy(p->ipopt_list, ip + 1, optlen);
	*phlen = sizeof(struct ip) + optlen;
	ip->ip_v = IPVERSION;
	ip->ip_hl = *phlen >> 2;
	ip->ip_len += optlen;
	return (m);
}

/*
 * Copy options from ip to jp,
 * omitting those not copied during fragmentation.
 */
int
ip_optcopy(ip, jp)
	struct ip *ip, *jp;
{
	register u_char *cp, *dp;
	int opt, optlen, cnt;

	cp = (u_char *)(ip + 1);
	dp = (u_char *)(jp + 1);
	cnt = (ip->ip_hl << 2) - sizeof (struct ip);
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[0];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP) {
			/* Preserve for IP mcast tunnel's LSRR alignment. */
			*dp++ = IPOPT_NOP;
			optlen = 1;
			continue;
		}

		KASSERT(cnt >= IPOPT_OLEN + sizeof(*cp),
		    ("ip_optcopy: malformed ipv4 option"));
		optlen = cp[IPOPT_OLEN];
		KASSERT(optlen >= IPOPT_OLEN + sizeof(*cp) && optlen <= cnt,
		    ("ip_optcopy: malformed ipv4 option"));

		/* bogus lengths should have been caught by ip_dooptions */
		if (optlen > cnt)
			optlen = cnt;
		if (IPOPT_COPIED(opt)) {
			bcopy(cp, dp, optlen);
			dp += optlen;
		}
	}
	for (optlen = dp - (u_char *)(jp+1); optlen & 0x3; optlen++)
		*dp++ = IPOPT_EOL;
	return (optlen);
}

/*
 * IP socket option processing.
 */
int
ip_ctloutput(so, sopt)
	struct socket *so;
	struct sockopt *sopt;
{
	struct	inpcb *inp = sotoinpcb(so);
	int	error, optval;

	error = optval = 0;
	if (sopt->sopt_level != IPPROTO_IP) {
		return (EINVAL);
	}

	switch (sopt->sopt_dir) {
	case SOPT_SET:
		switch (sopt->sopt_name) {
		case IP_OPTIONS:
#ifdef notyet
		case IP_RETOPTS:
#endif
		{
			struct mbuf *m;
			if (sopt->sopt_valsize > MLEN) {
				error = EMSGSIZE;
				break;
			}
			MGET(m, sopt->sopt_td ? M_TRYWAIT : M_DONTWAIT, MT_HEADER);
			if (m == NULL) {
				error = ENOBUFS;
				break;
			}
			m->m_len = sopt->sopt_valsize;
			error = sooptcopyin(sopt, mtod(m, char *), m->m_len,
					    m->m_len);
			INP_LOCK(inp);
			error = ip_pcbopts(inp, sopt->sopt_name, m);
			INP_UNLOCK(inp);
			return (error);
		}

		case IP_TOS:
		case IP_TTL:
		case IP_RECVOPTS:
		case IP_RECVRETOPTS:
		case IP_RECVDSTADDR:
		case IP_RECVTTL:
		case IP_RECVIF:
		case IP_FAITH:
		case IP_ONESBCAST:
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				break;

			switch (sopt->sopt_name) {
			case IP_TOS:
				inp->inp_ip_tos = optval;
				break;

			case IP_TTL:
				inp->inp_ip_ttl = optval;
				break;
#define	OPTSET(bit) do {						\
	INP_LOCK(inp);							\
	if (optval)							\
		inp->inp_flags |= bit;					\
	else								\
		inp->inp_flags &= ~bit;					\
	INP_UNLOCK(inp);						\
} while (0)

			case IP_RECVOPTS:
				OPTSET(INP_RECVOPTS);
				break;

			case IP_RECVRETOPTS:
				OPTSET(INP_RECVRETOPTS);
				break;

			case IP_RECVDSTADDR:
				OPTSET(INP_RECVDSTADDR);
				break;

			case IP_RECVTTL:
				OPTSET(INP_RECVTTL);
				break;

			case IP_RECVIF:
				OPTSET(INP_RECVIF);
				break;

			case IP_FAITH:
				OPTSET(INP_FAITH);
				break;

			case IP_ONESBCAST:
				OPTSET(INP_ONESBCAST);
				break;
			}
			break;
#undef OPTSET

		case IP_MULTICAST_IF:
		case IP_MULTICAST_VIF:
		case IP_MULTICAST_TTL:
		case IP_MULTICAST_LOOP:
		case IP_ADD_MEMBERSHIP:
		case IP_DROP_MEMBERSHIP:
#ifdef IGMPV3
		case IP_BLOCK_SOURCE:
		case IP_UNBLOCK_SOURCE:
		case IP_ADD_SOURCE_MEMBERSHIP:
		case IP_DROP_SOURCE_MEMBERSHIP:
		case MCAST_JOIN_GROUP:
		case MCAST_BLOCK_SOURCE:
		case MCAST_UNBLOCK_SOURCE:
		case MCAST_LEAVE_GROUP:
		case MCAST_JOIN_SOURCE_GROUP:
		case MCAST_LEAVE_SOURCE_GROUP:
#endif
			error = ip_setmoptions(inp, sopt);
			break;

		case IP_PORTRANGE:
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				break;

			INP_LOCK(inp);
			switch (optval) {
			case IP_PORTRANGE_DEFAULT:
				inp->inp_flags &= ~(INP_LOWPORT);
				inp->inp_flags &= ~(INP_HIGHPORT);
				break;

			case IP_PORTRANGE_HIGH:
				inp->inp_flags &= ~(INP_LOWPORT);
				inp->inp_flags |= INP_HIGHPORT;
				break;

			case IP_PORTRANGE_LOW:
				inp->inp_flags &= ~(INP_HIGHPORT);
				inp->inp_flags |= INP_LOWPORT;
				break;

			default:
				error = EINVAL;
				break;
			}
			INP_UNLOCK(inp);
			break;

#if defined(IPSEC) || defined(FAST_IPSEC)
		case IP_IPSEC_POLICY:
		{
			caddr_t req;
			size_t len = 0;
			int priv;
			struct mbuf *m;
			int optname;

			if ((error = soopt_getm(sopt, &m)) != 0) /* XXX */
				break;
			if ((error = soopt_mcopyin(sopt, m)) != 0) /* XXX */
				break;
			priv = (sopt->sopt_td != NULL &&
				suser(sopt->sopt_td) != 0) ? 0 : 1;
			req = mtod(m, caddr_t);
			len = m->m_len;
			optname = sopt->sopt_name;
			error = ipsec4_set_policy(inp, optname, req, len, priv);
			m_freem(m);
			break;
		}
#endif /*IPSEC*/

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case SOPT_GET:
		switch (sopt->sopt_name) {
		case IP_OPTIONS:
		case IP_RETOPTS:
			if (inp->inp_options)
				error = sooptcopyout(sopt, 
						     mtod(inp->inp_options,
							  char *),
						     inp->inp_options->m_len);
			else
				sopt->sopt_valsize = 0;
			break;

		case IP_TOS:
		case IP_TTL:
		case IP_RECVOPTS:
		case IP_RECVRETOPTS:
		case IP_RECVDSTADDR:
		case IP_RECVTTL:
		case IP_RECVIF:
		case IP_PORTRANGE:
		case IP_FAITH:
		case IP_ONESBCAST:
			switch (sopt->sopt_name) {

			case IP_TOS:
				optval = inp->inp_ip_tos;
				break;

			case IP_TTL:
				optval = inp->inp_ip_ttl;
				break;

#define	OPTBIT(bit)	(inp->inp_flags & bit ? 1 : 0)

			case IP_RECVOPTS:
				optval = OPTBIT(INP_RECVOPTS);
				break;

			case IP_RECVRETOPTS:
				optval = OPTBIT(INP_RECVRETOPTS);
				break;

			case IP_RECVDSTADDR:
				optval = OPTBIT(INP_RECVDSTADDR);
				break;

			case IP_RECVTTL:
				optval = OPTBIT(INP_RECVTTL);
				break;

			case IP_RECVIF:
				optval = OPTBIT(INP_RECVIF);
				break;

			case IP_PORTRANGE:
				if (inp->inp_flags & INP_HIGHPORT)
					optval = IP_PORTRANGE_HIGH;
				else if (inp->inp_flags & INP_LOWPORT)
					optval = IP_PORTRANGE_LOW;
				else
					optval = 0;
				break;

			case IP_FAITH:
				optval = OPTBIT(INP_FAITH);
				break;

			case IP_ONESBCAST:
				optval = OPTBIT(INP_ONESBCAST);
				break;
			}
			error = sooptcopyout(sopt, &optval, sizeof optval);
			break;

		case IP_MULTICAST_IF:
		case IP_MULTICAST_VIF:
		case IP_MULTICAST_TTL:
		case IP_MULTICAST_LOOP:
		case IP_ADD_MEMBERSHIP:
		case IP_DROP_MEMBERSHIP:
			error = ip_getmoptions(inp, sopt);
			break;

#if defined(IPSEC) || defined(FAST_IPSEC)
		case IP_IPSEC_POLICY:
		{
			caddr_t req = NULL;
			size_t len = 0;
			struct mbuf *m = NULL;
			size_t ovalsize = sopt->sopt_valsize;
			caddr_t oval = (caddr_t)sopt->sopt_val;

			if ((error = soopt_getm(sopt, &m)) != 0) /* XXX */
				break;
			if ((error = soopt_mcopyin(sopt, m)) != 0) /* XXX */
				break;

			sopt->sopt_valsize = ovalsize;
			sopt->sopt_val = oval;
			if (m != 0) {
				req = mtod(m, caddr_t);
				len = m->m_len;
			}

			error = ipsec4_get_policy(sotoinpcb(so), req, len, &m);
			if (error == 0)
				error = soopt_mcopyout(sopt, m);
			if (error == 0)
				m_freem(m);
			break;
		}
#endif /*IPSEC*/

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;
	}
	return (error);
}

/*
 * Set up IP options in pcb for insertion in output packets.
 * Store in mbuf with pointer in pcbopt, adding pseudo-option
 * with destination address if source routed.
 */
static int
ip_pcbopts(struct inpcb *inp, int optname, struct mbuf *m)
{
	register int cnt, optlen;
	register u_char *cp;
	struct mbuf **pcbopt;
	u_char opt;

	INP_LOCK_ASSERT(inp);

	pcbopt = &inp->inp_options;

	/* turn off any old options */
	if (*pcbopt)
		(void)m_free(*pcbopt);
	*pcbopt = 0;
	if (m == NULL || m->m_len == 0) {
		/*
		 * Only turning off any previous options.
		 */
		if (m != NULL)
			(void)m_free(m);
		return (0);
	}

	if (m->m_len % sizeof(int32_t))
		goto bad;
	/*
	 * IP first-hop destination address will be stored before
	 * actual options; move other options back
	 * and clear it when none present.
	 */
	if (m->m_data + m->m_len + sizeof(struct in_addr) >= &m->m_dat[MLEN])
		goto bad;
	cnt = m->m_len;
	m->m_len += sizeof(struct in_addr);
	cp = mtod(m, u_char *) + sizeof(struct in_addr);
	bcopy(mtod(m, void *), cp, (unsigned)cnt);
	bzero(mtod(m, void *), sizeof(struct in_addr));

	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[IPOPT_OPTVAL];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
			if (cnt < IPOPT_OLEN + sizeof(*cp))
				goto bad;
			optlen = cp[IPOPT_OLEN];
			if (optlen < IPOPT_OLEN + sizeof(*cp) || optlen > cnt)
				goto bad;
		}
		switch (opt) {

		default:
			break;

		case IPOPT_LSRR:
		case IPOPT_SSRR:
			/*
			 * user process specifies route as:
			 *	->A->B->C->D
			 * D must be our final destination (but we can't
			 * check that since we may not have connected yet).
			 * A is first hop destination, which doesn't appear in
			 * actual IP option, but is stored before the options.
			 */
			if (optlen < IPOPT_MINOFF - 1 + sizeof(struct in_addr))
				goto bad;
			m->m_len -= sizeof(struct in_addr);
			cnt -= sizeof(struct in_addr);
			optlen -= sizeof(struct in_addr);
			cp[IPOPT_OLEN] = optlen;
			/*
			 * Move first hop before start of options.
			 */
			bcopy((caddr_t)&cp[IPOPT_OFFSET+1], mtod(m, caddr_t),
			    sizeof(struct in_addr));
			/*
			 * Then copy rest of options back
			 * to close up the deleted entry.
			 */
			bcopy((&cp[IPOPT_OFFSET+1] + sizeof(struct in_addr)),
			    &cp[IPOPT_OFFSET+1],
			    (unsigned)cnt - (IPOPT_MINOFF - 1));
			break;
		}
	}
	if (m->m_len > MAX_IPOPTLEN + sizeof(struct in_addr))
		goto bad;
	*pcbopt = m;
	return (0);

bad:
	(void)m_free(m);
	return (EINVAL);
}

/*
 * XXX
 * The whole multicast option thing needs to be re-thought.
 * Several of these options are equally applicable to non-multicast
 * transmission, and one (IP_MULTICAST_TTL) totally duplicates a
 * standard option (IP_TTL).
 */

/*
 * following RFC1724 section 3.3, 0.0.0.0/8 is interpreted as interface index.
 */
struct ifnet *
ip_multicast_if(a, ifindexp)
	struct in_addr *a;
	int *ifindexp;
{
	int ifindex;
	struct ifnet *ifp;

	if (ifindexp)
		*ifindexp = 0;
	if (ntohl(a->s_addr) >> 24 == 0) {
		ifindex = ntohl(a->s_addr) & 0xffffff;
		if (ifindex < 0 || if_indexlim <= ifindex)
			return NULL;
		ifp = ifnet_byindex(ifindex);
		if (!ifp)
			return NULL;
		if (ifindexp)
			*ifindexp = ifindex;
	} else {
		INADDR_TO_IFP(*a, ifp);
	}
	return ifp;
}

/*
 * Set the IP multicast options in response to user setsockopt().
 */
static int
ip_setmoptions(struct inpcb *inp, struct sockopt *sopt)
{
	int error = 0;
	int i;
	struct in_addr addr;
	struct ip_mreq mreq;
	struct ifnet *ifp;
	struct ip_moptions *imo;
	int ifindex;
	int s = 0;
#ifdef IGMPV3
	struct sockaddr_storage ss_src, ss_grp;
	struct sockaddr_storage *del_ss;
	u_int16_t numsrc;
	u_int mode;
	int init;		/* indicate initial group join */
	int final;		/* indicate final group leave */
#endif

	imo = inp->inp_moptions;
	if (imo == NULL) {
		/*
		 * No multicast option buffer attached to the pcb;
		 * allocate one and initialize to default values.
		 */
		imo = (struct ip_moptions*)malloc(sizeof(*imo), M_IPMOPTS,
		    M_WAITOK);

		if (imo == NULL)
			return (ENOBUFS);
		inp->inp_moptions = imo;
		imo->imo_multicast_ifp = NULL;
		imo->imo_multicast_addr.s_addr = INADDR_ANY;
		imo->imo_multicast_vif = -1;
		imo->imo_multicast_ttl = IP_DEFAULT_MULTICAST_TTL;
		imo->imo_multicast_loop = IP_DEFAULT_MULTICAST_LOOP;
		imo->imo_num_memberships = 0;
	}

	switch (sopt->sopt_name) {
	/* store an index number for the vif you wanna use in the send */
	case IP_MULTICAST_VIF:
		if (legal_vif_num == 0) {
			error = EOPNOTSUPP;
			break;
		}
		error = sooptcopyin(sopt, &i, sizeof i, sizeof i);
		if (error)
			break;
		if (!legal_vif_num(i) && (i != -1)) {
			error = EINVAL;
			break;
		}
		imo->imo_multicast_vif = i;
		break;

	case IP_MULTICAST_IF:
		/*
		 * Select the interface for outgoing multicast packets.
		 */
		error = sooptcopyin(sopt, &addr, sizeof addr, sizeof addr);
		if (error)
			break;
		/*
		 * INADDR_ANY is used to remove a previous selection.
		 * When no interface is selected, a default one is
		 * chosen every time a multicast packet is sent.
		 */
		if (addr.s_addr == INADDR_ANY) {
			imo->imo_multicast_ifp = NULL;
			break;
		}
		/*
		 * The selected interface is identified by its local
		 * IP address.  Find the interface and confirm that
		 * it supports multicasting.
		 */
		s = splimp();
		ifp = ip_multicast_if(&addr, &ifindex);
		if (ifp == NULL || (ifp->if_flags & IFF_MULTICAST) == 0) {
			splx(s);
			error = EADDRNOTAVAIL;
			break;
		}
		imo->imo_multicast_ifp = ifp;
		if (ifindex)
			imo->imo_multicast_addr = addr;
		else
			imo->imo_multicast_addr.s_addr = INADDR_ANY;
		splx(s);
		break;

	case IP_MULTICAST_TTL:
		/*
		 * Set the IP time-to-live for outgoing multicast packets.
		 * The original multicast API required a char argument,
		 * which is inconsistent with the rest of the socket API.
		 * We allow either a char or an int.
		 */
		if (sopt->sopt_valsize == 1) {
			u_char ttl;
			error = sooptcopyin(sopt, &ttl, 1, 1);
			if (error)
				break;
			imo->imo_multicast_ttl = ttl;
		} else {
			u_int ttl;
			error = sooptcopyin(sopt, &ttl, sizeof ttl, 
					    sizeof ttl);
			if (error)
				break;
			if (ttl > 255)
				error = EINVAL;
			else
				imo->imo_multicast_ttl = ttl;
		}
		break;

	case IP_MULTICAST_LOOP:
		/*
		 * Set the loopback flag for outgoing multicast packets.
		 * Must be zero or one.  The original multicast API required a
		 * char argument, which is inconsistent with the rest
		 * of the socket API.  We allow either a char or an int.
		 */
		if (sopt->sopt_valsize == 1) {
			u_char loop;
			error = sooptcopyin(sopt, &loop, 1, 1);
			if (error)
				break;
			imo->imo_multicast_loop = !!loop;
		} else {
			u_int loop;
			error = sooptcopyin(sopt, &loop, sizeof loop,
					    sizeof loop);
			if (error)
				break;
			imo->imo_multicast_loop = !!loop;
		}
		break;

	case IP_ADD_MEMBERSHIP:
		/*
		 * Add a multicast group membership.
		 * Group must be a valid IP multicast address.
		 */
		error = sooptcopyin(sopt, &mreq, sizeof mreq, sizeof mreq);
		if (error)
			break;

		if (!IN_MULTICAST(ntohl(mreq.imr_multiaddr.s_addr))) {
			error = EINVAL;
			break;
		}
		s = splimp();
		if ((error = ip_getmopt_ifargs(sopt, &ifp,
						&mreq.imr_multiaddr,
						&mreq.imr_interface)) != 0) {
			splx(s);
			break;
		}

		/*
		 * See if the membership already exists or if all the
		 * membership slots are full.
		 */
		for (i = 0; i < imo->imo_num_memberships; ++i) {
			if (imo->imo_membership[i]->inm_ifp == ifp &&
			    imo->imo_membership[i]->inm_addr.s_addr
						== mreq.imr_multiaddr.s_addr)
				break;
		}
		if (i < imo->imo_num_memberships) {
			error = EADDRINUSE;
			splx(s);
			break;
		}
		if (i == IP_MAX_MEMBERSHIPS) {
			error = ETOOMANYREFS;
			splx(s);
			break;
		}
		/*
		 * Everything looks good; add a new record to the multicast
		 * address list for the given interface.
		 */
#ifdef IGMPV3
		/*
		 * Even this request doesn't add any source filter, create
		 * msf entry list. This is needed to indicate current msf state.
		 */
		IMO_MSF_ALLOC(imo->imo_msf[i]);
		if (error != 0) {
			splx(s);
			break;
		}
		init = 1;
		if ((imo->imo_membership[i] =
		    in_addmulti2(&mreq.imr_multiaddr, ifp, 0, NULL,
				MCAST_EXCLUDE, init, &error)) == NULL) {
			IMO_MSF_FREE(imo->imo_msf[i]);
			splx(s);
			break;
		}
#else
		if ((imo->imo_membership[i] =
		    in_addmulti(&mreq.imr_multiaddr, ifp)) == NULL) {
			error = ENOBUFS;
			splx(s);
			break;
		}
#endif
#ifdef IGMPV3
		imo->imo_msf[i]->msf_grpjoin = 1;
#endif
		++imo->imo_num_memberships;
		splx(s);
		break;

	case IP_DROP_MEMBERSHIP:
		/*
		 * Drop a multicast group membership.
		 * Group must be a valid IP multicast address.
		 */
		error = sooptcopyin(sopt, &mreq, sizeof mreq, sizeof mreq);
		if (error)
			break;

		if (!IN_MULTICAST(ntohl(mreq.imr_multiaddr.s_addr))) {
			error = EINVAL;
			break;
		}

		s = splimp();

		if ((error = ip_getmopt_ifargs(sopt, &ifp, NULL,
						&mreq.imr_interface)) != 0) {
			splx(s);
			break;
		}

		/*
		 * Find the membership in the membership array.
		 */
		for (i = 0; i < imo->imo_num_memberships; ++i) {
			if ((ifp == NULL ||
			     imo->imo_membership[i]->inm_ifp == ifp) &&
			     imo->imo_membership[i]->inm_addr.s_addr ==
			     mreq.imr_multiaddr.s_addr)
				break;
		}
		if (i == imo->imo_num_memberships) {
			error = EADDRNOTAVAIL;
			splx(s);
			break;
		}
		/*
		 * Give up the multicast address record to which the
		 * membership points if there is no member.
		 */
#ifdef IGMPV3
		error = in_getmopt_source_list(imo->imo_msf[i], &numsrc,
					   &del_ss, &mode);
		if (error != 0) {
			in_undomopt_source_addr(imo->imo_msf[i], sopt->sopt_name);
			if (del_ss != NULL)
				FREE(del_ss, M_IPMOPTS);
			splx(s);
			break;
		}

		final = 1;
		in_delmulti2(imo->imo_membership[i], numsrc, del_ss, mode,
			    final, &error);
		if (del_ss != NULL)
			FREE(del_ss, M_IPMOPTS);
		in_freemopt_source_list(imo->imo_msf[i],
					imo->imo_msf[i]->msf_head,
					imo->imo_msf[i]->msf_blkhead);
		IMO_MSF_FREE(imo->imo_msf[i]);
#else
		in_delmulti(imo->imo_membership[i]);
#endif

		/*
		 * Remove the gap in the membership array.
		 */
		for (++i; i < imo->imo_num_memberships; ++i) {
			imo->imo_membership[i-1] = imo->imo_membership[i];
			imo->imo_msf[i-1] = imo->imo_msf[i];
		}
		--imo->imo_num_memberships;
		splx(s);
		break;

#ifdef IGMPV3
	case MCAST_JOIN_GROUP:
		s = splimp();
		error = ip_getmopt_sgaddr(sopt, &ifp, &ss_grp, NULL);
		if (error != 0) {
			splx(s);
			break;
		}
		/*
		 * See if all the membership slots are full.
		 */
		for (i = 0; i < imo->imo_num_memberships; ++i) {
			if (imo->imo_membership[i]->inm_ifp == ifp &&
			    imo->imo_membership[i]->inm_addr.s_addr == SIN(&ss_grp)->sin_addr.s_addr)
				break;
		}
		if (i < imo->imo_num_memberships) {
			error = EADDRINUSE;
			splx(s);
			break;
		}
		if (i == IP_MAX_MEMBERSHIPS) {
			error = ETOOMANYREFS;
			splx(s);
			break;
		}

		/*
		 * Everything looks good; add a new record to the multicast
		 * address list for the given interface.
		 */
		IMO_MSF_ALLOC(imo->imo_msf[i]);
		if (error != 0) {
			splx(s);
			break;
		}
		init = 1;
		if ((imo->imo_membership[i] =
		    in_addmulti2(&SIN(&ss_grp)->sin_addr, ifp, 0, NULL,
		    		MCAST_EXCLUDE, init, &error)) == NULL) {
			IMO_MSF_FREE(imo->imo_msf[i]);
			splx(s);
			break;
		}

		imo->imo_msf[i]->msf_grpjoin = 1;
		++imo->imo_num_memberships;
		splx(s);
		break;

	case MCAST_LEAVE_GROUP:
		s = splimp();
		error = ip_getmopt_sgaddr(sopt, &ifp, &ss_grp, NULL);
		if (error != 0) {
			splx(s);
			break;
		}

		/*
		 * Find the membership in the membership array.
		 */
		for (i = 0; i < imo->imo_num_memberships; ++i) {
			if ((ifp == NULL ||
			     imo->imo_membership[i]->inm_ifp == ifp) &&
			     imo->imo_membership[i]->inm_addr.s_addr == SIN(&ss_grp)->sin_addr.s_addr)
				break;
		}
		if (i == imo->imo_num_memberships) {
			error = EADDRNOTAVAIL;
			splx(s);
			break;
		}

		error = in_getmopt_source_list(imo->imo_msf[i], &numsrc,
					       &del_ss, &mode);
		if (error != 0) {
			in_undomopt_source_addr(imo->imo_msf[i], sopt->sopt_name);
			if (del_ss != NULL)
				FREE(del_ss, M_IPMOPTS);
			splx(s);
			break;
		}

		final = 1;
		in_delmulti2(imo->imo_membership[i], numsrc, del_ss, mode,
			    final, &error);
		if (del_ss != NULL)
			FREE(del_ss, M_IPMOPTS);
		in_freemopt_source_list(imo->imo_msf[i],
					imo->imo_msf[i]->msf_head,
					imo->imo_msf[i]->msf_blkhead);
		IMO_MSF_FREE(imo->imo_msf[i]);

		/*
		 * Remove the gap in the membership array.
		 */
		for (++i; i < imo->imo_num_memberships; ++i) {
			imo->imo_membership[i-1] = imo->imo_membership[i];
			imo->imo_msf[i-1] = imo->imo_msf[i];
		}
		--imo->imo_num_memberships;
		splx(s);
		break;

	case IP_ADD_SOURCE_MEMBERSHIP:
	case MCAST_JOIN_SOURCE_GROUP:
		s = splimp();
		error = ip_getmopt_sgaddr(sopt, &ifp, &ss_grp, &ss_src);
		if (error != 0) {
			splx(s);
			break;
		}

		/*
		 * Find the membership in the membership array.
		 */
		for (i = 0; i < imo->imo_num_memberships; ++i) {
			if (imo->imo_membership[i]->inm_ifp == ifp &&
			    imo->imo_membership[i]->inm_addr.s_addr == SIN(&ss_grp)->sin_addr.s_addr)
				break;
		}
		if (i == IP_MAX_MEMBERSHIPS) {
			error = ETOOMANYREFS;
			splx(s);
			break;
		}
		if (i < imo->imo_num_memberships) {
			/*
			 * If Any-Source join was already requested, return
			 * EINVAL.
			 */
			if (imo->imo_msf[i]->msf_grpjoin != 0) {
				error = EINVAL;
				splx(s);
				break;
			}
			/*
			 * If there is EXCLUDE msf state, return EINVAL.
			 */
			if (imo->imo_msf[i]->msf_blknumsrc != 0) {
				error = EINVAL;
				splx(s);
				break;
			}
			/*
			 * If the implementation imposes a limit on the
			 * maximum number of sources in a source filter,
			 * ENOBUFS is generated.
			 */
			if (imo->imo_msf[i]->msf_numsrc >= igmpsomaxsrc) {
				error = ENOBUFS;
				splx(s);
				break;
			}
			init = 0;
		} else {
			IMO_MSF_ALLOC(imo->imo_msf[i]);
			if (error != 0) {
				splx(s);
				break;
			}
			init = 1;
		}

		/*
		 * Set source address to the msf.
		 * If requested source address was already in the socket list,
		 * return EADDRNOTAVAIL.
		 * If there is not enough memory, return ENOBUFS.
		 * Otherwise, 0 will be returned, which means okay.
		 */
		error = in_setmopt_source_addr(&ss_src, imo->imo_msf[i],
					       sopt->sopt_name);
		if (error != 0) {
			if (init)
				IMO_MSF_FREE(imo->imo_msf[i]);
			splx(s);
			break;
		}

		/*
		 * Everything looks good; add a new record to the multicast
		 * address list for the given interface.
		 * But if some error occurs when source list is added to
		 * the list, undo added msf list from the socket.
		 */
		numsrc = 1;
		imo->imo_membership[i] =
			in_addmulti2(&SIN(&ss_grp)->sin_addr, ifp, numsrc,
				    &ss_src, MCAST_INCLUDE, init, &error);
		if (imo->imo_membership[i] == NULL) {
			in_undomopt_source_addr(imo->imo_msf[i], sopt->sopt_name);
			splx(s);
			break;
		}
		in_cleanmopt_source_addr(imo->imo_msf[i], sopt->sopt_name);

		if (init)
			++imo->imo_num_memberships;
		splx(s);
		break;

	case IP_DROP_SOURCE_MEMBERSHIP:
	case MCAST_LEAVE_SOURCE_GROUP:
		s = splimp();
		error = ip_getmopt_sgaddr(sopt, &ifp, &ss_grp, &ss_src);
		if (error != 0) {
			splx(s);
			break;
		}

		/*
		 * Find the membership in the membership array.
		 */
		for (i = 0; i < imo->imo_num_memberships; ++i) {
			if ((ifp == NULL ||
			     imo->imo_membership[i]->inm_ifp == ifp) &&
			     imo->imo_membership[i]->inm_addr.s_addr == SIN(&ss_grp)->sin_addr.s_addr)
				break;
		}
		if (i == imo->imo_num_memberships) {
			error = EADDRNOTAVAIL;
			splx(s);
			break;
		}

		/*
		 * Remove source address from the msf.
		 * If (*,G) join or EXCLUDE join was requested previously,
		 * return EINVAL.
		 * If requested source address was not in the socket list,
		 * return EADDRNOTAVAIL.
		 * If there is not enough memory, return ENOBUFS.
		 * Otherwise, 0 will be returned, which means okay.
		 */
		if ((imo->imo_msf[i]->msf_grpjoin != 0) ||
				(imo->imo_msf[i]->msf_blknumsrc != 0)) {
			error = EINVAL;
			splx(s);
			break;
		}
		error = in_setmopt_source_addr(&ss_src, imo->imo_msf[i],
					       sopt->sopt_name);
		if (error != 0) {
			splx(s);
			break;
		}
		if (imo->imo_msf[i]->msf_numsrc == 0)
			final = 1;
		else
			final = 0;

		/*
		 * Give up the multicast address record to which the
		 * membership points.
		 */
		numsrc = 1;
		in_delmulti2(imo->imo_membership[i], numsrc, &ss_src,
				MCAST_INCLUDE, final, &error);
		if (error != 0) {
			printf("ip_setmoptions: error must be 0! panic!\n");
			in_undomopt_source_addr(imo->imo_msf[i], sopt->sopt_name);
			splx(s);
			break; /* strange... */
		}
		in_cleanmopt_source_addr(imo->imo_msf[i], sopt->sopt_name);


		/*
		 * Remove the gap in the membership array if there is no
		 * msf member.
		 */
		if (final) {
			for (++i; i < imo->imo_num_memberships; ++i) {
				imo->imo_membership[i-1]
						= imo->imo_membership[i];
				imo->imo_msf[i-1] = imo->imo_msf[i];
			}
			--imo->imo_num_memberships;
		}
		splx(s);
		break;

	case IP_BLOCK_SOURCE:
	case MCAST_BLOCK_SOURCE:
		s = splimp();
		error = ip_getmopt_sgaddr(sopt, &ifp, &ss_grp, &ss_src);
		if (error != 0) {
			splx(s);
			break;
		}

		/*
		 * Find the membership in the membership array.
		 */
		for (i = 0; i < imo->imo_num_memberships; ++i) {
			if (imo->imo_membership[i]->inm_ifp == ifp &&
			    imo->imo_membership[i]->inm_addr.s_addr == SIN(&ss_grp)->sin_addr.s_addr)
				break;
		}
		if (i == IP_MAX_MEMBERSHIPS) {
			error = ETOOMANYREFS;
			splx(s);
			break;
		}
		if (i < imo->imo_num_memberships) {
			/*
			 * If there is INCLUDE msf state, return EINVAL.
			 */
			if (imo->imo_msf[i]->msf_numsrc != 0) {
				error = EINVAL;
				splx(s);
				break;
			}
			if (imo->imo_msf[i]->msf_blknumsrc >= igmpsomaxsrc) {
				error = ENOBUFS;
				splx(s);
				break;
			}
			init = 0;
		} else {
			IMO_MSF_ALLOC(imo->imo_msf[i]);
			if (error != 0) {
				splx(s);
				break;
			}
			init = 1;
		}

		/*
		 * Set source address to the msf.
		 * If requested source address was already in the socket list,
		 * return EADDRNOTAVAIL.
		 * If there is not enough memory, return ENOBUFS.
		 * Otherwise, 0 will be returned, which means okay.
		 */
		error = in_setmopt_source_addr(&ss_src, imo->imo_msf[i],
					       sopt->sopt_name);
		if (error != 0) {
			if (init)
				IMO_MSF_FREE(imo->imo_msf[i]);
			splx(s);
			break;
		}

		/*
		 * Everything looks good; add a new record to the multicast
		 * address list for the given interface.
		 * But if some error occurs when source list is added to
		 * the list, undo added msf list from the socket.
		 */
		numsrc = 1;
		if (imo->imo_msf[i]->msf_grpjoin == 0) {
			/* IN{NULL}/EX{non NULL} -> EX{non NULL} */
			imo->imo_membership[i] =
				in_addmulti2(&SIN(&ss_grp)->sin_addr, ifp,
					    numsrc, &ss_src, MCAST_EXCLUDE,
					    init, &error);
			if (imo->imo_membership[i] == NULL) {
				in_undomopt_source_addr
					(imo->imo_msf[i], sopt->sopt_name);
				splx(s);
				break;
			}
		} else {
			/* EX{NULL} -> EX{non NULL} */
			imo->imo_membership[i] =
				in_modmulti2(&SIN(&ss_grp)->sin_addr,
					    ifp, numsrc, &ss_src, MCAST_EXCLUDE,
					    0, NULL, MCAST_EXCLUDE, init,
					    imo->imo_msf[i]->msf_grpjoin, &error);
			if (imo->imo_membership[i] == NULL) {
				in_undomopt_source_addr
					(imo->imo_msf[i], sopt->sopt_name);
				splx(s);
				break;
			}
			imo->imo_msf[i]->msf_grpjoin = 0;
		}
		in_cleanmopt_source_addr(imo->imo_msf[i], sopt->sopt_name);

		if (init)
			++imo->imo_num_memberships;
		splx(s);
		break;

	case IP_UNBLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
		s = splimp();
		error = ip_getmopt_sgaddr(sopt, &ifp, &ss_grp, &ss_src);
		if (error != 0) {
			splx(s);
			break;
		}

		/*
		 * Find the membership in the membership array.
		 */
		for (i = 0; i < imo->imo_num_memberships; ++i) {
			if ((ifp == NULL ||
			     imo->imo_membership[i]->inm_ifp == ifp) &&
			     imo->imo_membership[i]->inm_addr.s_addr == SIN(&ss_grp)->sin_addr.s_addr)
				break;
		}
		if (i == imo->imo_num_memberships) {
			error = EADDRNOTAVAIL;
			splx(s);
			break;
		}

		/*
		 * Remove source address from the msf.
		 * If (*,G) join or INCLUDE join was requested previously,
		 * return EINVAL.
		 * If requested source address was not in the socket list,
		 * return EADDRNOTAVAIL.
		 * If there is not enough memory, return ENOBUFS.
		 * Otherwise, 0 will be returned, which means okay.
		 */
		if ((imo->imo_msf[i]->msf_grpjoin != 0) ||
				(imo->imo_msf[i]->msf_numsrc != 0)) {
			error = EINVAL;
			splx(s);
			break;
		}
		error = in_setmopt_source_addr(&ss_src, imo->imo_msf[i],
					       sopt->sopt_name);
		if (error != 0) {
			splx(s);
			break;
		}
		if (imo->imo_msf[i]->msf_blknumsrc == 0)
			final = 1;
		else
			final = 0;

		/*
		 * Give up the multicast address record to which the
		 * membership points.
		 */
		numsrc = 1;
		in_delmulti2(imo->imo_membership[i], numsrc, &ss_src,
				MCAST_EXCLUDE, final, &error);
		if (error != 0) {
			printf("ip_setmoptions: error must be 0! panic!\n");
			in_undomopt_source_addr(imo->imo_msf[i], sopt->sopt_name);
			splx(s);
			break; /* strange... */
		}
		in_cleanmopt_source_addr(imo->imo_msf[i], sopt->sopt_name);

		/*
		 * Remove the gap in the membership array if there is no
		 * msf member.
		 */
		if (final) {
			for (++i; i < imo->imo_num_memberships; ++i) {
				imo->imo_membership[i-1]
						= imo->imo_membership[i];
				imo->imo_msf[i-1] = imo->imo_msf[i];
			}
			--imo->imo_num_memberships;
		}
		splx(s);
		break;
#endif /* IGMPV3 */

	default:
		error = EOPNOTSUPP;
		break;
	}

	/*
	 * If all options have default values, no need to keep the mbuf.
	 */
	if (imo->imo_multicast_ifp == NULL &&
	    imo->imo_multicast_vif == -1 &&
	    imo->imo_multicast_ttl == IP_DEFAULT_MULTICAST_TTL &&
	    imo->imo_multicast_loop == IP_DEFAULT_MULTICAST_LOOP &&
	    imo->imo_num_memberships == 0) {
		free(inp->inp_moptions, M_IPMOPTS);
		inp->inp_moptions = NULL;
	}

	return (error);
}

/*
 * Return the IP multicast options in response to user getsockopt().
 */
static int
ip_getmoptions(struct inpcb *inp, struct sockopt *sopt)
{
	struct ip_moptions *imo;
	struct in_addr addr;
	struct in_ifaddr *ia;
	int error, optval;
	u_char coptval;

	INP_LOCK(inp);
	imo = inp->inp_moptions;

	error = 0;
	switch (sopt->sopt_name) {
	case IP_MULTICAST_VIF: 
		if (imo != NULL)
			optval = imo->imo_multicast_vif;
		else
			optval = -1;
		INP_UNLOCK(inp);
		error = sooptcopyout(sopt, &optval, sizeof optval);
		break;

	case IP_MULTICAST_IF:
		if (imo == NULL || imo->imo_multicast_ifp == NULL)
			addr.s_addr = INADDR_ANY;
		else if (imo->imo_multicast_addr.s_addr) {
			/* return the value user has set */
			addr = imo->imo_multicast_addr;
		} else {
			IFP_TO_IA(imo->imo_multicast_ifp, ia);
			addr.s_addr = (ia == NULL) ? INADDR_ANY
				: IA_SIN(ia)->sin_addr.s_addr;
		}
		INP_UNLOCK(inp);
		error = sooptcopyout(sopt, &addr, sizeof addr);
		break;

	case IP_MULTICAST_TTL:
		if (imo == 0)
			optval = coptval = IP_DEFAULT_MULTICAST_TTL;
		else
			optval = coptval = imo->imo_multicast_ttl;
		INP_UNLOCK(inp);
		if (sopt->sopt_valsize == 1)
			error = sooptcopyout(sopt, &coptval, 1);
		else
			error = sooptcopyout(sopt, &optval, sizeof optval);
		break;

	case IP_MULTICAST_LOOP:
		if (imo == 0)
			optval = coptval = IP_DEFAULT_MULTICAST_LOOP;
		else
			optval = coptval = imo->imo_multicast_loop;
		INP_UNLOCK(inp);
		if (sopt->sopt_valsize == 1)
			error = sooptcopyout(sopt, &coptval, 1);
		else
			error = sooptcopyout(sopt, &optval, sizeof optval);
		break;

	default:
		INP_UNLOCK(inp);
		error = ENOPROTOOPT;
		break;
	}
	INP_UNLOCK_ASSERT(inp);

	return (error);
}

/*
 * Discard the IP multicast options.
 */
void
ip_freemoptions(imo)
	register struct ip_moptions *imo;
{
	register int i;
#ifdef IGMPV3
	struct sockaddr_storage *del_ss;
	u_int16_t numsrc;
	u_int mode;
	int final;
	int error;
#endif

	if (imo != NULL) {
		for (i = 0; i < imo->imo_num_memberships; ++i) {
#ifdef IGMPV3
			error = in_getmopt_source_list(imo->imo_msf[i], &numsrc,
						       &del_ss, &mode);
			if (error != 0) {
				/* XXX strange... panic or skip ? */
				/*
				in_undomopt_source_addr(imo->imo_msf[i],
							sopt->sopt_name);
				 */
				if (del_ss != NULL)
					FREE(del_ss, M_IPMOPTS);
				printf("ip_freemoptions: error must be 0! panic!\n");
				continue; /* XXX */
			}

			final = 1;
			in_delmulti2(imo->imo_membership[i], numsrc, del_ss,
				    mode, final, &error);
			if (del_ss != NULL)
				FREE(del_ss, M_IPMOPTS);
			in_freemopt_source_list(imo->imo_msf[i],
						imo->imo_msf[i]->msf_head,
						imo->imo_msf[i]->msf_blkhead);
			IMO_MSF_FREE(imo->imo_msf[i]);
#else
			in_delmulti(imo->imo_membership[i]);
#endif
		}
		free(imo, M_IPMOPTS);
	}
}

/*
 * Routine called from ip_output() to loop back a copy of an IP multicast
 * packet to the input queue of a specified interface.  Note that this
 * calls the output routine of the loopback "driver", but with an interface
 * pointer that might NOT be a loopback interface -- evil, but easier than
 * replicating that code here.
 */
static void
ip_mloopback(ifp, m, dst, hlen)
	struct ifnet *ifp;
	register struct mbuf *m;
	register struct sockaddr_in *dst;
	int hlen;
{
	register struct ip *ip;
	struct mbuf *copym;

	copym = m_copy(m, 0, M_COPYALL);
	if (copym != NULL && (copym->m_flags & M_EXT || copym->m_len < hlen))
		copym = m_pullup(copym, hlen);
	if (copym != NULL) {
		/* If needed, compute the checksum and mark it as valid. */
		if (copym->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
			in_delayed_cksum(copym);
			copym->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
			copym->m_pkthdr.csum_flags |=
			    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
			copym->m_pkthdr.csum_data = 0xffff;
		}
		/*
		 * We don't bother to fragment if the IP length is greater
		 * than the interface's MTU.  Can this possibly matter?
		 */
		ip = mtod(copym, struct ip *);
		ip->ip_len = htons(ip->ip_len);
		ip->ip_off = htons(ip->ip_off);
		ip->ip_sum = 0;
		ip->ip_sum = in_cksum(copym, hlen);
		/*
		 * NB:
		 * It's not clear whether there are any lingering
		 * reentrancy problems in other areas which might
		 * be exposed by using ip_input directly (in
		 * particular, everything which modifies the packet
		 * in-place).  Yet another option is using the
		 * protosw directly to deliver the looped back
		 * packet.  For the moment, we'll err on the side
		 * of safety by using if_simloop().
		 */
#if 1 /* XXX */
		if (dst->sin_family != AF_INET) {
			printf("ip_mloopback: bad address family %d\n",
						dst->sin_family);
			dst->sin_family = AF_INET;
		}
#endif

#ifdef notdef
		copym->m_pkthdr.rcvif = ifp;
		ip_input(copym);
#else
		if_simloop(ifp, copym, dst->sin_family, 0);
#endif
	}
}

static int
ip_getmopt_ifargs(sopt, ifp, ia_grp, ia_ifa)
	struct sockopt *sopt;
	struct ifnet **ifp;
	struct in_addr *ia_grp, *ia_ifa;
{
	struct route ro;
	struct sockaddr_in *dst;
	int error = 0;
	int optname = sopt->sopt_name;

	switch (optname) {
	case IP_ADD_MEMBERSHIP:
#ifdef IGMPV3
	case IP_ADD_SOURCE_MEMBERSHIP:
	case IP_BLOCK_SOURCE:
#endif
		/*
		 * If no interface address was provided, use the interface of
		 * the route to the given multicast address.
		 */
		if (ia_ifa->s_addr == INADDR_ANY) {
			bzero((caddr_t)&ro, sizeof(ro));
			ro.ro_rt = NULL;
			dst = satosin(&ro.ro_dst);
			dst->sin_len = sizeof(struct sockaddr_in);
			dst->sin_family = AF_INET;
			dst->sin_addr = *ia_grp;
			rtalloc_ign(&ro, RTF_CLONING);
			if (ro.ro_rt == NULL) {
				error = EADDRNOTAVAIL;
				return error;
			}
			*ifp = ro.ro_rt->rt_ifp;
			RTFREE(ro.ro_rt);
		} else {
			*ifp = ip_multicast_if(ia_ifa, NULL);
		}
		/*
		 * See if we found an interface, and confirm that it supports
		 * multicast.
		 */
		if (*ifp == NULL || ((*ifp)->if_flags & IFF_MULTICAST) == 0)
			error = EADDRNOTAVAIL;
		break;

	case IP_DROP_MEMBERSHIP:
#ifdef IGMPV3
	case IP_DROP_SOURCE_MEMBERSHIP:
	case IP_UNBLOCK_SOURCE:
#endif
		/*
		 * If an interface address was specified, get a pointer
		 * to its ifnet structure.
		 */
		if (ia_ifa->s_addr == INADDR_ANY)
			*ifp = NULL;
		else {
			*ifp = ip_multicast_if(ia_ifa, NULL);
			if (*ifp == NULL) {
				error = EADDRNOTAVAIL;
				break;
			}
		}
		break;
	}
	return error;
}

#ifdef IGMPV3
static int
in_getmopt_ifargs(sopt, ifp, ia_grp, index)
	struct sockopt *sopt;
	struct ifnet **ifp;
	struct in_addr *ia_grp;
	u_int32_t index;
{
	struct route ro;
	struct sockaddr_in *dst;
	int error = 0;
	int optname = sopt->sopt_name;

	/*
	 * If the interface is specified, validate it.
	 */
	if (index < 0 || if_indexlim <= index || !ifnet_byindex(index))
		return ENXIO;	/* XXX EINVAL? */

	switch (optname) {
	case MCAST_JOIN_GROUP:
	case MCAST_BLOCK_SOURCE:
	case MCAST_JOIN_SOURCE_GROUP:
		/*
		 * If no interface was explicitly specified, choose an
		 * appropriate one according to the given multicast address.
		 */
		if (index == 0) {
			bzero((caddr_t)&ro, sizeof(ro));
			ro.ro_rt = NULL;
			dst = satosin(&ro.ro_dst);
			dst->sin_len = sizeof(struct sockaddr_in);
			dst->sin_family = AF_INET;
			dst->sin_addr = *ia_grp;
			rtalloc((struct route *)&ro);
			if (ro.ro_rt == NULL) {
				error = EADDRNOTAVAIL;
				break;
			}
			*ifp = ro.ro_rt->rt_ifp;
			rtfree(ro.ro_rt);
		} else
#if defined(__FreeBSD__) && __FreeBSD__ >= 5
			*ifp = ifnet_byindex(index);
#else
			*ifp = ifindex2ifnet[index];
#endif

		if (*ifp == NULL || ((*ifp)->if_flags & IFF_MULTICAST) == 0) {
#ifdef IGMPV3_DEBUG
			printf("invalid interface (#%d) specified", index);
#endif
			error = EINVAL;
		}

		break;

	case MCAST_LEAVE_GROUP:
	case MCAST_UNBLOCK_SOURCE:
	case MCAST_LEAVE_SOURCE_GROUP:
		/*
		 * If an interface address was specified, get a pointer
		 * to its ifnet structure.
		 */
		if (index == 0)
			*ifp = NULL;
		else {
#if defined(__FreeBSD__) && __FreeBSD__ >= 5
			*ifp = ifnet_byindex(index);
#else
			*ifp = ifindex2ifnet[index];
#endif
			if (*ifp == NULL) {
				error = EADDRNOTAVAIL;
				break;
			}
		}
		break;
	}
	return error;
}

static int
ip_getmopt_sgaddr(sopt, ifp, ss_grp, ss_src)
	struct sockopt *sopt;
	struct ifnet **ifp;
	struct sockaddr_storage *ss_grp;
	struct sockaddr_storage *ss_src;
{
	struct sockaddr_in *sin_src, *sin_grp;
	int error = 0;
	int optname = sopt->sopt_name;

	switch (optname) {
	case MCAST_JOIN_GROUP:
	case MCAST_LEAVE_GROUP:
	    {
		struct group_req greq;

		error = sooptcopyin(sopt, &greq, sizeof greq, sizeof greq);
		if (error)
			break;
		if (greq.gr_group.ss_family != AF_INET) {
			error = EPFNOSUPPORT;
			break;
		}

		sin_grp = SIN(ss_grp);
		sin_grp->sin_addr = SIN(&greq.gr_group)->sin_addr;
		sin_grp->sin_len = sizeof(*sin_grp);
		sin_grp->sin_family = AF_INET;

		if (!IN_MULTICAST(ntohl(sin_grp->sin_addr.s_addr))) {
			error = EINVAL;
			break;
		}

		/*
		 * Get a pointer to the ifnet structure.
		 */
		error = in_getmopt_ifargs(sopt, ifp, &sin_grp->sin_addr,
					  greq.gr_interface);

		break;
	    }

	case IP_BLOCK_SOURCE:
	case IP_UNBLOCK_SOURCE:
	case IP_ADD_SOURCE_MEMBERSHIP:
	case IP_DROP_SOURCE_MEMBERSHIP:
	    {
		struct ip_mreq_source mreqsrc;
		struct sockaddr_in sin_ifa;

		error = sooptcopyin(sopt, &mreqsrc, sizeof mreqsrc, sizeof mreqsrc);
		if (error)
			break;

		sin_src = SIN(ss_src);
		sin_src->sin_addr = mreqsrc.imr_sourceaddr;
		sin_src->sin_len = sizeof(*sin_src);
		sin_src->sin_family = AF_INET;
		sin_grp = SIN(ss_grp);
		sin_grp->sin_addr = mreqsrc.imr_multiaddr;
		sin_grp->sin_len = sizeof(*sin_grp);
		sin_grp->sin_family = AF_INET;
		sin_ifa.sin_addr = mreqsrc.imr_interface;

		/*
		 * Group must be a valid IP multicast address.
		 */
		if (!IN_MULTICAST(ntohl(sin_grp->sin_addr.s_addr)) ||
		    !is_igmp_target(&sin_grp->sin_addr)) {
			error = EINVAL;
			break;
		}
		/*
		 * If no source address was provided or was class-d, bad
		 * class, return error.
		 */
		if ((ntohl(sin_src->sin_addr.s_addr) & IN_CLASSA_NET) == 0) {
			error = EINVAL;
			break;
		}
		if (IN_MULTICAST(ntohl(sin_src->sin_addr.s_addr)) ||
		    IN_BADCLASS(ntohl(sin_src->sin_addr.s_addr))) {
			error = EINVAL;
			break;
		}

		/*
		 * Get a pointer to the ifnet structure.
		 */
		error = ip_getmopt_ifargs(sopt, ifp, &sin_grp->sin_addr,
					  &sin_ifa.sin_addr);

		break;
	    }

	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
	case MCAST_JOIN_SOURCE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
	    {
		struct group_source_req gsreq;

		error = sooptcopyin(sopt, &gsreq, sizeof gsreq, sizeof gsreq);
		if (error)
			break;
		if ((gsreq.gsr_group.ss_family != AF_INET) ||
				(gsreq.gsr_source.ss_family != AF_INET)) {
			error = EPFNOSUPPORT;
			break;
		}

		sin_src = SIN(ss_src);
		sin_src->sin_addr = SIN(&gsreq.gsr_source)->sin_addr;
		sin_src->sin_len = sizeof(*sin_src);
		sin_src->sin_family = AF_INET;
		sin_grp = SIN(ss_grp);
		sin_grp->sin_addr = SIN(&gsreq.gsr_group)->sin_addr;
		sin_grp->sin_len = sizeof(*sin_grp);
		sin_grp->sin_family = AF_INET;

		if (!IN_MULTICAST(ntohl(sin_grp->sin_addr.s_addr)) ||
		    !is_igmp_target(&sin_grp->sin_addr)) {
#ifdef IGMPV3_DEBUG
			printf("invalid group %s specified\n", inet_ntoa(sin_grp->sin_addr));
#endif
			error = EINVAL;
			break;
		}
		if ((ntohl(sin_src->sin_addr.s_addr) & IN_CLASSA_NET) == 0) {
			error = EINVAL;
			break;
		}
		if (IN_MULTICAST(ntohl(sin_src->sin_addr.s_addr)) ||
		    IN_BADCLASS(ntohl(sin_src->sin_addr.s_addr))) {
#ifdef IGMPV3_DEBUG
			printf("invalid source %s specified\n", inet_ntoa(sin_src->sin_addr));
#endif
			error = EINVAL;
			break;
		}

		error = in_getmopt_ifargs(sopt, ifp, &sin_grp->sin_addr,
					  gsreq.gsr_interface);

		break;
	    }
	}

	return error;
}
#endif /* IGMPV3 */
