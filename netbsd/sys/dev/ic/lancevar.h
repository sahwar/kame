/*	$NetBSD: lancevar.h,v 1.2 1998/08/15 10:51:18 mycroft Exp $	*/

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "rnd.h"

#if NRND > 0
#include <sys/rnd.h>
#endif

struct lance_softc {
	struct	device sc_dev;		/* base device glue */
	struct	ethercom sc_ethercom;	/* Ethernet common part */
	struct	ifmedia sc_media;	/* our supported media */

	/*
	 * Memory functions:
	 *
	 *	copy to/from descriptor
	 *	copy to/from buffer
	 *	zero bytes in buffer
	 */
	void	(*sc_copytodesc)
		    __P((struct lance_softc *, void *, int, int));
	void	(*sc_copyfromdesc)
		    __P((struct lance_softc *, void *, int, int));
	void	(*sc_copytobuf)
		    __P((struct lance_softc *, void *, int, int));
	void	(*sc_copyfrombuf)
		    __P((struct lance_softc *, void *, int, int));
	void	(*sc_zerobuf)
		    __P((struct lance_softc *, int, int));

	/*
	 * Machine-dependent functions:
	 *
	 *	read/write CSR
	 *	hardware reset hook - may be NULL
	 *	hardware init hook - may be NULL
	 *	no carrier hook - may be NULL
	 *	media change hook - may be NULL
	 */
	u_int16_t (*sc_rdcsr)
		    __P((struct lance_softc *, u_int16_t));
	void	(*sc_wrcsr)
		    __P((struct lance_softc *, u_int16_t, u_int16_t));
	void	(*sc_hwreset) __P((struct lance_softc *));
	void	(*sc_hwinit) __P((struct lance_softc *));
	void	(*sc_nocarrier) __P((struct lance_softc *));
	int	(*sc_mediachange) __P((struct lance_softc *));
	void	(*sc_mediastatus) __P((struct lance_softc *,
		    struct ifmediareq *));

	/*
	 * Media-supported by this interface.  If this is NULL,
	 * the only supported media is assumed to be "manual".
	 */
	int	*sc_supmedia;
	int	sc_nsupmedia;
	int	sc_defaultmedia;

	/* PCnet bit to use software selection of a port */
	int	sc_initmodemedia;

	int	sc_havecarrier;	/* carrier status */

	void	*sc_sh;		/* shutdownhook cookie */

	u_int16_t sc_conf3;	/* CSR3 value */
	u_int16_t sc_saved_csr0;/* Value of csr0 at time of interrupt */

	void	*sc_mem;	/* base address of RAM -- CPU's view */
	u_long	sc_addr;	/* base address of RAM -- LANCE's view */

	u_long	sc_memsize;	/* size of RAM */

	int	sc_nrbuf;	/* number of receive buffers */
	int	sc_ntbuf;	/* number of transmit buffers */
	int	sc_last_rd;
	int	sc_first_td, sc_last_td, sc_no_td;

	int	sc_initaddr;
	int	sc_rmdaddr;
	int	sc_tmdaddr;
	int	*sc_rbufaddr;
	int	*sc_tbufaddr;

#ifdef LEDEBUG
	int	sc_debug;
#endif
	u_int8_t sc_enaddr[6];
	u_int8_t sc_pad[2];
#if NRND > 0
	rndsource_element_t	rnd_source;
#endif

	void (*sc_meminit) __P((struct lance_softc *));
	void (*sc_start) __P((struct ifnet *));
};

void lance_config __P((struct lance_softc *));
void lance_reset __P((struct lance_softc *));
void lance_init __P((struct lance_softc *));
int lance_put __P((struct lance_softc *, int, struct mbuf *));
void lance_read __P((struct lance_softc *, int, int)); 
void lance_setladrf __P((struct ethercom *, u_int16_t *));

/*
 * The following functions are only useful on certain cpu/bus
 * combinations.  They should be written in assembly language for
 * maximum efficiency, but machine-independent versions are provided
 * for drivers that have not yet been optimized.
 */
void lance_copytobuf_contig __P((struct lance_softc *, void *, int, int));
void lance_copyfrombuf_contig __P((struct lance_softc *, void *, int, int));
void lance_zerobuf_contig __P((struct lance_softc *, int, int));

#if 0	/* Example only - see lance.c */
void lance_copytobuf_gap2 __P((struct lance_softc *, void *, int, int));
void lance_copyfrombuf_gap2 __P((struct lance_softc *, void *, int, int));
void lance_zerobuf_gap2 __P((struct lance_softc *, int, int));

void lance_copytobuf_gap16 __P((struct lance_softc *, void *, int, int));
void lance_copyfrombuf_gap16 __P((struct lance_softc *, void *, int, int));
void lance_zerobuf_gap16 __P((struct lance_softc *, int, int));
#endif /* Example only */
