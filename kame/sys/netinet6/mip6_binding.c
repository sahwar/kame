/*	$KAME: mip6_binding.c,v 1.102 2002/05/14 13:31:34 keiichi Exp $	*/

/*
 * Copyright (C) 2001 WIDE Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999, 2000 and 2001 Ericsson Radio Systems AB
 * All rights reserved.
 *
 * Authors: Conny Larsson <Conny.Larsson@era.ericsson.se>
 *          Mattias Pettersson <Mattias.Pettersson@era.ericsson.se>
 *
 */

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
#include "opt_ipsec.h"
#include "opt_inet6.h"
#include "opt_mip6.h"
#endif
#ifdef __NetBSD__
#include "opt_ipsec.h"
#endif

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/syslog.h>

#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
#include <sys/callout.h>
#elif defined(__OpenBSD__)
#include <sys/timeout.h>
#endif

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/net_osdep.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet/icmp6.h>

#if defined(IPSEC) && !defined(__OpenBSD__)
#include <netinet6/ipsec.h>
#include <netinet6/ah.h>
#include <netkey/key.h>
#include <netkey/keydb.h>
#endif /* IPSEC && !__OpenBSD__ */

#include <netinet/ip_encap.h>

#include <net/if_hif.h>

#include <netinet6/mip6_var.h>
#include <netinet6/mip6.h>

extern struct protosw mip6_tunnel_protosw;
extern struct mip6_prefix_list mip6_prefix_list;

struct mip6_bc_list mip6_bc_list;
LIST_HEAD(, mip6_timeout) mip6_bc_timeout_list;

#ifndef MIP6_BC_HASH_SIZE
#define MIP6_BC_HASH_SIZE 35			/* XXX */
#endif
#define MIP6_IN6ADDR_HASH(addr)					\
	((addr)->s6_addr32[0] ^ (addr)->s6_addr32[1] ^		\
	 (addr)->s6_addr32[2] ^ (addr)->s6_addr32[3])
#define MIP6_BC_HASH_ID(addr) (MIP6_IN6ADDR_HASH(addr) % MIP6_BC_HASH_SIZE)
struct mip6_bc *mip6_bc_hash[MIP6_BC_HASH_SIZE];

#ifdef __NetBSD__
struct callout mip6_bu_ch = CALLOUT_INITIALIZER;
struct callout mip6_bc_ch = CALLOUT_INITIALIZER;
#elif (defined(__FreeBSD__) && __FreeBSD__ >= 3)
struct callout mip6_bu_ch;
struct callout mip6_bc_ch;
#elif defined(__OpenBSD__)
struct timeout mip6_bu_ch;
struct timeout mip6_bc_ch;
#endif
static int mip6_bu_count = 0;
static int mip6_bc_count = 0;

/* binding update functions. */
static int mip6_bu_send_bu __P((struct mip6_bu *));
static void mip6_bu_timeout __P((void *));
static void mip6_bu_starttimer __P((void));
static void mip6_bu_stoptimer __P((void));

/* binding cache functions. */
static struct mip6_bc *mip6_bc_create 
    __P((struct sockaddr_in6 *, struct sockaddr_in6 *, struct sockaddr_in6 *,
	 u_int8_t, u_int16_t, u_int32_t, struct ifnet *));
static int mip6_bc_list_insert __P((struct mip6_bc_list *, struct mip6_bc *));
static int mip6_bc_proxy_control __P((struct sockaddr_in6 *,
				      struct sockaddr_in6 *,
				      int));
static void mip6_bc_timeout __P((void *));
static void mip6_bc_starttimer __P((void));
static void mip6_bc_stoptimer __P((void));
static int mip6_bc_encapcheck __P((const struct mbuf *, int, int, void *));

#ifdef MIP6_CALLOUTTEST
static struct mip6_timeout_entry *mip6_timeoutentry_insert __P((time_t, caddr_t));
static void mip6_bc_timeoutentry_remove __P((struct mip6_timeout_entry *));
#endif /* MIP6_CALLOUTTEST */

static int mip6_dad_start __P((struct mip6_bc *, struct in6_addr *));
static int mip6_dad_stop __P((struct mip6_bc *));
static int mip6_bdt_delete __P((struct sockaddr_in6 *));
static int mip6_are_ifid_equal __P((struct in6_addr *, struct in6_addr *,
				    u_int8_t));

#ifdef MIP6_DEBUG
void mip6_bu_print __P((struct mip6_bu *));
#endif /* MIP6_DEBUG */

static const struct sockaddr_in6 sin6_any = {
	sizeof(struct sockaddr_in6),	/* sin6_len */
	AF_INET6,			/* sin6_family */
	0,				/* sin6_port */
	0,				/* sin6_flowinfo */
	{{{0, 0, 0, 0}}},		/* sin6_addr */
	0				/* sin6_scope_id */
};

/*
 * binding update management functions.
 */
void
mip6_bu_init()
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3) 
        callout_init(&mip6_bu_ch);
#endif
}

/*
 * find the binding update entry specified by the destination (peer)
 * address and optionally the home address of the mobile node.  if the
 * home address is not specified, the first binding update entry found
 * that matches the specified destination address will be returned.
 */
struct mip6_bu *
mip6_bu_list_find_withpaddr(bu_list, paddr, haddr)
	struct mip6_bu_list *bu_list;
	struct sockaddr_in6 *paddr;
	struct sockaddr_in6 *haddr;
{
	struct mip6_bu *mbu;

	/* sanity check. */
	if (paddr == NULL)
		return (NULL);

	for (mbu = LIST_FIRST(bu_list); mbu;
	     mbu = LIST_NEXT(mbu, mbu_entry)) {
		if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_paddr, paddr)
		    && ((haddr != NULL)
			? SA6_ARE_ADDR_EQUAL(&mbu->mbu_haddr, haddr)
			: 1))
			break;
	}
	return (mbu);
}

struct mip6_bu *
mip6_bu_list_find_home_registration(bu_list, haddr)
     struct mip6_bu_list *bu_list;
     struct sockaddr_in6 *haddr;
{
	struct mip6_bu *mbu;

	for (mbu = LIST_FIRST(bu_list); mbu;
	     mbu = LIST_NEXT(mbu, mbu_entry)) {
		if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_haddr, haddr) &&
		    (mbu->mbu_flags & IP6MU_HOME) != 0)
			break;
	}
	return (mbu);
}

struct mip6_bu *
mip6_bu_create(paddr, mpfx, coa, flags, sc)
	const struct sockaddr_in6 *paddr;
	struct mip6_prefix *mpfx;
	struct sockaddr_in6 *coa;
	u_int16_t flags;
	struct hif_softc *sc;
{
	struct mip6_bu *mbu;
	u_int32_t coa_lifetime;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

	MALLOC(mbu, struct mip6_bu *, sizeof(struct mip6_bu),
	       M_TEMP, M_NOWAIT);
	if (mbu == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: memory allocation failed.\n",
			 __FILE__, __LINE__));
		return (NULL);
	}

	coa_lifetime = mip6_coa_get_lifetime(&coa->sin6_addr);

	bzero(mbu, sizeof(*mbu));
	mbu->mbu_paddr = *paddr;
	mbu->mbu_haddr = mpfx->mpfx_haddr;
	if (sc->hif_location == HIF_LOCATION_HOME) {
		/* un-registration. */
		mbu->mbu_coa = mpfx->mpfx_haddr;
		mbu->mbu_reg_state = MIP6_BU_REG_STATE_DEREGWAITACK;
	} else {
		/* registration. */
		mbu->mbu_coa = *coa;
		mbu->mbu_reg_state = MIP6_BU_REG_STATE_REGWAITACK;
	}
	if (coa_lifetime < mpfx->mpfx_pltime) {
		mbu->mbu_lifetime = coa_lifetime;
	} else {
		mbu->mbu_lifetime = mpfx->mpfx_pltime;
	}
	mbu->mbu_expire = time_second + mbu->mbu_lifetime;
	/* sanity check for overflow */
	if (mbu->mbu_expire < time_second)
		mbu->mbu_expire = 0x7fffffff;
	mbu->mbu_refresh = mbu->mbu_lifetime;
	mbu->mbu_refexpire = time_second + mbu->mbu_refresh;
	/* sanity check for overflow */
	if (mbu->mbu_refexpire < time_second)
		mbu->mbu_refexpire = 0x7fffffff;
	mbu->mbu_acktimeout = MIP6_BA_INITIAL_TIMEOUT;
	mbu->mbu_ackexpire = time_second + mbu->mbu_acktimeout;
	mbu->mbu_flags = flags;
	mbu->mbu_hif = sc;
	/* *mbu->mbu_encap = NULL; */

	return (mbu);
}

int
mip6_home_registration(sc)
	struct hif_softc *sc;
{
	struct hif_subnet *hs;
	struct mip6_subnet *ms;
	struct mip6_subnet_prefix *mspfx;
	struct mip6_prefix *mpfx;
	struct mip6_bu *mbu;

	if (TAILQ_FIRST(&sc->hif_hs_list_home) == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: no home subnet\n",
			 __FILE__, __LINE__));
		return (EINVAL);
	}
	for (hs = TAILQ_FIRST(&sc->hif_hs_list_home);
	     hs;
	     hs = TAILQ_NEXT(hs, hs_entry)) {
		if ((ms = hs->hs_ms) == NULL) {
			/* must not happen. */
			return (EINVAL);
		}
		for (mspfx = TAILQ_FIRST(&ms->ms_mspfx_list);
		     mspfx;
		     mspfx = TAILQ_NEXT(mspfx, mspfx_entry)) {
			if ((mpfx = mspfx->mspfx_mpfx) == NULL) {
				/* must not happen. */
				return (EINVAL);
			}
			for (mbu = LIST_FIRST(&sc->hif_bu_list);
			     mbu;
			     mbu = LIST_NEXT(mbu, mbu_entry)) {
				if ((mbu->mbu_flags & IP6MU_HOME) == 0)
					continue;
				if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_haddr,
						       &mpfx->mpfx_haddr))
					break;
			}
			if (mbu == NULL) {
				/* not exist */
				const struct sockaddr_in6 *haaddr;
				struct mip6_subnet_ha *msha;
				struct mip6_ha *mha;

				if (sc->hif_location == HIF_LOCATION_HOME) {
					/*
					 * we are home and we have no
					 * binding update entry for
					 * home registration.  this
					 * will happen when either of
					 * the following two cases
					 * happens.
					 *
					 * 1. enabling MN function at
					 * home subnet.
					 *
					 * 2. returning home with
					 * expired home registration.
					 *
					 * in either case, we should
					 * do nothing.
					 */
					continue;
				}

				/*
				 * no home registration found.  create
				 * a new binding update entry.
				 */

				/* pick the preferable HA from the list. */
				msha = mip6_subnet_ha_list_find_preferable(
					&ms->ms_msha_list);
				if (msha == NULL) {
					/*
					 * if no HA is found, try to
					 * find a HA using Dynamic
					 * Home Agent Discovery.
					 */
					mip6log((LOG_INFO,
						 "%s:%d: "
						 "no home agent.  start ha discovery.\n",
						 __FILE__, __LINE__));
					mip6_icmp6_ha_discov_req_output(sc);
					haaddr = &sin6_any;
				} else {
					if ((mha = msha->msha_mha) == NULL) {
						/* must not happen. */
						return (EINVAL);
					}
					haaddr = &mha->mha_gaddr;
				}

				mbu = mip6_bu_create(haaddr, mpfx, &hif_coa,
						     IP6MU_ACK|IP6MU_HOME|IP6MU_DAD|IP6MU_SINGLE,
						     sc);
				if (mbu == NULL)
					return (ENOMEM);
				/*
				 * for the first registration to the
				 * home agent, the ack timeout value
				 * should be (retrans * dadtransmits)
				 * * 1.5.
				 */
				/*
				 * XXX: TODO: KAME has different dad
				 * retrans values for each interfaces.
				 * which retrans value should be
				 * selected ?
				 */

				mip6_bu_list_insert(&sc->hif_bu_list, mbu);
			} else {
				/* exist */
				int32_t coa_lifetime, prefix_lifetime;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
				long time_second = time.tv_sec;
#endif

				/* 
				 * a binding update entry exists.
				 * update information.
				 */

				/* update coa. */
				if (sc->hif_location == HIF_LOCATION_HOME) {
					/* un-registration. */
					mbu->mbu_coa = mbu->mbu_haddr;
					mbu->mbu_reg_state
					    = MIP6_BU_REG_STATE_DEREGWAITACK;
				} else {
					/* registration. */
					mbu->mbu_coa = hif_coa;
					mbu->mbu_reg_state
						= MIP6_BU_REG_STATE_REGWAITACK;
				}

				/* update lifetime. */
				coa_lifetime = mip6_coa_get_lifetime(&mbu->mbu_coa.sin6_addr);
				prefix_lifetime = mip6_subnet_prefix_list_get_minimum_lifetime(&ms->ms_mspfx_list);
				if (coa_lifetime < prefix_lifetime) {
					mbu->mbu_lifetime = coa_lifetime;
				} else {
					mbu->mbu_lifetime = prefix_lifetime;
				}
				mbu->mbu_expire
					= time_second + mbu->mbu_lifetime;
				/* sanity check for overflow */
				if (mbu->mbu_expire < time_second)
					mbu->mbu_expire = 0x7fffffff;
				mbu->mbu_refresh = mbu->mbu_lifetime;
				mbu->mbu_refexpire
					= time_second + mbu->mbu_refresh;
				/* sanity check for overflow */
				if (mbu->mbu_refexpire < time_second)
					mbu->mbu_refexpire = 0x7fffffff;
				mbu->mbu_acktimeout = MIP6_BA_INITIAL_TIMEOUT;
				mbu->mbu_ackexpire
					= time_second + mbu->mbu_acktimeout;
				/* mbu->mbu_flags |= IP6MU_DAD ;*/
			}
			mbu->mbu_state |= (MIP6_BU_STATE_WAITACK|MIP6_BU_STATE_WAITSENT);
		}
	}

	return (0);
}

int
mip6_bu_list_notify_binding_change(sc)
	struct hif_softc *sc;
{
	struct mip6_prefix *mpfx;
	struct mip6_bu *mbu, *mbu_next;
	int32_t coa_lifetime;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

	/* for each BU entry, update COA and make them about to send. */
	for (mbu = LIST_FIRST(&sc->hif_bu_list);
	     mbu;
	     mbu = mbu_next) {
		mbu_next = LIST_NEXT(mbu, mbu_entry);

		if (mbu->mbu_flags & IP6MU_HOME) {
			/* this is a BU for our home agent */
			/*
			 * XXX
			 * must send bu with ack flag to a previous ar.
			 */
			continue;
		}
		mbu->mbu_coa = hif_coa;
		coa_lifetime = mip6_coa_get_lifetime(&mbu->mbu_coa.sin6_addr);
		mpfx = mip6_prefix_list_find_withhaddr(&mip6_prefix_list,
						       &mbu->mbu_haddr);
		if (mpfx == NULL) {
			mip6log((LOG_NOTICE,
				 "%s:%d: expired prefix (%s).\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&mbu->mbu_haddr.sin6_addr)));
			mip6_bu_list_remove(&sc->hif_bu_list, mbu);
			continue;
		}
		if (coa_lifetime < mpfx->mpfx_pltime) {
			mbu->mbu_lifetime = coa_lifetime;
		} else {
			mbu->mbu_lifetime = mpfx->mpfx_pltime;
		}
		mbu->mbu_expire = time_second + mbu->mbu_lifetime;
		/* sanity check for overflow */
		if (mbu->mbu_expire < time_second)
			mbu->mbu_expire = 0x7fffffff;
		mbu->mbu_refresh = mbu->mbu_lifetime;
		mbu->mbu_refexpire = time_second + mbu->mbu_refresh;
		/* sanity check for overflow */
		if (mbu->mbu_refexpire < time_second)
			mbu->mbu_refexpire = 0x7fffffff;
		mbu->mbu_state |= MIP6_BU_STATE_WAITSENT;
		mip6_bu_send_bu(mbu);
	}

	return (0);
}

static void
mip6_bu_starttimer()
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_reset(&mip6_bu_ch,
		      MIP6_BU_TIMEOUT_INTERVAL * hz,
		      mip6_bu_timeout, NULL);
#elif defined(__OpenBSD__)
	timeout_set(&mip6_bu_ch, mip6_bu_timeout, NULL);
	timeout_add(&mip6_bu_ch,
		    MIP6_BU_TIMEOUT_INTERVAL * hz);
#else
	timeout(mip6_bu_timeout, (void *)0,
		MIP6_BU_TIMEOUT_INTERVAL * hz);
#endif
}

static void
mip6_bu_stoptimer()
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_stop(&mip6_bu_ch);
#elif defined(__OpenBSD__)
	timeout_del(&mip6_bu_ch);
#else
	untimeout(mip6_bu_timeout, (void *)0);
#endif
}

static void
mip6_bu_timeout(arg)
	void *arg;
{
	int s;
	struct hif_softc *sc;
	int error = 0;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

#ifdef __NetBSD__
	s = splsoftnet();
#else
	s = splnet();
#endif
	mip6_bu_starttimer();

	for (sc = TAILQ_FIRST(&hif_softc_list); sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		struct mip6_bu *mbu, *mbu_entry;

		for (mbu = LIST_FIRST(&sc->hif_bu_list);
		     mbu != NULL; mbu = mbu_entry) {
			mbu_entry = LIST_NEXT(mbu, mbu_entry);

			/* check expiration */
			if (mbu->mbu_expire < time_second) {
				if ((mbu->mbu_flags & IP6MU_HOME) != 0) {
					/*
					 * the binding update entry for
					 * the home registration
					 * should not be removed.
					 */
					mip6_home_registration(mbu->mbu_hif);
				} else {
					error = mip6_bu_list_remove(
						&sc->hif_bu_list, mbu);
					if (error) {
						mip6log((LOG_ERR,
							 "%s:%d: can't remove a binding update entry (0x%p)\n",
							 __FILE__, __LINE__,
							 mbu));
						/* continue anyway... */
					}
					continue;
				}
			}

			/* check if the peer supports BU */
			if ((mbu->mbu_state & MIP6_BU_STATE_BUNOTSUPP) != 0)
				continue;

#ifdef MIP6_ALLOW_COA_FALLBACK
			/* check if the peer supports HA destopt */
			if ((mbu->mbu_state & MIP6_BU_STATE_MIP6NOTSUPP) != 0)
				continue;
#endif

			/* check ack status */
			if ((mbu->mbu_flags & IP6MU_ACK)
			    && ((mbu->mbu_state & MIP6_BU_STATE_WAITACK) != 0)
			    && (mbu->mbu_ackexpire < time_second)) {
				mbu->mbu_acktimeout *= 2;
				if (mbu->mbu_acktimeout > MIP6_BA_MAX_TIMEOUT)
					mbu->mbu_acktimeout
						= MIP6_BA_MAX_TIMEOUT;
				mbu->mbu_ackexpire
					= time_second + mbu->mbu_acktimeout;
				mbu->mbu_state |= MIP6_BU_STATE_WAITSENT;
			}

			/* refresh check */
			if (mbu->mbu_refexpire < time_second) {
				/* refresh binding */
				mbu->mbu_refexpire
					= time_second + mbu->mbu_refresh;
				/* sanity check for overflow */
				if (mbu->mbu_refexpire < time_second)
					mbu->mbu_refexpire = 0x7fffffff;
				if (mbu->mbu_flags & IP6MU_ACK) {
					mbu->mbu_acktimeout
						= MIP6_BA_INITIAL_TIMEOUT;
					mbu->mbu_ackexpire
						= time_second
						+ mbu->mbu_acktimeout;
					mbu->mbu_state
						|= MIP6_BU_STATE_WAITACK;
				}
				mbu->mbu_state |= MIP6_BU_STATE_WAITSENT;
			}

			/* send pending BUs */
			if ((mbu->mbu_state & MIP6_BU_STATE_WAITSENT) != 0) {
				if (mip6_bu_send_bu(mbu)) {
					mip6log((LOG_ERR,
						 "%s:%d: "
						 "sending a binding update "
						 "from %s(%s) to %s failed.\n",
						 __FILE__, __LINE__,
						 ip6_sprintf(&mbu->mbu_haddr.sin6_addr),
						 ip6_sprintf(&mbu->mbu_coa.sin6_addr),
						 ip6_sprintf(&mbu->mbu_paddr.sin6_addr)));
				}
			}
		}
	}

	splx(s);
}

/*
 * Some BUs are sent with IPv6 datagram.  But when we have no traffic to
 * the BU destination, we may have some BUs left in the BU list.  Push
 * them out.
 */
static int
mip6_bu_send_bu(mbu)
	struct mip6_bu *mbu;
{
	struct mbuf *m;
	int error = 0;

	if (IN6_IS_ADDR_UNSPECIFIED(&mbu->mbu_paddr.sin6_addr)) {
		if ((mbu->mbu_flags & IP6MU_HOME) != 0) {
			mip6log((LOG_INFO,
				 "%s:%d: "
				 "no home agent.  start ha discovery.\n",
				 __FILE__, __LINE__));
			mip6_icmp6_ha_discov_req_output(mbu->mbu_hif);
		}

		/* return immediately.  we need WAITSENT flag being set. */
		return (0);
	}

	/* create ipv6 header to send a binding update destination opt */
	m = mip6_create_ip6hdr(&mbu->mbu_haddr, &mbu->mbu_paddr,
			       IPPROTO_NONE, 0);
	if (m == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: memory allocation failed.\n",
			 __FILE__, __LINE__));
		error = ENOBUFS;
		goto send_bu_end;
	}

	/* output a null packet. */
	if (!ip6_setpktaddrs(m, &mbu->mbu_haddr, &mbu->mbu_paddr)) {
		m_freem(m);
		goto send_bu_end;
	}
	error = ip6_output(m, NULL, NULL, 0, NULL, NULL);
	if (error) {
		mip6log((LOG_ERR,
			 "%s:%d: ip6_output returns error (%d) "
			 "when sending NULL packet to send BU.\n",
			 __FILE__, __LINE__,
			 error));
		goto send_bu_end;
	}

 send_bu_end:
	/*
	 * XXX when we reset waitsent flag ?  is it correct to clear here ?
	 */
	/* mbu->mbu_state &= ~MIP6_BU_STATE_WAITSENT; */

	return (error);
}

int
mip6_bu_list_insert(bu_list, mbu)
	struct mip6_bu_list *bu_list;
	struct mip6_bu *mbu;
{
	LIST_INSERT_HEAD(bu_list, mbu, mbu_entry);

	if (mip6_bu_count == 0) {
		mip6log((LOG_INFO, "%s:%d: BU timer started.\n",
			__FILE__, __LINE__));
		mip6_bu_starttimer();
	}
	mip6_bu_count++;
		
	return (0);
}

int
mip6_bu_list_remove(mbu_list, mbu)
	struct mip6_bu_list *mbu_list;
	struct mip6_bu *mbu;
{
	if ((mbu_list == NULL) || (mbu == NULL)) {
		return (EINVAL);
	}

	if ((mbu->mbu_state & MIP6_BU_STATE_MIP6NOTSUPP) != 0) {
		mip6_bdt_delete(&mbu->mbu_paddr);
	}

	LIST_REMOVE(mbu, mbu_entry);
	FREE(mbu, M_TEMP);

	mip6_bu_count--;
	if (mip6_bu_count == 0) {
		mip6_bu_stoptimer();
		mip6log((LOG_INFO,
			 "%s:%d: BU timer stopped.\n",
			__FILE__, __LINE__));
	}

	return (0);
}

int
mip6_bdt_create(sc, paddr)
	struct hif_softc *sc;
	struct sockaddr_in6 *paddr;
{
	struct rt_addrinfo rti;
	struct sockaddr_in6 dst, mask;
	struct sockaddr_dl gate;
	struct rtentry *retrt;
	int error = 0;

	(void)mip6_bdt_delete(paddr);

	dst = *paddr;

	/* XXX correct? */
	bzero(&gate, sizeof(gate));
	gate.sdl_len = sizeof(gate);
	gate.sdl_family = AF_LINK;

        bzero(&mask, sizeof(mask));
        mask.sin6_len = sizeof(struct sockaddr_in6);
        mask.sin6_family = AF_INET6;
        mask.sin6_addr = in6mask128;

	bzero((caddr_t)&rti, sizeof(rti));
	rti.rti_flags = RTF_UP|RTF_HOST;
	rti.rti_info[RTAX_DST] = (struct sockaddr *)&dst;
	rti.rti_info[RTAX_GATEWAY] = (struct sockaddr *)&gate;
	rti.rti_info[RTAX_NETMASK] = (struct sockaddr *)&mask;
	rti.rti_ifp = (struct ifnet *)sc;

#ifndef SCOPEDROUTING
	dst.sin6_scope_id = 0;
	mask.sin6_scope_id = 0;
#endif	
	error = rtrequest1(RTM_ADD, &rti, &retrt);
	if (error == 0) {
		retrt->rt_refcnt--;
	}

	return (error);
}

static int
mip6_bdt_delete(paddr)
	struct sockaddr_in6 *paddr;
{
	struct rtentry *rt;
	struct sockaddr_in6 dst;
	int error = 0;

	dst = *paddr;
#ifndef SCOPEDROUTING
	dst.sin6_scope_id = 0;
#endif /* !SCOPEDROUTING */
#ifdef __FreeBSD__
	rt = rtalloc1((struct sockaddr *)&dst, 0, 0UL);
#else
	rt = rtalloc1((struct sockaddr *)&dst, 0);
#endif /* __FreeBSD__ */
	if (rt
	    && ((rt->rt_flags & RTF_HOST) != 0)
	    && (SA6_ARE_ADDR_EQUAL(&dst, (struct sockaddr_in6 *)rt_key(rt)))) {
		error = rtrequest(RTM_DELETE, rt_key(rt),
				  (struct sockaddr *)0,
				  rt_mask(rt), 0, (struct rtentry **)0);
		rt->rt_refcnt--;
	}

	return (error);
}

int
mip6_bu_list_remove_all(mbu_list)
	struct mip6_bu_list *mbu_list;
{
	struct mip6_bu *mbu, *mbu_next;
	int error = 0;

	if (mbu_list == NULL) {
		return (EINVAL);
	}

	for (mbu = LIST_FIRST(mbu_list);
	     mbu;
	     mbu = mbu_next) {
		mbu_next = LIST_NEXT(mbu, mbu_entry);

		error = mip6_bu_list_remove(mbu_list, mbu);
		if (error) {
			mip6log((LOG_ERR,
				 "%s:%d: can't remove BU.\n",
				 __FILE__, __LINE__));
			continue;
		}
	}

	return (0);
}

/*
 * validate incoming binding updates.
 */

int
mip6_process_hurbu(haddr0, coa, flags, seqno, lifetime, haaddr)
	struct sockaddr_in6 *haddr0;
	struct sockaddr_in6 *coa;
	u_int8_t flags;
	u_int16_t seqno;
	u_int32_t lifetime;
	struct sockaddr_in6 *haaddr;
{
	struct mip6_bc *mbc, *mbc_next;
	struct nd_prefix *pr;
	struct ifnet *hifp = NULL;
	int error = 0;

	/* find the home ifp of this homeaddress. */
	for(pr = nd_prefix.lh_first;
	    pr;
	    pr = pr->ndpr_next) {
		if (in6_are_prefix_equal(&haddr0->sin6_addr,
					 &pr->ndpr_prefix.sin6_addr,
					 pr->ndpr_plen)) {
			hifp = pr->ndpr_ifp; /* home ifp. */
		}
	}
	if (hifp == NULL) {
		/*
		 * the haddr0 doesn't have an online prefix.  return a
		 * binding ack with an error NOT_HOME_SUBNET.
		 */
		if (mip6_bc_send_ba(haaddr, haddr0, coa,
				    MIP6_BA_STATUS_NOT_HOME_SUBNET,
				    seqno,
				    0,
				    0)) {
			mip6log((LOG_ERR,
				 "%s:%d: sending BA to %s(%s) failed. "
				 "send it later.\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&haddr0->sin6_addr),
				 ip6_sprintf(&coa->sin6_addr)));
		}
		return (0); /* XXX is 0 OK? */
	}

	if (flags & IP6MU_SINGLE) {
		mbc = mip6_bc_list_find_withphaddr(&mip6_bc_list,
						   haddr0);
		if (mbc == NULL) {
			/* XXX NOT_HOME_AGENT */
			mip6_bc_send_ba(haaddr, haddr0, coa,
					MIP6_BA_STATUS_NOT_HOME_AGENT,
					seqno,
					0,
					0);
			return (0);
		}
		if ((mbc->mbc_state & MIP6_BC_STATE_DAD_WAIT) != 0) {
			mip6_dad_stop(mbc);
		}
		else {
			/* remove rtable for proxy ND */
			if (mip6_bc_proxy_control(haddr0, haaddr, RTM_DELETE)) {
				/* XXX UNSPECIFIED */
				return (-1);
			}

			/* remove encapsulation entry */
			if (mip6_tunnel_control(MIP6_TUNNEL_DELETE,
						mbc,
						mip6_bc_encapcheck,
						&mbc->mbc_encap)) {
				/* XXX UNSPECIFIED */
				return (-1);
			}
		}

		/* remove a BC entry. */
		error = mip6_bc_list_remove(&mip6_bc_list, mbc);
		if (error) {
			mip6log((LOG_ERR,
				 "%s:%d: can't remove BC.\n",
				 __FILE__, __LINE__));
			mip6_bc_send_ba(haaddr, haddr0, coa,
					MIP6_BA_STATUS_UNSPECIFIED,
					seqno,
					0,
					0);
			return (error);
		}
	}
	else {
		int found = 0;

		for(mbc = LIST_FIRST(&mip6_bc_list);
		    mbc;
		    mbc = mbc_next) {
			mbc_next = LIST_NEXT(mbc, mbc_entry);

			if (mbc->mbc_ifp != hifp)
				continue;

			if (!mip6_are_ifid_equal(&mbc->mbc_phaddr.sin6_addr,
						 &haddr0->sin6_addr,
						 64 /* XXX */))
				continue;

			found = 1;

			/* remove rtable for proxy ND */
			error = mip6_bc_proxy_control(&mbc->mbc_phaddr, haaddr,
						      RTM_DELETE);
			if (error) {
				mip6log((LOG_ERR,
					 "%s:%d: proxy control error (%d)\n",
					 __FILE__, __LINE__, error));
			}

			/* remove encapsulation entry */
			error = mip6_tunnel_control(MIP6_TUNNEL_DELETE,
						    mbc,
						    mip6_bc_encapcheck,
						    &mbc->mbc_encap);
			if (error) {
				/* XXX UNSPECIFIED */
				mip6log((LOG_ERR,
					 "%s:%d: tunnel control error (%d)\n",
					 __FILE__, __LINE__, error));
				return (error);
			}

			/* remove a BC entry. */
			error = mip6_bc_list_remove(&mip6_bc_list, mbc);
			if (error) {
				mip6log((LOG_ERR,
					 "%s:%d: can't remove BC.\n",
					 __FILE__, __LINE__));
				mip6_bc_send_ba(haaddr, haddr0, coa,
						MIP6_BA_STATUS_UNSPECIFIED,
						seqno,
						0,
						0);
				return (error);
			}
		}
		if (found == 0) {
			mip6_bc_send_ba(haaddr, haddr0, coa,
					MIP6_BA_STATUS_NOT_HOME_AGENT,
					seqno,
					0,
					0);
			return (0);
		}
	}

	/* return BA */
	if (mip6_bc_send_ba(haaddr, haddr0, coa,
			    MIP6_BA_STATUS_ACCEPTED,
			    seqno,
			    0,
			    0)) {
		mip6log((LOG_ERR,
			 "%s:%d: sending BA to %s(%s) failed.  send it later.\n",
			 __FILE__, __LINE__,
			 ip6_sprintf(&haddr0->sin6_addr),
			 ip6_sprintf(&coa->sin6_addr)));
	}

	return (0);
}

static int
mip6_are_ifid_equal(addr1, addr2, prefixlen)
	struct in6_addr *addr1;
	struct in6_addr *addr2;
	u_int8_t prefixlen;
{
	int bytelen, bitlen;
	u_int8_t mask;
	int i;

	bytelen = prefixlen / 8;
	bitlen = prefixlen % 8;
	mask = 0;
	for (i = 0; i < bitlen; i++) {
		mask &= (0x80 >> i);
	}

	if (bitlen) {
		if ((addr1->s6_addr8[bytelen] & ~mask)
		    != (addr2->s6_addr8[bytelen] & ~mask))
			return (0);

		if (bcmp(((caddr_t)addr1) + bytelen,
			 ((caddr_t)addr2) + bytelen,
			 16 - bytelen - 1))
			return (0);
	} else {
		if (bcmp(((caddr_t)addr1) + bytelen,
			 ((caddr_t)addr2) + bytelen,
			 16 - bytelen))
			return (0);
	}

	return (1);
}

int
mip6_process_hrbu(haddr0, coa, flags, seqno, lifetime, haaddr)
	struct sockaddr_in6 *haddr0;
	struct sockaddr_in6 *coa;
	u_int8_t flags;
	u_int16_t seqno;
	u_int32_t lifetime;
	struct sockaddr_in6 *haaddr;
{
	struct nd_prefix *pr;
	struct ifnet *hifp = NULL;
	struct sockaddr_in6 haddr;
	struct mip6_bc *mbc = NULL;
	struct mip6_bc *dad_mbc = NULL;
	struct in6_addr lladdr = in6addr_any;
	u_int32_t prlifetime, refresh;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif
	/* find the home ifp of this homeaddress. */
	for(pr = nd_prefix.lh_first;
	    pr;
	    pr = pr->ndpr_next) {
		if (in6_are_prefix_equal(&haddr0->sin6_addr,
					 &pr->ndpr_prefix.sin6_addr,
					 pr->ndpr_plen)) {
			hifp = pr->ndpr_ifp; /* home ifp. */
		}
	}
	/* XXX really stupid to loop twice? */
	prlifetime = 0xffffffff;
	for(pr = nd_prefix.lh_first;
	    pr;
	    pr = pr->ndpr_next) {
		if (pr->ndpr_ifp != hifp) {
			/* this prefix is not a home prefix. */
			continue;
		}
		/* save minimum prefix lifetime for later use. */
		if (prlifetime > pr->ndpr_vltime)
			prlifetime = pr->ndpr_vltime;
	}
	if (hifp == NULL) {
		/*
		 * the haddr0 doesn't have an online prefix.  return a
		 * binding ack with an error NOT_HOME_SUBNET.
		 */
		if (mip6_bc_send_ba(haaddr, haddr0, coa,
				    MIP6_BA_STATUS_NOT_HOME_SUBNET,
				    seqno,
				    0,
				    0)) {
			mip6log((LOG_ERR,
				 "%s:%d: sending BA to %s(%s) failed. "
				 "send it later.\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&haddr0->sin6_addr),
				 ip6_sprintf(&coa->sin6_addr)));
		}
		return (0); /* XXX is 0 OK? */
	}

	/* adjust lifetime */
	if (lifetime > prlifetime)
		lifetime = prlifetime;

	if ((flags & IP6MU_SINGLE) != 0) {
		/*
		 * if prefixlen == 0, create a binding cache exactly
		 * for the only address specified by the sender.
		 */
		mbc = mip6_bc_list_find_withphaddr(&mip6_bc_list,
						   haddr0);
		if (mbc == NULL) {
			/* create BC entry */
			mbc = mip6_bc_create(haddr0,
					     coa,
					     haaddr,
					     flags,
					     seqno,
					     lifetime,
					     hifp);
			if (mbc == NULL) {
				/* XXX STATUS_RESOUCE */
				return (-1);
			}

			if (mip6_bc_list_insert(&mip6_bc_list, mbc)) {
				/* XXX STATUS_UNSPECIFIED */
				return (-1);
			}

			if (flags & IP6MU_DAD) {
				mbc->mbc_state |= MIP6_BC_STATE_DAD_WAIT;
				dad_mbc = mbc;
			}
			else {
				/* create encapsulation entry */
				if (mip6_tunnel_control(MIP6_TUNNEL_ADD,
							mbc,
							mip6_bc_encapcheck,
							&mbc->mbc_encap)) {
					/* XXX UNSPECIFIED */
					return (-1);
				}

				/* add rtable for proxy ND */
				mip6_bc_proxy_control(haddr0, haaddr, RTM_ADD);
			}
		} else {
			/* update a BC entry. */
			mbc->mbc_pcoa = *coa;
			mbc->mbc_flags = flags;
			mbc->mbc_seqno = seqno;
			mbc->mbc_lifetime = lifetime;
			mbc->mbc_expire = time_second + mbc->mbc_lifetime;
			/* sanity check for overflow */
			if (mbc->mbc_expire < time_second)
				mbc->mbc_expire = 0x7fffffff;
			mbc->mbc_state &= ~MIP6_BC_STATE_BR_WAITSENT;

			/* modify encapsulation entry */
			if (mip6_tunnel_control(MIP6_TUNNEL_CHANGE,
						mbc,
						mip6_bc_encapcheck,
						&mbc->mbc_encap)) {
				/* XXX UNSPECIFIED */
				return (-1);
			}
		}
	}
	else {
		/*
		 * create/update binding cache entries for each
		 * address derived from all the routing prefixes on
		 * this router.
		 */
		for(pr = nd_prefix.lh_first;
		    pr;
		    pr = pr->ndpr_next) {
			if (!pr->ndpr_raf_onlink)
				continue;
			if (pr->ndpr_ifp != hifp)
				continue;

			mip6_create_addr(&haddr,
					 (const struct sockaddr_in6 *)haddr0,
					 pr);

			if (IN6_IS_ADDR_LINKLOCAL(&haddr.sin6_addr))
				lladdr = haddr.sin6_addr;

			mbc = mip6_bc_list_find_withphaddr(&mip6_bc_list,
							   &haddr);
			if (mbc == NULL) {
				/* create a BC entry. */
				mbc = mip6_bc_create(&haddr,
						     coa,
						     haaddr,
						     flags,
						     seqno,
						     lifetime,
						     hifp);
				if (mip6_bc_list_insert(&mip6_bc_list, mbc))
					return (-1);

				if ((flags & IP6MU_DAD)) {
					mbc->mbc_state |= MIP6_BC_STATE_DAD_WAIT;
					if (SA6_ARE_ADDR_EQUAL(&haddr, haddr0))
						dad_mbc = mbc;
					continue;
				}

				/* create encapsulation entry */
				/* XXX */
				mip6_tunnel_control(MIP6_TUNNEL_ADD,
						    mbc,
						    mip6_bc_encapcheck,
						    &mbc->mbc_encap);

				/* add rtable for proxy ND */
				mip6_bc_proxy_control(&haddr, haaddr, RTM_ADD);
			} else {
				/* update a BC entry. */
				mbc->mbc_pcoa = *coa;
				mbc->mbc_flags = flags;
				mbc->mbc_seqno = seqno;
				mbc->mbc_lifetime = lifetime;
				mbc->mbc_expire
					= time_second + mbc->mbc_lifetime;
				/* sanity check for overflow */
				if (mbc->mbc_expire < time_second)
					mbc->mbc_expire = 0x7fffffff;
				mbc->mbc_state &= ~MIP6_BC_STATE_BR_WAITSENT;

				if (IN6_IS_ADDR_LINKLOCAL(&haddr.sin6_addr))
					continue;

				/* modify encapsulation entry */
				/* XXX */
				mip6_tunnel_control(MIP6_TUNNEL_ADD,
						    mbc,
						    mip6_bc_encapcheck,
						    &mbc->mbc_encap);
			}
		}
	}

	if (dad_mbc) {
		/* start DAD */
		mip6_dad_start(mbc, &lladdr);
	}
	else {
		/* return BA */
		refresh = lifetime * MIP6_REFRESH_LIFETIME_RATE / 100;
		if (refresh < MIP6_REFRESH_MINLIFETIME)
			refresh = lifetime < MIP6_REFRESH_MINLIFETIME ?
				  lifetime : MIP6_REFRESH_MINLIFETIME;
		if (mip6_bc_send_ba(haaddr, haddr0, coa,
				    MIP6_BA_STATUS_ACCEPTED,
				    seqno,
				    lifetime,
				    refresh)) {
			mip6log((LOG_ERR,
				 "%s:%d: sending BA to %s(%s) failed. "
				 "send it later.\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&mbc->mbc_phaddr.sin6_addr),
				 ip6_sprintf(&mbc->mbc_pcoa.sin6_addr)));
		}
	}

	return (0);
}

static int
mip6_dad_start(mbc, lladdr)
	struct  mip6_bc *mbc;
	struct  in6_addr *lladdr;
{
	struct in6_ifaddr *ia;

	if (mbc->mbc_dad != NULL)
		return (EEXIST);

	ia = (struct in6_ifaddr *)
		malloc(sizeof(*ia), M_IFADDR, M_NOWAIT);
	if (ia == NULL)
		return (ENOBUFS);

	bzero((caddr_t)ia, sizeof(*ia));
	ia->ia_ifa.ifa_addr = (struct sockaddr *)&ia->ia_addr;
	ia->ia_addr.sin6_family = AF_INET6;
	ia->ia_addr.sin6_len = sizeof(ia->ia_addr);
	ia->ia_ifp = mbc->mbc_ifp;
	ia->ia6_flags |= IN6_IFF_TENTATIVE;
	if (!lladdr || IN6_IS_ADDR_UNSPECIFIED(lladdr)) {
		sa6_copy_addr(&mbc->mbc_phaddr, &ia->ia_addr);
#ifndef SCOPEDROUTING
		ia->ia_addr.sin6_scope_id = 0;
#endif
	} else
		ia->ia_addr.sin6_addr = *lladdr;
	IFAREF(&ia->ia_ifa);
	mbc->mbc_dad = ia;
	nd6_dad_start((struct ifaddr *)ia, NULL);

	return (0);
}

static int
mip6_dad_stop(mbc)
	struct  mip6_bc *mbc;
{
	struct in6_ifaddr *ia = (struct in6_ifaddr *)mbc->mbc_dad;

	if (ia == NULL)
		return (ENOENT);
	nd6_dad_stop((struct ifaddr *)ia);
	free(ia, M_IFADDR);
	mbc->mbc_dad = NULL;
	return (0);
}

int
mip6_dad_success(ifa)
	struct ifaddr *ifa;
{
	struct  mip6_bc *mbc, *prim = NULL;

	for(mbc = LIST_FIRST(&mip6_bc_list);
	    mbc;
	    mbc = LIST_NEXT(mbc, mbc_entry)) {
		if (mbc->mbc_dad == ifa)
			break;
	}
	if (!mbc)
		return (ENOENT);

	prim = mbc;
	for(mbc = LIST_FIRST(&mip6_bc_list);
	    mbc;
	    mbc = LIST_NEXT(mbc, mbc_entry)) {
		if (mbc->mbc_ifp != prim->mbc_ifp ||
		    (mbc->mbc_state & MIP6_BC_STATE_DAD_WAIT) == 0) 
			continue;
		if (!mip6_are_ifid_equal(&mbc->mbc_phaddr.sin6_addr,
					 &prim->mbc_phaddr.sin6_addr,
					 64))
			continue;
		mbc->mbc_state &= ~MIP6_BC_STATE_DAD_WAIT;

		/* create encapsulation entry */
		mip6_tunnel_control(MIP6_TUNNEL_ADD,
				    mbc,
				    mip6_bc_encapcheck,
				    &mbc->mbc_encap);

		/* add rtable for proxy ND */
		mip6_bc_proxy_control(&mbc->mbc_phaddr, &mbc->mbc_addr, RTM_ADD);
	}
	free(ifa, M_IFADDR);
	prim->mbc_dad = NULL;
	/* return BA */
	if (mip6_bc_send_ba(&prim->mbc_addr, &prim->mbc_phaddr,
			    &prim->mbc_pcoa,
			    MIP6_BA_STATUS_ACCEPTED,
			    prim->mbc_seqno,
			    prim->mbc_lifetime,
			    prim->mbc_lifetime / 2 /* XXX */)) {
		mip6log((LOG_ERR,
			 "%s:%d: sending BA to %s(%s) failed. "
			 "send it later.\n",
			 __FILE__, __LINE__,
			 ip6_sprintf(&prim->mbc_phaddr.sin6_addr),
			 ip6_sprintf(&prim->mbc_pcoa.sin6_addr)));
	}

	return (0);
}

int
mip6_dad_duplicated(ifa)
	struct ifaddr *ifa;
{
	struct  mip6_bc *mbc, *mbc_next, *prim = NULL;

	for(mbc = LIST_FIRST(&mip6_bc_list);
	    mbc;
	    mbc = LIST_NEXT(mbc, mbc_entry)) {
		if (mbc->mbc_dad == ifa)
			break;
	}
	if (!mbc)
		return (ENOENT);

	prim = mbc;
	free(ifa, M_IFADDR);
	prim->mbc_dad = NULL;
	for(mbc = LIST_FIRST(&mip6_bc_list);
	    mbc;
	    mbc = mbc_next) {
		mbc_next = LIST_NEXT(mbc, mbc_entry);
		if (prim == mbc)
			continue;
		if (mbc->mbc_ifp != prim->mbc_ifp ||
		    (mbc->mbc_state & MIP6_BC_STATE_DAD_WAIT) == 0) 
			continue;
		if (!mip6_are_ifid_equal(&mbc->mbc_phaddr.sin6_addr,
					 &prim->mbc_phaddr.sin6_addr,
					 64))
			continue;
		mbc->mbc_state &= ~MIP6_BC_STATE_DAD_WAIT;
		mip6_bc_list_remove(&mip6_bc_list, mbc);
	}
	/* return BA */
	mip6_bc_send_ba(&prim->mbc_addr, &prim->mbc_phaddr,
			&prim->mbc_pcoa,
			MIP6_BA_STATUS_DAD_FAILED,
			prim->mbc_seqno,
			0, 0);
	mip6_bc_list_remove(&mip6_bc_list, prim);

	return (0);
}

struct ifaddr *
mip6_dad_find(taddr, ifp)
struct in6_addr *taddr;
struct ifnet *ifp;
{
	struct mip6_bc *mbc;
	struct in6_ifaddr *ia;

	for(mbc = LIST_FIRST(&mip6_bc_list);
	    mbc;
	    mbc = LIST_NEXT(mbc, mbc_entry)) {
		if ((mbc->mbc_state & MIP6_BC_STATE_DAD_WAIT) == 0)
			continue;
		if (mbc->mbc_ifp != ifp || mbc->mbc_dad == NULL)
			continue;
		ia = (struct in6_ifaddr *)mbc->mbc_dad;
		if (IN6_ARE_ADDR_EQUAL(&ia->ia_addr.sin6_addr, taddr))
			return ((struct ifaddr *)ia);
	}

	return (NULL);
}


static int
mip6_bc_proxy_control(target, local, cmd)
	struct sockaddr_in6 *target;
	struct sockaddr_in6 *local;
	int cmd;
{
	struct sockaddr_in6 mask; /* = {sizeof(mask), AF_INET6 } */
	struct sockaddr_in6 sa6;
	struct sockaddr_dl *sdl;
        struct rtentry *rt, *nrt;
	struct ifaddr *ifa;
	struct ifnet *ifp;
	int flags, error = 0;

	switch (cmd) {
	case RTM_DELETE:
		bzero(&sa6, sizeof(struct sockaddr_in6));
		sa6 = *target;
#ifndef SCOPEDROUTING
		sa6.sin6_scope_id = 0;
#endif

#ifdef __FreeBSD__
		rt = rtalloc1((struct sockaddr *)&sa6, 1, 0UL);
#else /* __FreeBSD__ */
		rt = rtalloc1((struct sockaddr *)&sa6, 1);
#endif /* __FreeBSD__ */
		if (rt == NULL)
			return EHOSTUNREACH;

		error = rtrequest(RTM_DELETE, rt_key(rt), (struct sockaddr *)0,
				  rt_mask(rt), 0, (struct rtentry **)0);
		rt->rt_refcnt--;
		rt = NULL;
		if (error) {
			mip6log((LOG_ERR,
				 "%s:%d: RTM_DELETE for %s returned "
				 "error = %d\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&target->sin6_addr), error));
		}

		break;

	case RTM_ADD:
		bzero(&sa6, sizeof(struct sockaddr_in6));
		sa6 = *target;
#ifndef SCOPEDROUTING
		sa6.sin6_scope_id = 0;
#endif

#ifdef __FreeBSD__
		rt = rtalloc1((struct sockaddr *)&sa6, 0, 0UL);
#else /* __FreeBSD__ */
		rt = rtalloc1((struct sockaddr *)&sa6, 0);
#endif /* __FreeBSD__ */
		if (rt) {
			if ((rt->rt_flags & RTF_ANNOUNCE) != 0 &&
			    rt->rt_gateway->sa_family == AF_LINK) {
				mip6log((LOG_NOTICE,
					 "%s:%d: RTM_ADD: we are already proxy for %s\n",
					 __FILE__, __LINE__,
					 ip6_sprintf(&target->sin6_addr)));
				return (EEXIST);
			}
			if ((rt->rt_flags & RTF_LLINFO) != 0) {
				/* nd cache exist */
				rtrequest(RTM_DELETE, rt_key(rt),
					  (struct sockaddr *)0,
					  rt_mask(rt), 0, (struct rtentry **)0);
			} else {
				/* XXX Path MTU entry? */
				mip6log((LOG_ERR,
					 "%s:%d: entry exist %s: rt_flags=0x%x\n",
					 __FILE__, __LINE__,
					 ip6_sprintf(&target->sin6_addr),
					 rt->rt_flags));
			}
			rt->rt_refcnt--;
		}

		/* Create sa6 */
		bzero(&sa6, sizeof(sa6));
		sa6 = *local;
#ifndef SCOPEDROUTING
		sa6.sin6_scope_id = 0;
#endif

		ifa = ifa_ifwithaddr((struct sockaddr *)&sa6);
		if (ifa == NULL)
			return (EINVAL);
		sa6  = *target;
#ifndef SCOPEDROUTING
		sa6.sin6_scope_id = 0;
#endif

		/* Create sdl */
		ifp = ifa->ifa_ifp;

#if defined(__bsdi__) || (defined(__FreeBSD__) && __FreeBSD__ < 3)
		for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next)
#else
		for (ifa = ifp->if_addrlist.tqh_first; ifa;
		     ifa = ifa->ifa_list.tqe_next)
#endif
			if (ifa->ifa_addr->sa_family == AF_LINK) break;

		if (!ifa)
			return EINVAL;

		MALLOC(sdl, struct sockaddr_dl *, ifa->ifa_addr->sa_len,
		       M_IFMADDR, M_NOWAIT);
		if (sdl == NULL)
			return EINVAL;
		bcopy((struct sockaddr_dl *)ifa->ifa_addr, sdl, ifa->ifa_addr->sa_len);

		/* Create mask */
		bzero(&mask, sizeof(mask));
		mask.sin6_family = AF_INET6;
		mask.sin6_len = sizeof(mask);

		in6_prefixlen2mask(&mask.sin6_addr, 128);
		flags = (RTF_STATIC | RTF_ANNOUNCE | RTA_NETMASK);

		error = rtrequest(RTM_ADD, (struct sockaddr *)&sa6,
				  (struct sockaddr *)sdl,
				  (struct sockaddr *)&mask, flags, &nrt);

		if (error == 0) {
			/* Avoid expiration */
			if (nrt) {
				nrt->rt_rmx.rmx_expire = 0;
				nrt->rt_genmask = NULL;
				nrt->rt_refcnt--;
			} else
				error = EINVAL;
		} else {
			mip6log((LOG_ERR,
				 "%s:%d: RTM_ADD for %s returned "
				 "error = %d\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&target->sin6_addr), error));
		}

		{
			/* very XXX */
			struct sockaddr_in6 daddr, taddr;

			bzero(&daddr, sizeof(daddr));
			daddr.sin6_family = AF_INET6;
			daddr.sin6_len = sizeof(daddr);
			daddr.sin6_addr = in6addr_linklocal_allnodes;
			bzero(&taddr, sizeof(taddr));
			taddr.sin6_family = AF_INET6;
			taddr.sin6_len = sizeof(taddr);
			sa6_copy_addr(target, &taddr);
			if (in6_addr2zoneid(ifp, &daddr.sin6_addr,
					    &daddr.sin6_scope_id)) {
				/* XXX: should not happen */
				mip6log((LOG_ERR,
					 "%s:%d: in6_addr2zoneid failed\n",
					 __FILE__, __LINE__));
				error = EIO; /* XXX */
			}
			if (error == 0) {
				error = in6_embedscope(&daddr.sin6_addr,
						       &daddr);
			}
			if (error == 0) {
				nd6_na_output(ifp, &daddr, &taddr,
					      ND_NA_FLAG_OVERRIDE,
					      1,
					      (struct sockaddr *)sdl);
			}
		}

		free(sdl, M_IFMADDR);

		break;

	default:
		mip6log((LOG_ERR,
			 "%s:%d: we only support RTM_ADD/DELETE operation.\n",
			 __FILE__, __LINE__));
		error = -1;
		break;
	}

	return (error);
}

/*
 * check whether this address needs dad or not
 */
int
mip6_ifa_need_dad(ia)
	struct in6_ifaddr *ia;
{
	struct sockaddr_in6 sin6;
	struct hif_softc *sc = NULL;
	struct mip6_bu *mbu = NULL;
	int need_dad = 0;

	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = ia->ia_addr.sin6_addr;
	if (in6_addr2zoneid(ia->ia_ifp, &sin6.sin6_addr, &sin6.sin6_scope_id))
		return (EINVAL);

	for (sc = TAILQ_FIRST(&hif_softc_list); sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		mbu = mip6_bu_list_find_home_registration(
			&sc->hif_bu_list,
			&sin6);
		if (mbu != NULL)
			break;
	}
#ifdef MIP6_DEBUG
if(mbu)	mip6_bu_print(mbu);
#endif
	if ((mbu == NULL) || (mbu->mbu_lifetime <= 0))
		need_dad = 1;

	return (need_dad);
}

#ifdef MIP6_DEBUG
void
mip6_bu_print(mbu)
	struct mip6_bu *mbu;
{
	mip6log((LOG_INFO,
		 "paddr      %s\n"
		 "haddr      %s\n"
		 "coa        %s\n"
		 "lifetime   %lu\n"
		 "remain     %lld\n"
		 "refresh    %lu\n"
		 "refremain  %lld\n"
		 "acktimeout %lu\n"
		 "ackremain  %lld\n"
		 "seqno      %u\n"
		 "flags      0x%x\n"
		 "state      0x%x\n"
		 "hif        0x%p\n"
		 "reg_state  %u\n",
		 ip6_sprintf(&mbu->mbu_paddr.sin6_addr),
		 ip6_sprintf(&mbu->mbu_haddr.sin6_addr),
		 ip6_sprintf(&mbu->mbu_coa.sin6_addr),
		 (u_long)mbu->mbu_lifetime,
		 (long long)mbu->mbu_expire,
		 (u_long)mbu->mbu_refresh,
		 (long long)mbu->mbu_refexpire,
		 (u_long)mbu->mbu_acktimeout,
		 (long long)mbu->mbu_ackexpire,
		 mbu->mbu_seqno,
		 mbu->mbu_flags,
		 mbu->mbu_state,
		 mbu->mbu_hif,
		 mbu->mbu_reg_state));

}
#endif /* MIP6_DEBUG */


/*
 * binding ack management functions.
 */



/*
 * binding request management functions
 */
int
mip6_validate_br(m, opt)
	struct mbuf *m;
	u_int8_t *opt;
{
	/* XXX: no need to validate. */

	return (0);
}

int
mip6_process_br(m, opt)
	struct mbuf *m;
	u_int8_t *opt;
{
	struct ip6_hdr *ip6;
	struct sockaddr_in6 *sin6src, *sin6dst;
	struct ip6_opt_binding_request *br_opt;
	struct hif_softc *sc;
	struct mip6_bu *mbu;
	struct mip6_prefix *mpfx;
	int64_t haddr_remain, coa_remain, remain;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

	ip6 = mtod(m, struct ip6_hdr *);
	if (ip6_getpktaddrs(m, &sin6src, &sin6dst))
		return (EINVAL);
	br_opt = (struct ip6_opt_binding_request *)opt;

	sc = hif_list_find_withhaddr(sin6dst);
	if (sc == NULL) {
		/* this BR is not for our home address. */
		return (0);
	}

	mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list, sin6src, sin6dst);
	if (mbu == NULL) {
		/* XXX there is no BU for this peer.  create? */
		return (0);
	}

	mpfx = mip6_prefix_list_find_withhaddr(&mip6_prefix_list,
					       &mbu->mbu_haddr);
	if (mpfx == NULL) {
		/*
		 * there are no prefixes associated to the home address.
		 */
		/* XXX */
		return (0);
	}
	haddr_remain = mpfx->mpfx_plexpire - time_second;
	coa_remain = mip6_coa_get_lifetime(&mbu->mbu_coa.sin6_addr);
	remain =  (haddr_remain < coa_remain)
		? haddr_remain : coa_remain;
	mbu->mbu_lifetime = (u_int32_t)remain;
	mbu->mbu_expire = time_second + mbu->mbu_lifetime;
	/* sanity check for overflow */
	if (mbu->mbu_expire < time_second)
		mbu->mbu_expire = 0x7fffffff;
	mbu->mbu_state |= MIP6_BU_STATE_WAITSENT;

	/*
	 * TOXO: XXX
	 *
	 * unique ideintifier suboption processing.
	 */

	return (0);
}


/*
 * binding cache management functions.
 */

void
mip6_bc_init()
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3) 
        callout_init(&mip6_bc_ch);
#endif
	bzero(&mip6_bc_hash, sizeof(mip6_bc_hash));
}

static struct mip6_bc *
mip6_bc_create(phaddr, pcoa, addr, flags, seqno, lifetime, ifp)
	struct sockaddr_in6 *phaddr;
	struct sockaddr_in6 *pcoa;
	struct sockaddr_in6 *addr;
	u_int8_t flags;
	u_int16_t seqno;
	u_int32_t lifetime;
	struct ifnet *ifp;
{
	struct mip6_bc *mbc;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

	MALLOC(mbc, struct mip6_bc *, sizeof(struct mip6_bc),
	       M_TEMP, M_NOWAIT);
	if (mbc == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: memory allocation failed.\n",
			 __FILE__, __LINE__));
		return (NULL);
	}
	bzero(mbc, sizeof(*mbc));

	mbc->mbc_phaddr = *phaddr;
	mbc->mbc_pcoa = *pcoa;
	mbc->mbc_addr = *addr;
	mbc->mbc_flags = flags;
	mbc->mbc_seqno = seqno;
	mbc->mbc_lifetime = lifetime;
	mbc->mbc_expire = time_second + mbc->mbc_lifetime;
	/* sanity check for overflow */
	if (mbc->mbc_expire < time_second)
		mbc->mbc_expire = 0x7fffffff;
	mbc->mbc_state = 0;
	mbc->mbc_ifp = ifp;

	return (mbc);
}

#ifdef MIP6_CALLOUTTEST
static struct mip6_timeout_entry *
mip6_timeoutentry_insert(expire, content)
	time_t expire;
	caddr_t content;
{
	struct mip6_timeout *mto, *newmto = NULL;
	struct mip6_timeout_entry *mtoe = NULL;

/* XXX splxxx ?? */
	/* Search appropriate positon to insert a timeout slot */
	for (mto = LIST_FIRST(&mip6_bc_timeout_list); mto && LIST_NEXT(mto, mto_entry);
	     mto = LIST_NEXT(mto, mto_entry)) {
		if (mto->mto_expire >= expire)
			break;
	}

	/* Allocate a new timeout slot when a sutable slot is not found */
	if (mto == NULL || mto->mto_expire != expire) {
		MALLOC(newmto, struct mip6_timeout *,
		       sizeof(struct mip6_timeout), M_TEMP, M_NOWAIT);
		if (!newmto) {
			mip6log((LOG_ERR,
				 "%s:%d: memory allocation failed.\n",
				 __FILE__, __LINE__));
			return (NULL);
		}
		bzero(newmto, sizeof(struct mip6_timeout));
		if (!mto) {
			LIST_INSERT_HEAD(&mip6_bc_timeout_list, newmto, mto_entry);
		} else if (mto->mto_expire > expire) {
			LIST_INSERT_BEFORE(mto, newmto, mto_entry);
		} else {
			LIST_INSERT_AFTER(mto, newmto, mto_entry);
		}
		mto = newmto;
	}

	MALLOC(mtoe, struct mip6_timeout_entry *,
	       sizeof(struct mip6_timeout_entry), M_TEMP, M_NOWAIT);
	if (!mtoe) {
		if (newmto)
			FREE(newmto, M_TEMP);
		mip6log((LOG_ERR,
			 "%s:%d: memory allocation failed.\n",
			 __FILE__, __LINE__));
		return (NULL);
	}
	bzero(mtoe, sizeof(struct mip6_timeout_entry));
	mtoe->mtoe_timeout = mto;
	mtoe->mtoe_ptr = content;
	LIST_INSERT_HEAD(&mto->mto_timelist, mtoe, mtoe_entry);

	return (mtoe);
}

static void
mip6_bc_timeoutentry_remove(mtoe)
	struct mip6_timeout_entry *mtoe;
{
/* XXX splxxx ?? */
	LIST_REMOVE(mtoe, mtoe_entry);
	if (LIST_EMPTY(&mtoe->mtoe_timeout->mto_timelist)) {
		LIST_REMOVE(mtoe->mtoe_timeout, mto_entry);
		FREE(mtoe->mtoe_timeout, M_TEMP);
	}
	FREE(mtoe, M_TEMP);

	mip6log((LOG_INFO, "%s:%d: Removed timeout entry.\n",
		__FILE__, __LINE__));
}
#endif /* MIP6_CALLOUTTEST */

static int
mip6_bc_list_insert(mbc_list, mbc)
	struct mip6_bc_list *mbc_list;
	struct mip6_bc *mbc;
{
	int id = MIP6_BC_HASH_ID(&mbc->mbc_phaddr.sin6_addr);

	if (mip6_bc_hash[id] != NULL) {
		LIST_INSERT_BEFORE(mbc, mip6_bc_hash[id], mbc_entry);
	} else {
		LIST_INSERT_HEAD(mbc_list, mbc, mbc_entry);
	}
	mip6_bc_hash[id] = mbc;

#ifdef MIP6_CALLOUTTEST
	mbc->mbc_timeout = mip6_timeoutentry_insert(mbc->mbc_expire, (caddr_t)mbc); /* For BC expiration */
	mip6_timeoutentry_insert(mbc->mbc_expire - mbc->mbc_lifetime / 4, (caddr_t)mbc); /* For BR */
#endif

	if (mip6_bc_count == 0) {
		mip6log((LOG_INFO, "%s:%d: BC timer started.\n",
			__FILE__, __LINE__));
		mip6_bc_starttimer();
	}
	mip6_bc_count++;

	return (0);
}

int
mip6_bc_list_remove(mbc_list, mbc)
	struct mip6_bc_list *mbc_list;
	struct mip6_bc *mbc;
{
	int error = 0;
	int id;

	if ((mbc_list == NULL) || (mbc == NULL)) {
		return (EINVAL);
	}

	id = MIP6_BC_HASH_ID(&mbc->mbc_phaddr.sin6_addr);
	if (mip6_bc_hash[id] == mbc) {
		struct mip6_bc *next = LIST_NEXT(mbc, mbc_entry);
		if (next != NULL &&
		    id == MIP6_BC_HASH_ID(&next->mbc_phaddr.sin6_addr)) {
			mip6_bc_hash[id] = next;
		} else {
			mip6_bc_hash[id] = NULL;
		}
	}
#ifdef MIP6_CALLOUTTEST
	if (mbc->mbc_timeout) {
		mip6_bc_timeoutentry_remove(mbc->mbc_timeout);
	}
#endif
	LIST_REMOVE(mbc, mbc_entry);
	if (mbc->mbc_flags & IP6MU_HOME) {
		error = mip6_bc_proxy_control(&mbc->mbc_phaddr, NULL,
					      RTM_DELETE);
		if (error) {
			mip6log((LOG_ERR,
				 "%s:%d: can't delete a proxy ND entry "
				 "for %s.\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&mbc->mbc_phaddr.sin6_addr)));
		}
	}
	FREE(mbc, M_TEMP);

	mip6_bc_count--;
	if (mip6_bc_count == 0) {
		mip6_bc_stoptimer();
		mip6log((LOG_INFO, "%s:%d: BC timer stopped.\n",
			__FILE__, __LINE__));
	}

	return (0);
}

struct mip6_bc *
mip6_bc_list_find_withphaddr(mbc_list, haddr)
	struct mip6_bc_list *mbc_list;
	struct sockaddr_in6 *haddr;
{
	struct mip6_bc *mbc;
	int id = MIP6_BC_HASH_ID(&haddr->sin6_addr);

	for (mbc = mip6_bc_hash[id]; mbc;
	     mbc = LIST_NEXT(mbc, mbc_entry)) {
		if (MIP6_BC_HASH_ID(&mbc->mbc_phaddr.sin6_addr) != id)
			return NULL;
		if (SA6_ARE_ADDR_EQUAL(&mbc->mbc_phaddr, haddr))
			break;
	}

	return (mbc);
}

struct mip6_bc *
mip6_bc_list_find_withcoa(mbc_list, pcoa)
	struct mip6_bc_list *mbc_list;
	struct sockaddr_in6 *pcoa;
{
	struct mip6_bc *mbc;

	for (mbc = LIST_FIRST(mbc_list); mbc;
	     mbc = LIST_NEXT(mbc, mbc_entry)) {
		if (SA6_ARE_ADDR_EQUAL(&mbc->mbc_pcoa, pcoa))
			break;
	}

	return (mbc);
}

static void
mip6_bc_timeout(dummy)
	void *dummy;
{
	int s;
#ifdef MIP6_CALLOUTTEST
	struct mip6_bc *mbc;
	struct mip6_timeout *mto, *mto_next;
	struct mip6_timeout_entry *mtoe, *mtoe_next;
#else
	struct mip6_bc *mbc, *mbc_next;
#endif /* MIP6_CALLOUTTEST */
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

#ifdef __NetBSD__
	s = splsoftnet();
#else
	s = splnet();
#endif

#ifdef MIP6_CALLOUTTEST
	for (mto = LIST_FIRST(&mip6_bc_timeout_list); mto; mto = mto_next) {
		mto_next = LIST_NEXT(mto, mto_entry);

		if (mto->mto_expire > time_second)
			break;
		for (mtoe = LIST_FIRST(&mto->mto_timelist); mtoe; mtoe = mtoe_next) {
			mtoe_next = LIST_NEXT(mtoe, mtoe_entry);
			mbc = (struct mip6_bc *)mtoe->mtoe_ptr;
			if (mbc->mbc_expire < time_second) {
				mip6_bc_list_remove(&mip6_bc_list, (struct mip6_bc *)mtoe->mtoe_ptr);
			} else if ((mbc->mbc_expire - time_second)
			    < (mbc->mbc_lifetime / 4)) { /* XXX */
				mbc->mbc_state |= MIP6_BC_STATE_BR_WAITSENT;
				mip6_bc_timeoutentry_remove(mtoe);
			}
		}
	}
#else
	for (mbc = LIST_FIRST(&mip6_bc_list); mbc; mbc = mbc_next) {
		mbc_next = LIST_NEXT(mbc, mbc_entry);

		/* expiration check */
		if (mbc->mbc_expire < time_second) {
			mip6_bc_list_remove(&mip6_bc_list, mbc);
		}

		/* XXX send BR if BR_WAITSENT is remained not
		   piggybacked before */

		/* XXX set BR_WAITSENT when BC is going to expire */
		if ((mbc->mbc_expire - time_second)
		    < (mbc->mbc_lifetime / 4)) { /* XXX */
			mbc->mbc_state |= MIP6_BC_STATE_BR_WAITSENT;
		}

		/* XXX send BA if BA_WAITSENT is remained not
		   piggybacked before */
		if (mbc->mbc_state & MIP6_BC_STATE_BA_WAITSENT) {
			
		}
	}
#endif

	mip6_bc_starttimer();

	splx(s);
}

static void
mip6_bc_stoptimer(void)
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_stop(&mip6_bc_ch);
#elif defined(__OpenBSD__)
	timeout_del(&mip6_bc_ch);
#else
	untimeout(mip6_bc_timeout, (void *)0);
#endif
}

static void mip6_bc_starttimer(void)
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_reset(&mip6_bc_ch,
		      MIP6_BC_TIMEOUT_INTERVAL * hz,
		      mip6_bc_timeout, NULL);
#elif defined(__OpenBSD__)
	timeout_set(&mip6_bc_ch, mip6_bc_timeout, NULL);
	timeout_add(&mip6_bc_ch,
		    MIP6_BC_TIMEOUT_INTERVAL * hz);
#else
	timeout(mip6_bc_timeout, (void *)0,
		MIP6_BC_TIMEOUT_INTERVAL * hz);
#endif
}


/*
 * tunneling functions.
 */
int
mip6_tunnel_control(action, entry, func, ep)
	int action;
	void *entry;
	int (*func) __P((const struct mbuf *, int, int, void *));
	const struct encaptab **ep;
{
	if ((entry == NULL) || (ep == NULL)) {
		return (EINVAL);
	}

	if ((action == MIP6_TUNNEL_CHANGE) && *ep) {
		encap_detach(*ep);
	}

	switch (action) {
	case MIP6_TUNNEL_ADD:
	case MIP6_TUNNEL_CHANGE:
		*ep = encap_attach_func(AF_INET6, IPPROTO_IPV6,
					func,
					&mip6_tunnel_protosw,
					(void *)entry);
		if (*ep == NULL) {
			mip6log((LOG_ERR,
				 "%s:%d: "
				 "encap entry create failed.\n",
				 __FILE__, __LINE__));
			return (EINVAL);
		}
		break;
	}

	return (0);
}

int
mip6_bu_encapcheck(m, off, proto, arg)
	const struct mbuf *m;
	int off;
	int proto;
	void *arg;
{
	struct ip6_hdr *ip6;
	struct mip6_bu *mbu = (struct mip6_bu *)arg;
	struct hif_softc *sc;
	struct hif_subnet_list *hs_list_home, *hs_list_foreign;
	struct hif_subnet *hs;
	struct mip6_subnet *ms;
	struct mip6_subnet_prefix *mspfx;
	struct mip6_prefix *mpfx;
	struct sockaddr_in6 *encap_src, *encap_dst;
	struct sockaddr_in6 *haaddr, *myaddr, *mycoa;

	if (mbu == NULL) {
		return (0);
	}
	if ((sc = mbu->mbu_hif) == NULL) {
		return (0);
	}
	if (((hs_list_home = &sc->hif_hs_list_home) == NULL)
	    || (hs_list_foreign = &sc->hif_hs_list_foreign) == NULL) {
		return (0);
	}

	ip6 = mtod(m, struct ip6_hdr*);
	if (ip6_getpktaddrs((struct mbuf *)m, &encap_src, &encap_dst))
		return (0);

	haaddr = &mbu->mbu_paddr;
	myaddr = &mbu->mbu_haddr;
	mycoa = &mbu->mbu_coa;

	/*
	 * check wether this packet is from the correct sender (that
	 * is, our home agent) to the CoA the mobile node has
	 * registered before.
	 */
	if (!SA6_ARE_ADDR_EQUAL(encap_src, haaddr) ||
	    !SA6_ARE_ADDR_EQUAL(encap_dst, mycoa)) {
		return (0);
	}

	/*
	 * XXX: should we compare the ifid of the inner dstaddr of the
	 * incoming packet and the ifid of the mobile node's?  these
	 * check will be done in the ip6_input and later.
	 */

	/* check mn prefix */
	for (hs = TAILQ_FIRST(hs_list_home); hs;
	     hs = TAILQ_NEXT(hs, hs_entry)) {
		if ((ms = hs->hs_ms) == NULL) {
			/* must not happen. */
			continue;
		}
		for (mspfx = TAILQ_FIRST(&ms->ms_mspfx_list); mspfx;
		     mspfx = TAILQ_NEXT(mspfx, mspfx_entry)) {
			if ((mpfx = mspfx->mspfx_mpfx) == NULL)	{
				/* must not happen. */
				continue;
			}
			if (!in6_are_prefix_equal(&myaddr->sin6_addr,
						  &mpfx->mpfx_prefix.sin6_addr,
						  mpfx->mpfx_prefixlen)) {
				/* this prefix doesn't match my prefix.
				   check next. */
				continue;
			}
			goto match;
		}
	}
	for (hs = TAILQ_FIRST(hs_list_foreign); hs;
	     hs = TAILQ_NEXT(hs, hs_entry)) {
		if ((ms = hs->hs_ms) == NULL) {
			/* must not happen. */
			continue;
		}
		for (mspfx = TAILQ_FIRST(&ms->ms_mspfx_list); mspfx;
		     mspfx = TAILQ_NEXT(mspfx, mspfx_entry)) {
			if ((mpfx = mspfx->mspfx_mpfx) == NULL)	{
				/* must not happen. */
				continue;
			}
			if (!in6_are_prefix_equal(&myaddr->sin6_addr,
						  &mpfx->mpfx_prefix.sin6_addr,
						  mpfx->mpfx_prefixlen)) {
				/* this prefix doesn't match my prefix.
				   check next. */
				continue;
			}
			goto match;
		}
	}
	return (0);
 match:
	return (128);
}

static int
mip6_bc_encapcheck(m, off, proto, arg)
	const struct mbuf *m;
	int off;
	int proto;
	void *arg;
{
	struct ip6_hdr *ip6;
	struct mip6_bc *mbc = (struct mip6_bc *)arg;
	struct sockaddr_in6 *encap_src, *encap_dst;
	struct sockaddr_in6 *mnaddr;

	if (mbc == NULL) {
		return (0);
	}

	ip6 = mtod(m, struct ip6_hdr*);
	if (ip6_getpktaddrs((struct mbuf *)m, &encap_src, &encap_dst))
		return (0);

	mnaddr = &mbc->mbc_pcoa;

	/* check mn addr */
	if (!SA6_ARE_ADDR_EQUAL(encap_src, mnaddr)) {
		return (0);
	}

	/* check my addr */
	/* XXX */

	return (128);
}

/*
 ******************************************************************************
 * Function:    mip6_tunnel_input
 * Description: similar to gif_input() and in6_gif_input().
 * Ret value:	standard error codes.
 ******************************************************************************
 */
int
mip6_tunnel_input(mp, offp, proto)
	struct mbuf **mp;
	int *offp, proto;
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6;
	int s;

	ip6 = mtod(m, struct ip6_hdr *);
	m_adj(m, *offp);

	switch (proto) {
	case IPPROTO_IPV6:
	{
		struct ip6_hdr *ip6;
		if (m->m_len < sizeof(*ip6)) {
			m = m_pullup(m, sizeof(*ip6));
			if (!m)
				return (IPPROTO_DONE);
		}

		ip6 = mtod(m, struct ip6_hdr *);

		s = splimp();
		if (IF_QFULL(&ip6intrq)) {
			IF_DROP(&ip6intrq);	/* update statistics */
			splx(s);
			goto bad;
		}
		IF_ENQUEUE(&ip6intrq, m);
#if 0
		/* we don't need it as we tunnel IPv6 in IPv6 only. */
		schednetisr(NETISR_IPV6);
#endif
		splx(s);
		break;
	}
	default:
		mip6log((LOG_ERR,
			 "%s:%d: protocol %d not supported.\n",
			 __FILE__, __LINE__,
			 proto));
		goto bad;
	}

	return (IPPROTO_DONE);

 bad:
	m_freem(m);
	return (IPPROTO_DONE);
}

/*
 * encapsulate the packet from the correspondent node to the mobile
 * node that is communicating.  the encap_src is to be a home agent's
 * address and the encap_dst is to be a mobile node coa, according to
 * the binding cache entry for the destined mobile node.
 */
int
mip6_tunnel_output(mp, mbc)
	struct mbuf **mp;    /* the original ipv6 packet */
	struct mip6_bc *mbc; /* the bc entry for the dst of the pkt */
{
	const struct encaptab *ep = mbc->mbc_encap;
	struct mbuf *m = *mp;
	struct sockaddr_in6 *encap_src = &mbc->mbc_addr;
	struct sockaddr_in6 *encap_dst = &mbc->mbc_pcoa;
	struct ip6_hdr *ip6;
	int len, error = 0;

	if (ep->af != AF_INET6) {
		mip6log((LOG_ERR,
			 "%s:%d: illegal address family type %d\n",
			 __FILE__, __LINE__, ep->af));
		return (EFAULT);
	}

	/* Recursion problems? */

	if (SA6_IS_ADDR_UNSPECIFIED(encap_src)) {
		mip6log((LOG_ERR,
			 "%s:%d: the encap source address is unspecified\n",
			 __FILE__, __LINE__));
		return (EFAULT);
	}

	len = m->m_pkthdr.len; /* payload length */

	if (m->m_len < sizeof(*ip6)) {
		m = m_pullup(m, sizeof(*ip6));
		if (!m) {
			mip6log((LOG_ERR,
				 "%s:%d: m_pullup failed\n",
				 __FILE__, __LINE__));
			return (ENOBUFS);
		}
	}
	ip6 = mtod(m, struct ip6_hdr *);

	/* prepend new, outer ipv6 header */
	M_PREPEND(m, sizeof(struct ip6_hdr), M_DONTWAIT);
	if (m && m->m_len < sizeof(struct ip6_hdr))
		m = m_pullup(m, sizeof(struct ip6_hdr));
	if (m == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: outer header allocation failed\n",
			 __FILE__, __LINE__));
		return (ENOBUFS);
	}

	/* fill the outer header */
	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	ip6->ip6_plen = htons((u_short)len);
	ip6->ip6_nxt = IPPROTO_IPV6;
	ip6->ip6_hlim = ip6_defhlim;
	ip6->ip6_src = encap_src->sin6_addr;

	/* bidirectional configured tunnel mode */
	if (!SA6_IS_ADDR_UNSPECIFIED(encap_dst))
		ip6->ip6_dst = encap_dst->sin6_addr;
	else {
		mip6log((LOG_ERR,
			 "%s:%d: the encap dest address is unspecified\n",
			 __FILE__, __LINE__));
		m_freem(m);
		return (ENETUNREACH);
	}
	if (ip6_setpktaddrs(m, encap_src, encap_dst) == NULL) {
		m_freem(m);
		return(error);
	}
#if defined(IPV6_MINMTU) && 0
	/*
	 * force fragmentation to minimum MTU, to avoid path MTU discovery.
	 * it is too painful to ask for resend of inner packet, to achieve
	 * path MTU discovery for encapsulated packets.
	 */
	return(ip6_output(m, 0, 0, IPV6_MINMTU, 0, NULL));
#else
	return(ip6_output(m, 0, 0, 0, 0, NULL));
#endif
}

/*
 * if packet is tunneled, send BU to the peer for route optimization.
 */
/*
 * the algorithm below is worth considering
 *
 * from Hesham Soliman on mobile-ip
 * <034BEFD03799D411A59F00508BDF754603008B1F@esealnt448.al.sw.ericsson.se>
 *
 * - Did the packet contain a routing header ?
 * - Did the routing header contain the Home address of the 
 *  MN as the last segment and its CoA (as specified in the BU list) ?
 *
 * If the answer to both is yes then the packet was route optimised.
 * if no then it wasn't and it doesn't really matter whether it was 
 * tunnelled by THE HA or another node. 
 * This will have two advantages (outside the HMIPv6 area) :
 *
 * - Simpler processing in the kernel since the MIPv6 code would
 *   not have to "remember" whether the inner packet being processed
 *   now was originally tunnelled.
 *
 * - Will allow for future HA redundancy mechanisms because if the 
 *   HA crashes and another HA starts tunnelling the packet the 
 *   MN does not need to know or care. Excet of course when it's 
 *   about to refresh the Binding Cache but that can be handled 
 *   by the HA redundancy protocol.
 */
int
mip6_route_optimize(m)
	struct mbuf *m;
{
	struct mbuf *n;
	struct ip6aux *ip6a;
	struct ip6_hdr *ip6;
	struct sockaddr_in6 *sin6src, *sin6dst;
	struct mip6_prefix *mpfx;
	struct mip6_bu *mbu;
	struct hif_softc *sc;
	int32_t coa_lifetime;
	int error = 0;

	if (!MIP6_IS_MN) {
		/* only MN does the route optimization. */
		return (0);
	}

	ip6 = mtod(m, struct ip6_hdr *);
	if (ip6_getpktaddrs(m, &sin6src, &sin6dst))
		return (EINVAL);

	if (IN6_IS_ADDR_LINKLOCAL(&sin6src->sin6_addr) ||
	    IN6_IS_ADDR_SITELOCAL(&sin6src->sin6_addr)) {	/* XXX */
		return (0);
	}
	/* Quick check */
	if (IN6_IS_ADDR_LINKLOCAL(&sin6dst->sin6_addr) ||
	    IN6_IS_ADDR_SITELOCAL(&sin6dst->sin6_addr) ||	/* XXX */
	    IN6_IS_ADDR_MULTICAST(&sin6dst->sin6_addr)) {
		return (0);
	}
	
	n = ip6_findaux(m);
	if (n) {
		ip6a = mtod(n, struct ip6aux *);
		if (ip6a->ip6a_flags & IP6A_ROUTEOPTIMIZED) {
			/* no need to optimize route. */
			return (0);
		}
	}
	/*
	 * this packet has no rthdr or has a rthdr not related mip6
	 * route optimization.
	 */

	/* check if we are home. */
	sc = hif_list_find_withhaddr(sin6dst);
	if (sc == NULL) {
		/* this dst addr is not one of our home addresses. */
		return (0);
	}
	if (sc->hif_location == HIF_LOCATION_HOME) {
		/* we are home.  no route optimization is required. */
		return (0);
	}

	/*
	 * find a mip6_prefix which has a home address of received
	 * packet.
	 */
	mpfx = mip6_prefix_list_find_withhaddr(&mip6_prefix_list,
					       sin6dst);
	if (mpfx == NULL) {
		/*
		 * no related prefix found.  this packet is
		 * destined to another address of this node
		 * that is not a home address.
		 */
		return (0);
	}

	/*
	 * search all binding update entries with the address of the
	 * peer sending this un-optimized packet.
	 */
	mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list,
					  sin6src,
					  sin6dst);
	if (mbu == NULL) {
		/*
		 * if no binding update entry is found, this is a
		 * first packet from the peer.  create a new binding
		 * update entry for this peer.
		 */
		mbu = mip6_bu_create(sin6src,
				     mpfx,
				     &hif_coa,
				     0, sc);
		if (mbu == NULL) {
			error = ENOMEM;
			goto bad;
		}
		mbu->mbu_state |= MIP6_BU_STATE_WAITSENT;

		mip6_bu_list_insert(&sc->hif_bu_list, mbu);
	} else {
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
		long time_second = time.tv_sec;
#endif
		/*
		 * found a binding update entry.  we should resend a
		 * binding update to this peer because he is not add
		 * routing header for the route optimization.
		 */
		mbu->mbu_coa = hif_coa;
		coa_lifetime = mip6_coa_get_lifetime(&mbu->mbu_coa.sin6_addr);
		if (coa_lifetime < mpfx->mpfx_pltime) {
			mbu->mbu_lifetime = coa_lifetime;
		} else {
			mbu->mbu_lifetime = mpfx->mpfx_pltime;
		}
		mbu->mbu_expire = time_second + mbu->mbu_lifetime;
		/* sanity check for overflow */
		if (mbu->mbu_expire < time_second)
			mbu->mbu_expire = 0x7fffffff;
		mbu->mbu_refresh = mbu->mbu_lifetime;
		mbu->mbu_refexpire = time_second + mbu->mbu_refresh;
		/* sanity check for overflow */
		if (mbu->mbu_refexpire < time_second)
			mbu->mbu_refexpire = 0x7fffffff;
		mbu->mbu_state |= MIP6_BU_STATE_WAITSENT;
	}

	return (0);
 bad:
	m_freem(m);
	return (error);
}