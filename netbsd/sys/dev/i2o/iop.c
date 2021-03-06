/*	$NetBSD: iop.c,v 1.19.4.3 2002/03/09 19:38:16 he Exp $	*/

/*-
 * Copyright (c) 2000, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
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

/*
 * Support for I2O IOPs (intelligent I/O processors).
 */

#include "opt_i2o.h"
#include "iop.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/ioctl.h>
#include <sys/endian.h>
#include <sys/conf.h>
#include <sys/kthread.h>

#include <machine/vmparam.h>
#include <machine/bus.h>

#include <vm/vm.h>

#include <dev/i2o/i2o.h>
#include <dev/i2o/iopio.h>
#include <dev/i2o/iopreg.h>
#include <dev/i2o/iopvar.h>

#define POLL(ms, cond)				\
do {						\
	int i;					\
	for (i = (ms) * 10; i; i--) {		\
		if (cond)			\
			break;			\
		DELAY(100);			\
	}					\
} while (/* CONSTCOND */0);

#ifdef I2ODEBUG
#define DPRINTF(x)	printf x
#else
#define	DPRINTF(x)
#endif

#ifdef I2OVERBOSE
#define IFVERBOSE(x)	x
#define	COMMENT(x)	NULL
#else
#define	IFVERBOSE(x)
#define	COMMENT(x)
#endif

#define IOP_ICTXHASH_NBUCKETS	16
#define	IOP_ICTXHASH(ictx)	(&iop_ictxhashtbl[(ictx) & iop_ictxhash])

#define	IOP_MAX_SEGS	(((IOP_MAX_XFER + NBPG - 1) / NBPG) + 1)

#define	IOP_TCTX_SHIFT	12
#define	IOP_TCTX_MASK	((1 << IOP_TCTX_SHIFT) - 1)

static LIST_HEAD(, iop_initiator) *iop_ictxhashtbl;
static u_long	iop_ictxhash;
static void	*iop_sdh;
static struct	i2o_systab *iop_systab;
static int	iop_systab_size;

extern struct cfdriver iop_cd;

#define	IC_CONFIGURE	0x01
#define	IC_PRIORITY	0x02

struct iop_class {
	u_short	ic_class;
	u_short	ic_flags;
#ifdef I2OVERBOSE
	const char	*ic_caption;
#endif
} static const iop_class[] = {
	{	
		I2O_CLASS_EXECUTIVE,
		0,
		COMMENT("executive")
	},
	{
		I2O_CLASS_DDM,
		0,
		COMMENT("device driver module")
	},
	{
		I2O_CLASS_RANDOM_BLOCK_STORAGE,
		IC_CONFIGURE | IC_PRIORITY,
		IFVERBOSE("random block storage")
	},
	{
		I2O_CLASS_SEQUENTIAL_STORAGE,
		IC_CONFIGURE | IC_PRIORITY,
		IFVERBOSE("sequential storage")
	},
	{
		I2O_CLASS_LAN,
		IC_CONFIGURE | IC_PRIORITY,
		IFVERBOSE("LAN port")
	},
	{
		I2O_CLASS_WAN,
		IC_CONFIGURE | IC_PRIORITY,
		IFVERBOSE("WAN port")
	},
	{
		I2O_CLASS_FIBRE_CHANNEL_PORT,
		IC_CONFIGURE,
		IFVERBOSE("fibrechannel port")
	},
	{
		I2O_CLASS_FIBRE_CHANNEL_PERIPHERAL,
		0,
		COMMENT("fibrechannel peripheral")
	},
 	{
 		I2O_CLASS_SCSI_PERIPHERAL,
 		0,
 		COMMENT("SCSI peripheral")
 	},
	{
		I2O_CLASS_ATE_PORT,
		IC_CONFIGURE,
		IFVERBOSE("ATE port")
	},
	{	
		I2O_CLASS_ATE_PERIPHERAL,
		0,
		COMMENT("ATE peripheral")
	},
	{	
		I2O_CLASS_FLOPPY_CONTROLLER,
		IC_CONFIGURE,
		IFVERBOSE("floppy controller")
	},
	{
		I2O_CLASS_FLOPPY_DEVICE,
		0,
		COMMENT("floppy device")
	},
	{
		I2O_CLASS_BUS_ADAPTER_PORT,
		IC_CONFIGURE,
		IFVERBOSE("bus adapter port" )
	},
};

#if defined(I2ODEBUG) && defined(I2OVERBOSE)
static const char * const iop_status[] = {
	"success",
	"abort (dirty)",
	"abort (no data transfer)",
	"abort (partial transfer)",
	"error (dirty)",
	"error (no data transfer)",
	"error (partial transfer)",
	"undefined error code",
	"process abort (dirty)",
	"process abort (no data transfer)",
	"process abort (partial transfer)",
	"transaction error",
};
#endif

static inline u_int32_t	iop_inl(struct iop_softc *, int);
static inline void	iop_outl(struct iop_softc *, int, u_int32_t);

static void	iop_config_interrupts(struct device *);
static void	iop_configure_devices(struct iop_softc *, int, int);
static void	iop_devinfo(int, char *);
static int	iop_print(void *, const char *);
static void	iop_shutdown(void *);
static int	iop_submatch(struct device *, struct cfdata *, void *);
static int	iop_vendor_print(void *, const char *);

static void	iop_adjqparam(struct iop_softc *, int);
static void	iop_create_reconf_thread(void *);
static int	iop_handle_reply(struct iop_softc *, u_int32_t);
static int	iop_hrt_get(struct iop_softc *);
static int	iop_hrt_get0(struct iop_softc *, struct i2o_hrt *, int);
static void	iop_intr_event(struct device *, struct iop_msg *, void *);
static int	iop_lct_get0(struct iop_softc *, struct i2o_lct *, int,
			     u_int32_t);
static void	iop_msg_poll(struct iop_softc *, struct iop_msg *, int);
static void	iop_msg_wait(struct iop_softc *, struct iop_msg *, int);
static int	iop_ofifo_init(struct iop_softc *);
static int	iop_passthrough(struct iop_softc *, struct ioppt *,
				struct proc *);
static void	iop_reconf_thread(void *);
static void	iop_release_mfa(struct iop_softc *, u_int32_t);
static int	iop_reset(struct iop_softc *);
static int	iop_systab_set(struct iop_softc *);
static void	iop_tfn_print(struct iop_softc *, struct i2o_fault_notify *);

#ifdef I2ODEBUG
static void	iop_reply_print(struct iop_softc *, struct i2o_reply *);
#endif

cdev_decl(iop);

static inline u_int32_t
iop_inl(struct iop_softc *sc, int off)
{

	bus_space_barrier(sc->sc_iot, sc->sc_ioh, off, 4,
	    BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, off));
}

static inline void
iop_outl(struct iop_softc *sc, int off, u_int32_t val)
{

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, off, val);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

/*
 * Initialise the IOP and our interface.
 */
void
iop_init(struct iop_softc *sc, const char *intrstr)
{
	struct iop_msg *im;
	int rv, i, j, state, nsegs;
	u_int32_t mask;
	char ident[64];

	state = 0;

	printf("I2O adapter");

	if (iop_ictxhashtbl == NULL)
		iop_ictxhashtbl = hashinit(IOP_ICTXHASH_NBUCKETS,
		    M_DEVBUF, M_NOWAIT, &iop_ictxhash);

	/* Disable interrupts at the IOP. */
	mask = iop_inl(sc, IOP_REG_INTR_MASK);
	iop_outl(sc, IOP_REG_INTR_MASK, mask | IOP_INTR_OFIFO);

	/* Allocate a scratch DMA map for small miscellaneous shared data. */
	if (bus_dmamap_create(sc->sc_dmat, NBPG, 1, NBPG, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->sc_scr_dmamap) != 0) {
		printf("%s: cannot create scratch dmamap\n",
		    sc->sc_dv.dv_xname);
		return;
	}

	if (bus_dmamem_alloc(sc->sc_dmat, NBPG, NBPG, 0,
	    sc->sc_scr_seg, 1, &nsegs, BUS_DMA_NOWAIT) != 0) {
		printf("%s: cannot alloc scratch dmamem\n",
		    sc->sc_dv.dv_xname);
		goto bail_out;
	}
	state++;

	if (bus_dmamem_map(sc->sc_dmat, sc->sc_scr_seg, nsegs, NBPG,
	    &sc->sc_scr, 0)) {
		printf("%s: cannot map scratch dmamem\n", sc->sc_dv.dv_xname);
		goto bail_out;
	}
	state++;

	if (bus_dmamap_load(sc->sc_dmat, sc->sc_scr_dmamap, sc->sc_scr,
	    NBPG, NULL, BUS_DMA_NOWAIT)) {
		printf("%s: cannot load scratch dmamap\n", sc->sc_dv.dv_xname);
		goto bail_out;
	}
	state++;

#ifdef I2ODEBUG
	/* So that our debug checks don't choke. */
	sc->sc_framesize = 128;
#endif

	/* Reset the adapter and request status. */
 	if ((rv = iop_reset(sc)) != 0) {
 		printf("%s: not responding (reset)\n", sc->sc_dv.dv_xname);
		goto bail_out;
 	}

 	if ((rv = iop_status_get(sc, 1)) != 0) {
		printf("%s: not responding (get status)\n",
		    sc->sc_dv.dv_xname);
		goto bail_out;
 	}

	sc->sc_flags |= IOP_HAVESTATUS;
	iop_strvis(sc, sc->sc_status.productid, sizeof(sc->sc_status.productid),
	    ident, sizeof(ident));
	printf(" <%s>\n", ident);

#ifdef I2ODEBUG
	printf("%s: orgid=0x%04x version=%d\n", sc->sc_dv.dv_xname,
	    le16toh(sc->sc_status.orgid),
	    (le32toh(sc->sc_status.segnumber) >> 12) & 15);
	printf("%s: type want have cbase\n", sc->sc_dv.dv_xname);
	printf("%s: mem  %04x %04x %08x\n", sc->sc_dv.dv_xname,
	    le32toh(sc->sc_status.desiredprivmemsize),
	    le32toh(sc->sc_status.currentprivmemsize),
	    le32toh(sc->sc_status.currentprivmembase));
	printf("%s: i/o  %04x %04x %08x\n", sc->sc_dv.dv_xname,
	    le32toh(sc->sc_status.desiredpriviosize),
	    le32toh(sc->sc_status.currentpriviosize),
	    le32toh(sc->sc_status.currentpriviobase));
#endif

	sc->sc_maxob = le32toh(sc->sc_status.maxoutboundmframes);
	if (sc->sc_maxob > IOP_MAX_OUTBOUND)
		sc->sc_maxob = IOP_MAX_OUTBOUND;
	sc->sc_maxib = le32toh(sc->sc_status.maxinboundmframes);
	if (sc->sc_maxib > IOP_MAX_INBOUND)
		sc->sc_maxib = IOP_MAX_INBOUND;
	sc->sc_framesize = le16toh(sc->sc_status.inboundmframesize) << 2;
	if (sc->sc_framesize > IOP_MAX_MSG_SIZE)
		sc->sc_framesize = IOP_MAX_MSG_SIZE;

#if defined(I2ODEBUG) || defined(DIAGNOSTIC)
	if (sc->sc_framesize < IOP_MIN_MSG_SIZE) {
		printf("%s: frame size too small (%d)\n",
		    sc->sc_dv.dv_xname, sc->sc_framesize);
		return;
	}
#endif

	/* Allocate message wrappers. */
	im = malloc(sizeof(*im) * sc->sc_maxib, M_DEVBUF, M_NOWAIT);
	if (im == NULL) {
		printf("%s: memory allocation failure\n", sc->sc_dv.dv_xname);
		goto bail_out;
	}
	state++;
	memset(im, 0, sizeof(*im) * sc->sc_maxib);
	sc->sc_ims = im;
	SLIST_INIT(&sc->sc_im_freelist);

	for (i = 0, state++; i < sc->sc_maxib; i++, im++) {
		rv = bus_dmamap_create(sc->sc_dmat, IOP_MAX_XFER,
		    IOP_MAX_SEGS, IOP_MAX_XFER, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &im->im_xfer[0].ix_map);
		if (rv != 0) {
			printf("%s: couldn't create dmamap (%d)",
			    sc->sc_dv.dv_xname, rv);
			goto bail_out;
		}

		im->im_tctx = i;
		SLIST_INSERT_HEAD(&sc->sc_im_freelist, im, im_chain);
	}

	/* Initialise the IOP's outbound FIFO. */
	if (iop_ofifo_init(sc) != 0) {
		printf("%s: unable to init oubound FIFO\n",
		    sc->sc_dv.dv_xname);
		goto bail_out;
	}

	/*
 	 * Defer further configuration until (a) interrupts are working and
 	 * (b) we have enough information to build the system table.
 	 */
	config_interrupts((struct device *)sc, iop_config_interrupts);

	/* Configure shutdown hook before we start any device activity. */
	if (iop_sdh == NULL)
		iop_sdh = shutdownhook_establish(iop_shutdown, NULL);

	/* Ensure interrupts are enabled at the IOP. */
	mask = iop_inl(sc, IOP_REG_INTR_MASK);
	iop_outl(sc, IOP_REG_INTR_MASK, mask & ~IOP_INTR_OFIFO);

	if (intrstr != NULL)
		printf("%s: interrupting at %s\n", sc->sc_dv.dv_xname,
		    intrstr);

#ifdef I2ODEBUG
	printf("%s: queue depths: inbound %d/%d, outbound %d/%d\n",
	    sc->sc_dv.dv_xname, sc->sc_maxib,
	    le32toh(sc->sc_status.maxinboundmframes),
	    sc->sc_maxob, le32toh(sc->sc_status.maxoutboundmframes));
#endif

	lockinit(&sc->sc_conflock, PRIBIO, "iopconf", hz * 30, 0);
	return;

 bail_out:
 	if (state > 3) {
		for (j = 0; j < i; j++)
			bus_dmamap_destroy(sc->sc_dmat,
			    sc->sc_ims[j].im_xfer[0].ix_map);
		free(sc->sc_ims, M_DEVBUF);
	}
	if (state > 2)
		bus_dmamap_unload(sc->sc_dmat, sc->sc_scr_dmamap);
	if (state > 1)
		bus_dmamem_unmap(sc->sc_dmat, sc->sc_scr, NBPG);
	if (state > 0)
		bus_dmamem_free(sc->sc_dmat, sc->sc_scr_seg, nsegs);
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_scr_dmamap);
}

/*
 * Perform autoconfiguration tasks.
 */
static void
iop_config_interrupts(struct device *self)
{
	struct iop_attach_args ia;
	struct iop_softc *sc, *iop;
	struct i2o_systab_entry *ste;
	int rv, i, niop;

	sc = (struct iop_softc *)self;
	LIST_INIT(&sc->sc_iilist);

	printf("%s: configuring...\n", sc->sc_dv.dv_xname);

	if (iop_hrt_get(sc) != 0) {
		printf("%s: unable to retrieve HRT\n", sc->sc_dv.dv_xname);
		return;
	}

	/*
 	 * Build the system table.
 	 */
	if (iop_systab == NULL) {
		for (i = 0, niop = 0; i < iop_cd.cd_ndevs; i++) {
			if ((iop = device_lookup(&iop_cd, i)) == NULL)
				continue;
			if ((iop->sc_flags & IOP_HAVESTATUS) == 0)
				continue;
			if (iop_status_get(iop, 1) != 0) {
				printf("%s: unable to retrieve status\n",
				    sc->sc_dv.dv_xname);
				iop->sc_flags &= ~IOP_HAVESTATUS;
				continue;
			}
			niop++;
		}
		if (niop == 0)
			return;

		i = sizeof(struct i2o_systab_entry) * (niop - 1) +
		    sizeof(struct i2o_systab);
		iop_systab_size = i;
		iop_systab = malloc(i, M_DEVBUF, M_NOWAIT);

		memset(iop_systab, 0, i);
		iop_systab->numentries = niop;
		iop_systab->version = I2O_VERSION_11;

		for (i = 0, ste = iop_systab->entry; i < iop_cd.cd_ndevs; i++) {
			if ((iop = device_lookup(&iop_cd, i)) == NULL)
				continue;
			if ((iop->sc_flags & IOP_HAVESTATUS) == 0)
				continue;

			ste->orgid = iop->sc_status.orgid;
			ste->iopid = iop->sc_dv.dv_unit + 2;
			ste->segnumber =
			    htole32(le32toh(iop->sc_status.segnumber) & ~4095);
			ste->iopcaps = iop->sc_status.iopcaps;
			ste->inboundmsgframesize =
			    iop->sc_status.inboundmframesize;
			ste->inboundmsgportaddresslow =
			    htole32(iop->sc_memaddr + IOP_REG_IFIFO);
			ste++;
		}
	}

	/*
	 * Post the system table to the IOP and bring it to the OPERATIONAL
	 * state.
	 */
	if (iop_systab_set(sc) != 0) {
		printf("%s: unable to set system table\n", sc->sc_dv.dv_xname);
		return;
	}
	if (iop_simple_cmd(sc, I2O_TID_IOP, I2O_EXEC_SYS_ENABLE, IOP_ICTX, 1,
	    30000) != 0) {
		printf("%s: unable to enable system\n", sc->sc_dv.dv_xname);
		return;
	}

	/*
	 * Set up an event handler for this IOP.
	 */
	sc->sc_eventii.ii_dv = self;
	sc->sc_eventii.ii_intr = iop_intr_event;
	sc->sc_eventii.ii_flags = II_NOTCTX | II_UTILITY;
	sc->sc_eventii.ii_tid = I2O_TID_IOP;
	iop_initiator_register(sc, &sc->sc_eventii);

	rv = iop_util_eventreg(sc, &sc->sc_eventii,
	    I2O_EVENT_EXEC_RESOURCE_LIMITS |
	    I2O_EVENT_EXEC_CONNECTION_FAIL |
	    I2O_EVENT_EXEC_ADAPTER_FAULT |
	    I2O_EVENT_EXEC_POWER_FAIL |
	    I2O_EVENT_EXEC_RESET_PENDING |
	    I2O_EVENT_EXEC_RESET_IMMINENT |
	    I2O_EVENT_EXEC_HARDWARE_FAIL |
	    I2O_EVENT_EXEC_XCT_CHANGE |
	    I2O_EVENT_EXEC_DDM_AVAILIBILITY |
	    I2O_EVENT_GEN_DEVICE_RESET |
	    I2O_EVENT_GEN_STATE_CHANGE |
	    I2O_EVENT_GEN_GENERAL_WARNING);
	if (rv != 0) {
		printf("%s: unable to register for events", sc->sc_dv.dv_xname);
		return;
	}

	/*
	 * Attempt to match and attach a product-specific extension.
	 */
	ia.ia_class = I2O_CLASS_ANY;
	ia.ia_tid = I2O_TID_IOP;
	config_found_sm(self, &ia, iop_vendor_print, iop_submatch);

	/*
	 * Start device configuration.
	 */
	lockmgr(&sc->sc_conflock, LK_EXCLUSIVE, NULL);
	if ((rv = iop_reconfigure(sc, 0)) == -1) {
		printf("%s: configure failed (%d)\n", sc->sc_dv.dv_xname, rv);
		return;
	}
	lockmgr(&sc->sc_conflock, LK_RELEASE, NULL);

	kthread_create(iop_create_reconf_thread, sc);
}

/*
 * Create the reconfiguration thread.  Called after the standard kernel
 * threads have been created.
 */
static void
iop_create_reconf_thread(void *cookie)
{
	struct iop_softc *sc;
	int rv;

	sc = cookie;
	sc->sc_flags |= IOP_ONLINE;

	rv = kthread_create1(iop_reconf_thread, sc, &sc->sc_reconf_proc,
 	    "%s", sc->sc_dv.dv_xname);
 	if (rv != 0) {
		printf("%s: unable to create reconfiguration thread (%d)",
 		    sc->sc_dv.dv_xname, rv);
 		return;
 	}
}

/*
 * Reconfiguration thread; listens for LCT change notification, and
 * initiates re-configuration if received.
 */
static void
iop_reconf_thread(void *cookie)
{
	struct iop_softc *sc;
	struct i2o_lct lct;
	u_int32_t chgind;
	int rv;

	sc = cookie;
	chgind = sc->sc_chgind + 1;

	for (;;) {
		DPRINTF(("%s: async reconfig: requested 0x%08x\n",
		    sc->sc_dv.dv_xname, chgind));

		PHOLD(sc->sc_reconf_proc);
		rv = iop_lct_get0(sc, &lct, sizeof(lct), chgind);
		PRELE(sc->sc_reconf_proc);

		DPRINTF(("%s: async reconfig: notified (0x%08x, %d)\n",
		    sc->sc_dv.dv_xname, le32toh(lct.changeindicator), rv));

		if (rv == 0 &&
		    lockmgr(&sc->sc_conflock, LK_EXCLUSIVE, NULL) == 0) {
			iop_reconfigure(sc, le32toh(lct.changeindicator));
			chgind = sc->sc_chgind + 1;
			lockmgr(&sc->sc_conflock, LK_RELEASE, NULL);
		}

		tsleep(iop_reconf_thread, PWAIT, "iopzzz", hz * 5);
	}
}

/*
 * Reconfigure: find new and removed devices.
 */
int
iop_reconfigure(struct iop_softc *sc, u_int chgind)
{
	struct iop_msg *im;
	struct i2o_hba_bus_scan mf;
	struct i2o_lct_entry *le;
	struct iop_initiator *ii, *nextii;
	int rv, tid, i;

	/*
	 * If the reconfiguration request isn't the result of LCT change
	 * notification, then be more thorough: ask all bus ports to scan
	 * their busses.  Wait up to 5 minutes for each bus port to complete
	 * the request.
	 */
	if (chgind == 0) {
		if ((rv = iop_lct_get(sc)) != 0) {
			DPRINTF(("iop_reconfigure: unable to read LCT\n"));
			return (rv);
		}

		le = sc->sc_lct->entry;
		for (i = 0; i < sc->sc_nlctent; i++, le++) {
			if ((le16toh(le->classid) & 4095) !=
			    I2O_CLASS_BUS_ADAPTER_PORT)
				continue;
			tid = le16toh(le->localtid) & 4095;

			im = iop_msg_alloc(sc, IM_WAIT);

			mf.msgflags = I2O_MSGFLAGS(i2o_hba_bus_scan);
			mf.msgfunc = I2O_MSGFUNC(tid, I2O_HBA_BUS_SCAN);
			mf.msgictx = IOP_ICTX;
			mf.msgtctx = im->im_tctx;

			DPRINTF(("%s: scanning bus %d\n", sc->sc_dv.dv_xname,
			    tid));

			rv = iop_msg_post(sc, im, &mf, 5*60*1000);
			iop_msg_free(sc, im);
#ifdef I2ODEBUG
			if (rv != 0)
				printf("%s: bus scan failed\n",
				    sc->sc_dv.dv_xname);
#endif
		}
	} else if (chgind <= sc->sc_chgind) {
		DPRINTF(("%s: LCT unchanged (async)\n", sc->sc_dv.dv_xname));
		return (0);
	}

	/* Re-read the LCT and determine if it has changed. */
	if ((rv = iop_lct_get(sc)) != 0) {
		DPRINTF(("iop_reconfigure: unable to re-read LCT\n"));
		return (rv);
	}
	DPRINTF(("%s: %d LCT entries\n", sc->sc_dv.dv_xname, sc->sc_nlctent));

	chgind = le32toh(sc->sc_lct->changeindicator);
	if (chgind == sc->sc_chgind) {
		DPRINTF(("%s: LCT unchanged\n", sc->sc_dv.dv_xname));
		return (0);
	}
	DPRINTF(("%s: LCT changed\n", sc->sc_dv.dv_xname));
	sc->sc_chgind = chgind;

	if (sc->sc_tidmap != NULL)
		free(sc->sc_tidmap, M_DEVBUF);
	sc->sc_tidmap = malloc(sc->sc_nlctent * sizeof(struct iop_tidmap),
	    M_DEVBUF, M_NOWAIT);
	memset(sc->sc_tidmap, 0, sizeof(sc->sc_tidmap));

	/* Allow 1 queued command per device while we're configuring. */
	iop_adjqparam(sc, 1);

	/*
	 * Match and attach child devices.  We configure high-level devices
	 * first so that any claims will propagate throughout the LCT,
	 * hopefully masking off aliased devices as a result.
	 *
	 * Re-reading the LCT at this point is a little dangerous, but we'll
	 * trust the IOP (and the operator) to behave itself...
	 */
	iop_configure_devices(sc, IC_CONFIGURE | IC_PRIORITY,
	    IC_CONFIGURE | IC_PRIORITY);
	if ((rv = iop_lct_get(sc)) != 0)
		DPRINTF(("iop_reconfigure: unable to re-read LCT\n"));
	iop_configure_devices(sc, IC_CONFIGURE | IC_PRIORITY,
	    IC_CONFIGURE);

	for (ii = LIST_FIRST(&sc->sc_iilist); ii != NULL; ii = nextii) {
		nextii = LIST_NEXT(ii, ii_list);

		/* Detach devices that were configured, but are now gone. */
		for (i = 0; i < sc->sc_nlctent; i++)
			if (ii->ii_tid == sc->sc_tidmap[i].it_tid)
				break;
		if (i == sc->sc_nlctent ||
		    (sc->sc_tidmap[i].it_flags & IT_CONFIGURED) == 0)
			config_detach(ii->ii_dv, DETACH_FORCE);

		/*
		 * Tell initiators that existed before the re-configuration
		 * to re-configure.
		 */
		if (ii->ii_reconfig == NULL)
			continue;
		if ((rv = (*ii->ii_reconfig)(ii->ii_dv)) != 0)
			printf("%s: %s failed reconfigure (%d)\n",
			    sc->sc_dv.dv_xname, ii->ii_dv->dv_xname, rv);
	}

	/* Re-adjust queue parameters and return. */
	if (sc->sc_nii != 0)
		iop_adjqparam(sc, (sc->sc_maxib - sc->sc_nuii - IOP_MF_RESERVE)
		    / sc->sc_nii);

	return (0);
}

/*
 * Configure I2O devices into the system.
 */
static void
iop_configure_devices(struct iop_softc *sc, int mask, int maskval)
{
	struct iop_attach_args ia;
	struct iop_initiator *ii;
	const struct i2o_lct_entry *le;
	struct device *dv;
	int i, j, nent;
	u_int usertid;

	nent = sc->sc_nlctent;
	for (i = 0, le = sc->sc_lct->entry; i < nent; i++, le++) {
		sc->sc_tidmap[i].it_tid = le16toh(le->localtid) & 4095;

		/* Ignore the device if it's in use. */
		usertid = le32toh(le->usertid) & 4095;
		if (usertid != I2O_TID_NONE && usertid != I2O_TID_HOST)
			continue;

		ia.ia_class = le16toh(le->classid) & 4095;
		ia.ia_tid = sc->sc_tidmap[i].it_tid;

		/* Ignore uninteresting devices. */
		for (j = 0; j < sizeof(iop_class) / sizeof(iop_class[0]); j++)
			if (iop_class[j].ic_class == ia.ia_class)
				break;
		if (j < sizeof(iop_class) / sizeof(iop_class[0]) &&
		    (iop_class[j].ic_flags & mask) != maskval)
			continue;

		/*
		 * Try to configure the device only if it's not already
		 * configured.
 		 */
 		LIST_FOREACH(ii, &sc->sc_iilist, ii_list) {
 			if (ia.ia_tid == ii->ii_tid) {
				sc->sc_tidmap[i].it_flags |= IT_CONFIGURED;
				strcpy(sc->sc_tidmap[i].it_dvname,
				    ii->ii_dv->dv_xname);
 				break;
			}
		}
		if (ii != NULL)
			continue;

		dv = config_found_sm(&sc->sc_dv, &ia, iop_print, iop_submatch);
		if (dv != NULL) {
 			sc->sc_tidmap[i].it_flags |= IT_CONFIGURED;
			strcpy(sc->sc_tidmap[i].it_dvname, dv->dv_xname);
		}
	}
}

/*
 * Adjust queue parameters for all child devices.
 */
static void
iop_adjqparam(struct iop_softc *sc, int mpi)
{
	struct iop_initiator *ii;

	LIST_FOREACH(ii, &sc->sc_iilist, ii_list)
		if (ii->ii_adjqparam != NULL)
			(*ii->ii_adjqparam)(ii->ii_dv, mpi);
}

static void
iop_devinfo(int class, char *devinfo)
{
#ifdef I2OVERBOSE
	int i;

	for (i = 0; i < sizeof(iop_class) / sizeof(iop_class[0]); i++)
		if (class == iop_class[i].ic_class)
			break;
	
	if (i == sizeof(iop_class) / sizeof(iop_class[0]))
		sprintf(devinfo, "device (class 0x%x)", class);
	else
		strcpy(devinfo, iop_class[i].ic_caption);
#else

	sprintf(devinfo, "device (class 0x%x)", class);
#endif
}

static int
iop_print(void *aux, const char *pnp)
{
	struct iop_attach_args *ia;
	char devinfo[256];

	ia = aux;

	if (pnp != NULL) {
		iop_devinfo(ia->ia_class, devinfo);
		printf("%s at %s", devinfo, pnp);
	}
	printf(" tid %d", ia->ia_tid);
	return (UNCONF);
}

static int
iop_vendor_print(void *aux, const char *pnp)
{

	return (QUIET);
}

static int
iop_submatch(struct device *parent, struct cfdata *cf, void *aux)
{
	struct iop_attach_args *ia;
	
	ia = aux;

	if (cf->iopcf_tid != IOPCF_TID_DEFAULT && cf->iopcf_tid != ia->ia_tid)
		return (0);

	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

/*
 * Shut down all configured IOPs.
 */ 
static void
iop_shutdown(void *junk)
{
	struct iop_softc *sc;
	int i;

	printf("shutting down iop devices...");

	for (i = 0; i < iop_cd.cd_ndevs; i++) {
		if ((sc = device_lookup(&iop_cd, i)) == NULL)
			continue;
		if ((sc->sc_flags & IOP_ONLINE) == 0)
			continue;
		iop_simple_cmd(sc, I2O_TID_IOP, I2O_EXEC_SYS_QUIESCE, IOP_ICTX,
		    0, 5000);
		iop_simple_cmd(sc, I2O_TID_IOP, I2O_EXEC_IOP_CLEAR, IOP_ICTX,
		    0, 1000);
	}

	/* Wait.  Some boards could still be flushing, stupidly enough. */
	delay(5000*1000);
	printf(" done\n");
}

/*
 * Retrieve IOP status.
 */
int
iop_status_get(struct iop_softc *sc, int nosleep)
{
	struct i2o_exec_status_get mf;
	struct i2o_status *st;
	paddr_t pa;
	int rv, i;

	pa = sc->sc_scr_seg->ds_addr;
	st = (struct i2o_status *)sc->sc_scr;

	mf.msgflags = I2O_MSGFLAGS(i2o_exec_status_get);
	mf.msgfunc = I2O_MSGFUNC(I2O_TID_IOP, I2O_EXEC_STATUS_GET);
	mf.reserved[0] = 0;
	mf.reserved[1] = 0;
	mf.reserved[2] = 0;
	mf.reserved[3] = 0;
	mf.addrlow = (u_int32_t)pa;
	mf.addrhigh = (u_int32_t)((u_int64_t)pa >> 32);
	mf.length = sizeof(sc->sc_status);

	memset(st, 0, sizeof(*st));
	bus_dmamap_sync(sc->sc_dmat, sc->sc_scr_dmamap, 0, sizeof(*st),
	    BUS_DMASYNC_PREREAD);

	if ((rv = iop_post(sc, (u_int32_t *)&mf)) != 0)
		return (rv);

	for (i = 25; i != 0; i--) {
		bus_dmamap_sync(sc->sc_dmat, sc->sc_scr_dmamap, 0,
		    sizeof(*st), BUS_DMASYNC_POSTREAD);
		if (st->syncbyte == 0xff)
			break;
		if (nosleep)
			DELAY(100*1000);
		else
			tsleep(iop_status_get, PWAIT, "iopstat", hz / 10);
	}

	if (st->syncbyte != 0xff) {
		printf("%s: STATUS_GET timed out\n", sc->sc_dv.dv_xname);
		rv = EIO;
	} else {
		memcpy(&sc->sc_status, st, sizeof(sc->sc_status));
		rv = 0;
	}

	return (rv);
}

/*
 * Initialize and populate the IOP's outbound FIFO.
 */
static int
iop_ofifo_init(struct iop_softc *sc)
{
	bus_addr_t addr;
	bus_dma_segment_t seg;
	struct i2o_exec_outbound_init *mf;
	int i, rseg, rv;
	u_int32_t mb[IOP_MAX_MSG_SIZE / sizeof(u_int32_t)], *sw;

	sw = (u_int32_t *)sc->sc_scr;

	mf = (struct i2o_exec_outbound_init *)mb;
	mf->msgflags = I2O_MSGFLAGS(i2o_exec_outbound_init);
	mf->msgfunc = I2O_MSGFUNC(I2O_TID_IOP, I2O_EXEC_OUTBOUND_INIT);
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = 0;
	mf->pagesize = NBPG;
	mf->flags = IOP_INIT_CODE | ((sc->sc_framesize >> 2) << 16);

	/*
	 * The I2O spec says that there are two SGLs: one for the status
	 * word, and one for a list of discarded MFAs.  It continues to say
	 * that if you don't want to get the list of MFAs, an IGNORE SGL is
	 * necessary; this isn't the case (and is in fact a bad thing).
	 */
	mb[sizeof(*mf) / sizeof(u_int32_t) + 0] = sizeof(*sw) |
	    I2O_SGL_SIMPLE | I2O_SGL_END_BUFFER | I2O_SGL_END;
	mb[sizeof(*mf) / sizeof(u_int32_t) + 1] =
	    (u_int32_t)sc->sc_scr_seg->ds_addr;
	mb[0] += 2 << 16;

	*sw = 0;
	bus_dmamap_sync(sc->sc_dmat, sc->sc_scr_dmamap, 0, sizeof(*sw),
	    BUS_DMASYNC_PREREAD);

	if ((rv = iop_post(sc, mb)) != 0)
		return (rv);

	POLL(5000,
	    (bus_dmamap_sync(sc->sc_dmat, sc->sc_scr_dmamap, 0, sizeof(*sw),
	    BUS_DMASYNC_POSTREAD),
	    *sw == htole32(I2O_EXEC_OUTBOUND_INIT_COMPLETE)));

	if (*sw != htole32(I2O_EXEC_OUTBOUND_INIT_COMPLETE)) {
		printf("%s: outbound FIFO init failed (%d)\n",
		    sc->sc_dv.dv_xname, le32toh(*sw));
		return (EIO);
	}

	/* Allocate DMA safe memory for the reply frames. */
	if (sc->sc_rep_phys == 0) {
		sc->sc_rep_size = sc->sc_maxob * sc->sc_framesize;

		rv = bus_dmamem_alloc(sc->sc_dmat, sc->sc_rep_size, NBPG,
		    0, &seg, 1, &rseg, BUS_DMA_NOWAIT);
		if (rv != 0) {
			printf("%s: dma alloc = %d\n", sc->sc_dv.dv_xname,
			   rv);
			return (rv);
		}

		rv = bus_dmamem_map(sc->sc_dmat, &seg, rseg, sc->sc_rep_size,
		    &sc->sc_rep, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
		if (rv != 0) {
			printf("%s: dma map = %d\n", sc->sc_dv.dv_xname, rv);
			return (rv);
		}

		rv = bus_dmamap_create(sc->sc_dmat, sc->sc_rep_size, 1,
		    sc->sc_rep_size, 0, BUS_DMA_NOWAIT, &sc->sc_rep_dmamap);
		if (rv != 0) {
			printf("%s: dma create = %d\n", sc->sc_dv.dv_xname,
			    rv);
			return (rv);
		}

		rv = bus_dmamap_load(sc->sc_dmat, sc->sc_rep_dmamap,
		    sc->sc_rep, sc->sc_rep_size, NULL, BUS_DMA_NOWAIT);
		if (rv != 0) {
			printf("%s: dma load = %d\n", sc->sc_dv.dv_xname, rv);
			return (rv);
		}

		sc->sc_rep_phys = sc->sc_rep_dmamap->dm_segs[0].ds_addr;
	}

	/* Populate the outbound FIFO. */
	for (i = sc->sc_maxob, addr = sc->sc_rep_phys; i != 0; i--) {
		iop_outl(sc, IOP_REG_OFIFO, (u_int32_t)addr);
		addr += sc->sc_framesize;
	}

	return (0);
}

/*
 * Read the specified number of bytes from the IOP's hardware resource table.
 */
static int
iop_hrt_get0(struct iop_softc *sc, struct i2o_hrt *hrt, int size)
{
	struct iop_msg *im;
	int rv;
	struct i2o_exec_hrt_get *mf;
	u_int32_t mb[IOP_MAX_MSG_SIZE / sizeof(u_int32_t)];

	im = iop_msg_alloc(sc, IM_WAIT);
	mf = (struct i2o_exec_hrt_get *)mb;
	mf->msgflags = I2O_MSGFLAGS(i2o_exec_hrt_get);
	mf->msgfunc = I2O_MSGFUNC(I2O_TID_IOP, I2O_EXEC_HRT_GET);
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = im->im_tctx;

	iop_msg_map(sc, im, mb, hrt, size, 0, NULL);
	rv = iop_msg_post(sc, im, mb, 30000);
	iop_msg_unmap(sc, im);
	iop_msg_free(sc, im);
	return (rv);
}

/*
 * Read the IOP's hardware resource table.
 */
static int
iop_hrt_get(struct iop_softc *sc)
{
	struct i2o_hrt hrthdr, *hrt;
	int size, rv;

	PHOLD(curproc);
	rv = iop_hrt_get0(sc, &hrthdr, sizeof(hrthdr));
	PRELE(curproc);
	if (rv != 0)
		return (rv);

	DPRINTF(("%s: %d hrt entries\n", sc->sc_dv.dv_xname,
	    le16toh(hrthdr.numentries)));

	size = sizeof(struct i2o_hrt) + 
	    (le16toh(hrthdr.numentries) - 1) * sizeof(struct i2o_hrt_entry);
	hrt = (struct i2o_hrt *)malloc(size, M_DEVBUF, M_NOWAIT);

	if ((rv = iop_hrt_get0(sc, hrt, size)) != 0) {
		free(hrt, M_DEVBUF);
		return (rv);
	}

	if (sc->sc_hrt != NULL)
		free(sc->sc_hrt, M_DEVBUF);
	sc->sc_hrt = hrt;
	return (0);
}

/*
 * Request the specified number of bytes from the IOP's logical
 * configuration table.  If a change indicator is specified, this
 * is a verbatim notification request, so the caller is prepared
 * to wait indefinitely.
 */
static int
iop_lct_get0(struct iop_softc *sc, struct i2o_lct *lct, int size,
	     u_int32_t chgind)
{
	struct iop_msg *im;
	struct i2o_exec_lct_notify *mf;
	int rv;
	u_int32_t mb[IOP_MAX_MSG_SIZE / sizeof(u_int32_t)];

	im = iop_msg_alloc(sc, IM_WAIT);
	memset(lct, 0, size);

	mf = (struct i2o_exec_lct_notify *)mb;
	mf->msgflags = I2O_MSGFLAGS(i2o_exec_lct_notify);
	mf->msgfunc = I2O_MSGFUNC(I2O_TID_IOP, I2O_EXEC_LCT_NOTIFY);
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = im->im_tctx;
	mf->classid = I2O_CLASS_ANY;
	mf->changeindicator = chgind;

#ifdef I2ODEBUG
	printf("iop_lct_get0: reading LCT");
	if (chgind != 0)
		printf(" (async)");
	printf("\n");
#endif

	iop_msg_map(sc, im, mb, lct, size, 0, NULL);
	rv = iop_msg_post(sc, im, mb, (chgind == 0 ? 120*1000 : 0));
	iop_msg_unmap(sc, im);
	iop_msg_free(sc, im);
	return (rv);
}

/*
 * Read the IOP's logical configuration table.
 */
int
iop_lct_get(struct iop_softc *sc)
{
	int esize, size, rv;
	struct i2o_lct *lct;

	esize = le32toh(sc->sc_status.expectedlctsize);
	lct = (struct i2o_lct *)malloc(esize, M_DEVBUF, M_WAITOK);
	if (lct == NULL)
		return (ENOMEM);

	if ((rv = iop_lct_get0(sc, lct, esize, 0)) != 0) {
		free(lct, M_DEVBUF);
		return (rv);
	}

	size = le16toh(lct->tablesize) << 2;
	if (esize != size) {
		free(lct, M_DEVBUF);
		lct = (struct i2o_lct *)malloc(size, M_DEVBUF, M_WAITOK);
		if (lct == NULL)
			return (ENOMEM);

		if ((rv = iop_lct_get0(sc, lct, size, 0)) != 0) {
			free(lct, M_DEVBUF);
			return (rv);
		}
	}

	/* Swap in the new LCT. */
	if (sc->sc_lct != NULL)
		free(sc->sc_lct, M_DEVBUF);
	sc->sc_lct = lct;
	sc->sc_nlctent = ((le16toh(sc->sc_lct->tablesize) << 2) -
	    sizeof(struct i2o_lct) + sizeof(struct i2o_lct_entry)) /
	    sizeof(struct i2o_lct_entry);
	return (0);
}

/*
 * Request the specified parameter group from the target.  If an initiator
 * is specified (a) don't wait for the operation to complete, but instead
 * let the initiator's interrupt handler deal with the reply and (b) place a
 * pointer to the parameter group op in the wrapper's `im_dvcontext' field.
 */
int
iop_field_get_all(struct iop_softc *sc, int tid, int group, void *buf,
		  int size, struct iop_initiator *ii)
{
	struct iop_msg *im;
	struct i2o_util_params_op *mf;
	struct i2o_reply *rf;
	int rv;
	struct iop_pgop *pgop;
	u_int32_t mb[IOP_MAX_MSG_SIZE / sizeof(u_int32_t)];

	im = iop_msg_alloc(sc, (ii == NULL ? IM_WAIT : 0) | IM_NOSTATUS);
	if ((pgop = malloc(sizeof(*pgop), M_DEVBUF, M_WAITOK)) == NULL) {
		iop_msg_free(sc, im);
		return (ENOMEM);
	}
	if ((rf = malloc(sizeof(*rf), M_DEVBUF, M_WAITOK)) == NULL) {
		iop_msg_free(sc, im);
		free(pgop, M_DEVBUF);
		return (ENOMEM);
	}
	im->im_dvcontext = pgop;
	im->im_rb = rf;

	mf = (struct i2o_util_params_op *)mb;
	mf->msgflags = I2O_MSGFLAGS(i2o_util_params_op);
	mf->msgfunc = I2O_MSGFUNC(tid, I2O_UTIL_PARAMS_GET);
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = im->im_tctx;
	mf->flags = 0;

	pgop->olh.count = htole16(1);
	pgop->olh.reserved = htole16(0);
	pgop->oat.operation = htole16(I2O_PARAMS_OP_FIELD_GET);
	pgop->oat.fieldcount = htole16(0xffff);
	pgop->oat.group = htole16(group);

	if (ii == NULL)
		PHOLD(curproc);

	memset(buf, 0, size);
	iop_msg_map(sc, im, mb, pgop, sizeof(*pgop), 1, NULL);
	iop_msg_map(sc, im, mb, buf, size, 0, NULL);
	rv = iop_msg_post(sc, im, mb, (ii == NULL ? 30000 : 0));

	if (ii == NULL)
		PRELE(curproc);

	/* Detect errors; let partial transfers to count as success. */
	if (ii == NULL && rv == 0) {
		if (rf->reqstatus == I2O_STATUS_ERROR_PARTIAL_XFER &&
		    le16toh(rf->detail) == I2O_DSC_UNKNOWN_ERROR)
			rv = 0;
		else
			rv = (rf->reqstatus != 0 ? EIO : 0);

		if (rv != 0)
			printf("%s: FIELD_GET failed for tid %d group %d\n",
			    sc->sc_dv.dv_xname, tid, group);
	}

	if (ii == NULL || rv != 0) {
		iop_msg_unmap(sc, im);
		iop_msg_free(sc, im);
		free(pgop, M_DEVBUF);
		free(rf, M_DEVBUF);
	}

	return (rv);
}

/*
 * Set a single field in a scalar parameter group.
 */
int
iop_field_set(struct iop_softc *sc, int tid, int group, void *buf,
	      int size, int field)
{
	struct iop_msg *im;
	struct i2o_util_params_op *mf;
	struct iop_pgop *pgop;
	int rv, totsize;
	u_int32_t mb[IOP_MAX_MSG_SIZE / sizeof(u_int32_t)];

	totsize = sizeof(*pgop) + size;

	im = iop_msg_alloc(sc, IM_WAIT);
	if ((pgop = malloc(totsize, M_DEVBUF, M_WAITOK)) == NULL) {
		iop_msg_free(sc, im);
		return (ENOMEM);
	}

	mf = (struct i2o_util_params_op *)mb;
	mf->msgflags = I2O_MSGFLAGS(i2o_util_params_op);
	mf->msgfunc = I2O_MSGFUNC(tid, I2O_UTIL_PARAMS_SET);
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = im->im_tctx;
	mf->flags = 0;

	pgop->olh.count = htole16(1);
	pgop->olh.reserved = htole16(0);
	pgop->oat.operation = htole16(I2O_PARAMS_OP_FIELD_SET);
	pgop->oat.fieldcount = htole16(1);
	pgop->oat.group = htole16(group);
	pgop->oat.fields[0] = htole16(field);
	memcpy(pgop + 1, buf, size);

	iop_msg_map(sc, im, mb, pgop, totsize, 1, NULL);
	rv = iop_msg_post(sc, im, mb, 30000);
	if (rv != 0)
		printf("%s: FIELD_SET failed for tid %d group %d\n",
		    sc->sc_dv.dv_xname, tid, group);

	iop_msg_unmap(sc, im);
	iop_msg_free(sc, im);
	free(pgop, M_DEVBUF);
	return (rv);
}

/*
 * Delete all rows in a tablular parameter group.
 */
int
iop_table_clear(struct iop_softc *sc, int tid, int group)
{
	struct iop_msg *im;
	struct i2o_util_params_op *mf;
	struct iop_pgop pgop;
	u_int32_t mb[IOP_MAX_MSG_SIZE / sizeof(u_int32_t)];
	int rv;

	im = iop_msg_alloc(sc, IM_WAIT);

	mf = (struct i2o_util_params_op *)mb;
	mf->msgflags = I2O_MSGFLAGS(i2o_util_params_op);
	mf->msgfunc = I2O_MSGFUNC(tid, I2O_UTIL_PARAMS_SET);
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = im->im_tctx;
	mf->flags = 0;

	pgop.olh.count = htole16(1);
	pgop.olh.reserved = htole16(0);
	pgop.oat.operation = htole16(I2O_PARAMS_OP_TABLE_CLEAR);
	pgop.oat.fieldcount = htole16(0);
	pgop.oat.group = htole16(group);
	pgop.oat.fields[0] = htole16(0);

	PHOLD(curproc);
	iop_msg_map(sc, im, mb, &pgop, sizeof(pgop), 1, NULL);
	rv = iop_msg_post(sc, im, mb, 30000);
	if (rv != 0)
		printf("%s: TABLE_CLEAR failed for tid %d group %d\n",
		    sc->sc_dv.dv_xname, tid, group);

	iop_msg_unmap(sc, im);
	PRELE(curproc);
	iop_msg_free(sc, im);
	return (rv);
}

/*
 * Add a single row to a tabular parameter group.  The row can have only one
 * field.
 */
int
iop_table_add_row(struct iop_softc *sc, int tid, int group, void *buf,
		  int size, int row)
{
	struct iop_msg *im;
	struct i2o_util_params_op *mf;
	struct iop_pgop *pgop;
	int rv, totsize;
	u_int32_t mb[IOP_MAX_MSG_SIZE / sizeof(u_int32_t)];

	totsize = sizeof(*pgop) + sizeof(u_int16_t) * 2 + size;

	im = iop_msg_alloc(sc, IM_WAIT);
	if ((pgop = malloc(totsize, M_DEVBUF, M_WAITOK)) == NULL) {
		iop_msg_free(sc, im);
		return (ENOMEM);
	}

	mf = (struct i2o_util_params_op *)mb;
	mf->msgflags = I2O_MSGFLAGS(i2o_util_params_op);
	mf->msgfunc = I2O_MSGFUNC(tid, I2O_UTIL_PARAMS_SET);
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = im->im_tctx;
	mf->flags = 0;

	pgop->olh.count = htole16(1);
	pgop->olh.reserved = htole16(0);
	pgop->oat.operation = htole16(I2O_PARAMS_OP_ROW_ADD);
	pgop->oat.fieldcount = htole16(1);
	pgop->oat.group = htole16(group);
	pgop->oat.fields[0] = htole16(0);	/* FieldIdx */
	pgop->oat.fields[1] = htole16(1);	/* RowCount */
	pgop->oat.fields[2] = htole16(row);	/* KeyValue */
	memcpy(&pgop->oat.fields[3], buf, size);

	iop_msg_map(sc, im, mb, pgop, totsize, 1, NULL);
	rv = iop_msg_post(sc, im, mb, 30000);
	if (rv != 0)
		printf("%s: ADD_ROW failed for tid %d group %d row %d\n",
		    sc->sc_dv.dv_xname, tid, group, row);

	iop_msg_unmap(sc, im);
	iop_msg_free(sc, im);
	free(pgop, M_DEVBUF);
	return (rv);
}

/*
 * Execute a simple command (no parameters).
 */
int
iop_simple_cmd(struct iop_softc *sc, int tid, int function, int ictx,
	       int async, int timo)
{
	struct iop_msg *im;
	struct i2o_msg mf;
	int rv, fl;

	fl = (async != 0 ? IM_WAIT : IM_POLL);
	im = iop_msg_alloc(sc, fl);

	mf.msgflags = I2O_MSGFLAGS(i2o_msg);
	mf.msgfunc = I2O_MSGFUNC(tid, function);
	mf.msgictx = ictx;
	mf.msgtctx = im->im_tctx;

	rv = iop_msg_post(sc, im, &mf, timo);
	iop_msg_free(sc, im);
	return (rv);
}

/*
 * Post the system table to the IOP.
 */
static int
iop_systab_set(struct iop_softc *sc)
{
	struct i2o_exec_sys_tab_set *mf;
	struct iop_msg *im;
	bus_space_handle_t bsh;
	bus_addr_t boo;
	u_int32_t mema[2], ioa[2];
	int rv;
	u_int32_t mb[IOP_MAX_MSG_SIZE / sizeof(u_int32_t)];

	im = iop_msg_alloc(sc, IM_WAIT);

	mf = (struct i2o_exec_sys_tab_set *)mb;
	mf->msgflags = I2O_MSGFLAGS(i2o_exec_sys_tab_set);
	mf->msgfunc = I2O_MSGFUNC(I2O_TID_IOP, I2O_EXEC_SYS_TAB_SET);
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = im->im_tctx;
	mf->iopid = (sc->sc_dv.dv_unit + 2) << 12;
	mf->segnumber = 0;

	mema[1] = sc->sc_status.desiredprivmemsize;
	ioa[1] = sc->sc_status.desiredpriviosize;

	if (mema[1] != 0) {
		rv = bus_space_alloc(sc->sc_bus_memt, 0, 0xffffffff,
		    le32toh(mema[1]), NBPG, 0, 0, &boo, &bsh);
		mema[0] = htole32(boo);
		if (rv != 0) {
			printf("%s: can't alloc priv mem space, err = %d\n",
			    sc->sc_dv.dv_xname, rv);
			mema[0] = 0;
			mema[1] = 0;
		}
	}

	if (ioa[1] != 0) {
		rv = bus_space_alloc(sc->sc_bus_iot, 0, 0xffff,
		    le32toh(ioa[1]), 0, 0, 0, &boo, &bsh);
		ioa[0] = htole32(boo);
		if (rv != 0) {
			printf("%s: can't alloc priv i/o space, err = %d\n",
			    sc->sc_dv.dv_xname, rv);
			ioa[0] = 0;
			ioa[1] = 0;
		}
	}

	PHOLD(curproc);
	iop_msg_map(sc, im, mb, iop_systab, iop_systab_size, 1, NULL);
	iop_msg_map(sc, im, mb, mema, sizeof(mema), 1, NULL);
	iop_msg_map(sc, im, mb, ioa, sizeof(ioa), 1, NULL);
	rv = iop_msg_post(sc, im, mb, 5000);
	iop_msg_unmap(sc, im);
	iop_msg_free(sc, im);
	PRELE(curproc);
	return (rv);
}

/*
 * Reset the IOP.  Must be called with interrupts disabled.
 */
static int
iop_reset(struct iop_softc *sc)
{
	u_int32_t mfa, *sw;
	struct i2o_exec_iop_reset mf;
	int rv;
	paddr_t pa;

	sw = (u_int32_t *)sc->sc_scr;
	pa = sc->sc_scr_seg->ds_addr;

	mf.msgflags = I2O_MSGFLAGS(i2o_exec_iop_reset);
	mf.msgfunc = I2O_MSGFUNC(I2O_TID_IOP, I2O_EXEC_IOP_RESET);
	mf.reserved[0] = 0;
	mf.reserved[1] = 0;
	mf.reserved[2] = 0;
	mf.reserved[3] = 0;
	mf.statuslow = (u_int32_t)pa;
	mf.statushigh = (u_int32_t)((u_int64_t)pa >> 32);

	*sw = htole32(0);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_scr_dmamap, 0, sizeof(*sw),
	    BUS_DMASYNC_PREREAD);

	if ((rv = iop_post(sc, (u_int32_t *)&mf)))
		return (rv);

	POLL(2500,
	    (bus_dmamap_sync(sc->sc_dmat, sc->sc_scr_dmamap, 0, sizeof(*sw),
	    BUS_DMASYNC_POSTREAD), *sw != 0));
	if (*sw != htole32(I2O_RESET_IN_PROGRESS)) {
		printf("%s: reset rejected, status 0x%x\n",
		    sc->sc_dv.dv_xname, le32toh(*sw));
		return (EIO);
	}

	/* 
	 * IOP is now in the INIT state.  Wait no more than 10 seconds for
	 * the inbound queue to become responsive.
	 */
	POLL(10000, (mfa = iop_inl(sc, IOP_REG_IFIFO)) != IOP_MFA_EMPTY);
	if (mfa == IOP_MFA_EMPTY) {
		printf("%s: reset failed\n", sc->sc_dv.dv_xname);
		return (EIO);
	}

	iop_release_mfa(sc, mfa);
	return (0);
}

/*
 * Register a new initiator.  Must be called with the configuration lock
 * held.
 */
void
iop_initiator_register(struct iop_softc *sc, struct iop_initiator *ii)
{
	static int ictxgen;
	int s;

	/* 0 is reserved (by us) for system messages. */
	ii->ii_ictx = ++ictxgen;

	/*
	 * `Utility initiators' don't make it onto the per-IOP initiator list
	 * (which is used only for configuration), but do get one slot on
	 * the inbound queue.
	 */
	if ((ii->ii_flags & II_UTILITY) == 0) {
		LIST_INSERT_HEAD(&sc->sc_iilist, ii, ii_list);
		sc->sc_nii++;
	} else
		sc->sc_nuii++;

	s = splbio();
	LIST_INSERT_HEAD(IOP_ICTXHASH(ii->ii_ictx), ii, ii_hash);
	splx(s);
}

/*
 * Unregister an initiator.  Must be called with the configuration lock
 * held.
 */
void
iop_initiator_unregister(struct iop_softc *sc, struct iop_initiator *ii)
{
	int s;

	if ((ii->ii_flags & II_UTILITY) == 0) {
		LIST_REMOVE(ii, ii_list);
		sc->sc_nii--;
	} else
		sc->sc_nuii--;

	s = splbio();
	LIST_REMOVE(ii, ii_hash);
	splx(s);
}

/*
 * Handle a reply frame from the IOP.
 */
static int
iop_handle_reply(struct iop_softc *sc, u_int32_t rmfa)
{
	struct iop_msg *im;
	struct i2o_reply *rb;
	struct i2o_fault_notify *fn;
	struct iop_initiator *ii;
	u_int off, ictx, tctx, status, size;

	off = (int)(rmfa - sc->sc_rep_phys);
	rb = (struct i2o_reply *)(sc->sc_rep + off);

	/* Perform reply queue DMA synchronisation. */
	bus_dmamap_sync(sc->sc_dmat, sc->sc_rep_dmamap, off,
	    sc->sc_framesize, BUS_DMASYNC_POSTREAD);
	if (--sc->sc_curib != 0)
		bus_dmamap_sync(sc->sc_dmat, sc->sc_rep_dmamap,
		    0, sc->sc_rep_size, BUS_DMASYNC_PREREAD);

#ifdef I2ODEBUG
	if ((le32toh(rb->msgflags) & I2O_MSGFLAGS_64BIT) != 0)
		panic("iop_handle_reply: 64-bit reply");
#endif
	/* 
	 * Find the initiator.
	 */
	ictx = le32toh(rb->msgictx);
	if (ictx == IOP_ICTX)
		ii = NULL;
	else {
		ii = LIST_FIRST(IOP_ICTXHASH(ictx));
		for (; ii != NULL; ii = LIST_NEXT(ii, ii_hash))
			if (ii->ii_ictx == ictx)
				break;
		if (ii == NULL) {
#ifdef I2ODEBUG
			iop_reply_print(sc, rb);
#endif
			printf("%s: WARNING: bad ictx returned (%x)\n",
			    sc->sc_dv.dv_xname, ictx);
			return (-1);
		}
	}

	/*
	 * If we received a transport failure notice, we've got to dig the
	 * transaction context (if any) out of the original message frame,
	 * and then release the original MFA back to the inbound FIFO.
	 */
	if ((rb->msgflags & I2O_MSGFLAGS_FAIL) != 0) {
		status = I2O_STATUS_SUCCESS;

		fn = (struct i2o_fault_notify *)rb;
		tctx = iop_inl(sc, fn->lowmfa + 12);
		iop_release_mfa(sc, fn->lowmfa);
		iop_tfn_print(sc, fn);
	} else {
		status = rb->reqstatus;
		tctx = le32toh(rb->msgtctx);
	}

	if (ii == NULL || (ii->ii_flags & II_NOTCTX) == 0) {
		/*
		 * This initiator tracks state using message wrappers.
		 *
		 * Find the originating message wrapper, and if requested
		 * notify the initiator.
		 */
		im = sc->sc_ims + (tctx & IOP_TCTX_MASK);
		if ((tctx & IOP_TCTX_MASK) > sc->sc_maxib ||
		    (im->im_flags & IM_ALLOCED) == 0 ||
		    tctx != im->im_tctx) {
			printf("%s: WARNING: bad tctx returned (0x%08x, %p)\n",
			    sc->sc_dv.dv_xname, tctx, im);
			if (im != NULL)
				printf("%s: flags=0x%08x tctx=0x%08x\n",
				    sc->sc_dv.dv_xname, im->im_flags,
				    im->im_tctx);
#ifdef I2ODEBUG
			if ((rb->msgflags & I2O_MSGFLAGS_FAIL) == 0)
				iop_reply_print(sc, rb);
#endif
			return (-1);
		}

		if ((rb->msgflags & I2O_MSGFLAGS_FAIL) != 0)
			im->im_flags |= IM_FAIL;

#ifdef I2ODEBUG
		if ((im->im_flags & IM_REPLIED) != 0)
			panic("%s: dup reply", sc->sc_dv.dv_xname);
#endif
		im->im_flags |= IM_REPLIED;

#ifdef I2ODEBUG
		if (status != I2O_STATUS_SUCCESS)
			iop_reply_print(sc, rb);
#endif
		im->im_reqstatus = status;

		/* Copy the reply frame, if requested. */
		if (im->im_rb != NULL) {
			size = (le32toh(rb->msgflags) >> 14) & ~3;
#ifdef I2ODEBUG
			if (size > sc->sc_framesize)
				panic("iop_handle_reply: reply too large");
#endif
			memcpy(im->im_rb, rb, size);
		}

		/* Notify the initiator. */
		if ((im->im_flags & IM_WAIT) != 0)
			wakeup(im);
		else if ((im->im_flags & (IM_POLL | IM_POLL_INTR)) != IM_POLL)
			(*ii->ii_intr)(ii->ii_dv, im, rb);
	} else {
		/*
		 * This initiator discards message wrappers.
		 *
		 * Simply pass the reply frame to the initiator.
		 */
		(*ii->ii_intr)(ii->ii_dv, NULL, rb);
	}

	return (status);
}

/*
 * Handle an interrupt from the IOP.
 */
int
iop_intr(void *arg)
{
	struct iop_softc *sc;
	u_int32_t rmfa;

	sc = arg;

	if ((iop_inl(sc, IOP_REG_INTR_STATUS) & IOP_INTR_OFIFO) == 0)
		return (0);

	for (;;) {
		/* Double read to account for IOP bug. */
		if ((rmfa = iop_inl(sc, IOP_REG_OFIFO)) == IOP_MFA_EMPTY) {
			rmfa = iop_inl(sc, IOP_REG_OFIFO);
			if (rmfa == IOP_MFA_EMPTY)
				break;
		}
		iop_handle_reply(sc, rmfa);
		iop_outl(sc, IOP_REG_OFIFO, rmfa);
	}

	return (1);
}

/*
 * Handle an event signalled by the executive.
 */
static void
iop_intr_event(struct device *dv, struct iop_msg *im, void *reply)
{
	struct i2o_util_event_register_reply *rb;
	struct iop_softc *sc;
	u_int event;

	sc = (struct iop_softc *)dv;
	rb = reply;

	if ((rb->msgflags & I2O_MSGFLAGS_FAIL) != 0)
		return;

	event = le32toh(rb->event);
	printf("%s: event 0x%08x received\n", dv->dv_xname, event);
}

/* 
 * Allocate a message wrapper.
 */
struct iop_msg *
iop_msg_alloc(struct iop_softc *sc, int flags)
{
	struct iop_msg *im;
	static u_int tctxgen;
	int s, i;

#ifdef I2ODEBUG
	if ((flags & IM_SYSMASK) != 0)
		panic("iop_msg_alloc: system flags specified");
#endif

	s = splbio();
	im = SLIST_FIRST(&sc->sc_im_freelist);
#if defined(DIAGNOSTIC) || defined(I2ODEBUG)
	if (im == NULL)
		panic("iop_msg_alloc: no free wrappers");
#endif
	SLIST_REMOVE_HEAD(&sc->sc_im_freelist, im_chain);
	splx(s);

	im->im_tctx = (im->im_tctx & IOP_TCTX_MASK) | tctxgen;
	tctxgen += (1 << IOP_TCTX_SHIFT);
	im->im_flags = flags | IM_ALLOCED;
	im->im_rb = NULL;
	i = 0;
	do {
		im->im_xfer[i++].ix_size = 0;
	} while (i < IOP_MAX_MSG_XFERS);

	return (im);
}

/* 
 * Free a message wrapper.
 */
void
iop_msg_free(struct iop_softc *sc, struct iop_msg *im)
{
	int s;

#ifdef I2ODEBUG
	if ((im->im_flags & IM_ALLOCED) == 0)
		panic("iop_msg_free: wrapper not allocated");
#endif

	im->im_flags = 0;
	s = splbio();
	SLIST_INSERT_HEAD(&sc->sc_im_freelist, im, im_chain);
	splx(s);
}

/*
 * Map a data transfer.  Write a scatter-gather list into the message frame. 
 */
int
iop_msg_map(struct iop_softc *sc, struct iop_msg *im, u_int32_t *mb,
	    void *xferaddr, int xfersize, int out, struct proc *up)
{
	bus_dmamap_t dm;
	bus_dma_segment_t *ds;
	struct iop_xfer *ix;
	u_int rv, i, nsegs, flg, off, xn;
	u_int32_t *p;

	for (xn = 0, ix = im->im_xfer; xn < IOP_MAX_MSG_XFERS; xn++, ix++)
		if (ix->ix_size == 0)
			break;

#ifdef I2ODEBUG
	if (xfersize == 0)
		panic("iop_msg_map: null transfer");
	if (xfersize > IOP_MAX_XFER)
		panic("iop_msg_map: transfer too large");
	if (xn == IOP_MAX_MSG_XFERS)
		panic("iop_msg_map: too many xfers");
#endif

	/*
	 * Only the first DMA map is static.
	 */
	if (xn != 0) {
		rv = bus_dmamap_create(sc->sc_dmat, IOP_MAX_XFER,
		    IOP_MAX_SEGS, IOP_MAX_XFER, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &ix->ix_map);
		if (rv != 0)
			return (rv);
	}

	dm = ix->ix_map;
	rv = bus_dmamap_load(sc->sc_dmat, dm, xferaddr, xfersize, up,
	    (up == NULL ? BUS_DMA_NOWAIT : 0));
	if (rv != 0)
		goto bad;

	/*
	 * How many SIMPLE SG elements can we fit in this message?
	 */
	off = mb[0] >> 16;
	p = mb + off;
	nsegs = ((sc->sc_framesize >> 2) - off) >> 1;

	if (dm->dm_nsegs > nsegs) {
		bus_dmamap_unload(sc->sc_dmat, ix->ix_map);
		rv = EFBIG;
		DPRINTF(("iop_msg_map: too many segs\n"));
		goto bad;
	}

	nsegs = dm->dm_nsegs;
	xfersize = 0;

	/*
	 * Write out the SG list.
	 */
	if (out)
		flg = I2O_SGL_SIMPLE | I2O_SGL_DATA_OUT;
	else
		flg = I2O_SGL_SIMPLE;

	for (i = nsegs, ds = dm->dm_segs; i > 1; i--, p += 2, ds++) {
		p[0] = (u_int32_t)ds->ds_len | flg;
		p[1] = (u_int32_t)ds->ds_addr;
		xfersize += ds->ds_len;
	}

	p[0] = (u_int32_t)ds->ds_len | flg | I2O_SGL_END_BUFFER;
	p[1] = (u_int32_t)ds->ds_addr;
	xfersize += ds->ds_len;

	/* Fix up the transfer record, and sync the map. */
	ix->ix_flags = (out ? IX_OUT : IX_IN);
	ix->ix_size = xfersize;
	bus_dmamap_sync(sc->sc_dmat, ix->ix_map, 0, xfersize,
	    out ? BUS_DMASYNC_POSTWRITE : BUS_DMASYNC_POSTREAD);

	/*
	 * If this is the first xfer we've mapped for this message, adjust
	 * the SGL offset field in the message header.
	 */
	if ((im->im_flags & IM_SGLOFFADJ) == 0) {
		mb[0] += (mb[0] >> 12) & 0xf0;
		im->im_flags |= IM_SGLOFFADJ;
	}
	mb[0] += (nsegs << 17);
	return (0);

 bad:
 	if (xn != 0)
		bus_dmamap_destroy(sc->sc_dmat, ix->ix_map);
	return (rv);
}

/*
 * Map a block I/O data transfer (different in that there's only one per
 * message maximum, and PAGE addressing may be used).  Write a scatter
 * gather list into the message frame.
 */
int
iop_msg_map_bio(struct iop_softc *sc, struct iop_msg *im, u_int32_t *mb,
		void *xferaddr, int xfersize, int out)
{
	bus_dma_segment_t *ds;
	bus_dmamap_t dm;
	struct iop_xfer *ix;
	u_int rv, i, nsegs, off, slen, tlen, flg;
	paddr_t saddr, eaddr;
	u_int32_t *p;

#ifdef I2ODEBUG
	if (xfersize == 0)
		panic("iop_msg_map_bio: null transfer");
	if (xfersize > IOP_MAX_XFER)
		panic("iop_msg_map_bio: transfer too large");
	if ((im->im_flags & IM_SGLOFFADJ) != 0)
		panic("iop_msg_map_bio: SGLOFFADJ");
#endif

	ix = im->im_xfer;
	dm = ix->ix_map;
	rv = bus_dmamap_load(sc->sc_dmat, dm, xferaddr, xfersize, NULL,
	    BUS_DMA_NOWAIT);
	if (rv != 0)
		return (rv);

	off = mb[0] >> 16;
	nsegs = ((sc->sc_framesize >> 2) - off) >> 1;

	/*
	 * If the transfer is highly fragmented and won't fit using SIMPLE
	 * elements, use PAGE_LIST elements instead.  SIMPLE elements are
	 * potentially more efficient, both for us and the IOP.
	 */
	if (dm->dm_nsegs > nsegs) {
		nsegs = 1;
		p = mb + off + 1;

		/* XXX This should be done with a bus_space flag. */
		for (i = dm->dm_nsegs, ds = dm->dm_segs; i > 0; i--, ds++) {
			slen = ds->ds_len;
			saddr = ds->ds_addr;

			while (slen > 0) {
				eaddr = (saddr + NBPG) & ~(NBPG - 1);
				tlen = min(eaddr - saddr, slen);
				slen -= tlen;
				*p++ = le32toh(saddr);
				saddr = eaddr;
				nsegs++;
			}
		}

		mb[off] = xfersize | I2O_SGL_PAGE_LIST | I2O_SGL_END_BUFFER |
		    I2O_SGL_END;
		if (out)
			mb[off] |= I2O_SGL_DATA_OUT;
	} else {
		p = mb + off;
		nsegs = dm->dm_nsegs;

		if (out)
			flg = I2O_SGL_SIMPLE | I2O_SGL_DATA_OUT;
		else
			flg = I2O_SGL_SIMPLE;

		for (i = nsegs, ds = dm->dm_segs; i > 1; i--, p += 2, ds++) {
			p[0] = (u_int32_t)ds->ds_len | flg;
			p[1] = (u_int32_t)ds->ds_addr;
		}

		p[0] = (u_int32_t)ds->ds_len | flg | I2O_SGL_END_BUFFER |
		    I2O_SGL_END;
		p[1] = (u_int32_t)ds->ds_addr;
		nsegs <<= 1;
	}

	/* Fix up the transfer record, and sync the map. */
	ix->ix_flags = (out ? IX_OUT : IX_IN);
	ix->ix_size = xfersize;
	bus_dmamap_sync(sc->sc_dmat, ix->ix_map, 0, xfersize,
	    out ? BUS_DMASYNC_POSTWRITE : BUS_DMASYNC_POSTREAD);

	/*
	 * Adjust the SGL offset and total message size fields.  We don't
	 * set IM_SGLOFFADJ, since it's used only for SIMPLE elements.
	 */
	mb[0] += ((off << 4) + (nsegs << 16));
	return (0);
}

/*
 * Unmap all data transfers associated with a message wrapper.
 */
void
iop_msg_unmap(struct iop_softc *sc, struct iop_msg *im)
{
	struct iop_xfer *ix;
	int i;

#ifdef I2ODEBUG	
	if (im->im_xfer[0].ix_size == 0)
		panic("iop_msg_unmap: no transfers mapped");
#endif

	for (ix = im->im_xfer, i = 0;;) {
		bus_dmamap_sync(sc->sc_dmat, ix->ix_map, 0, ix->ix_size,
		    ix->ix_flags & IX_OUT ? BUS_DMASYNC_POSTWRITE :
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, ix->ix_map);

		/* Only the first DMA map is static. */
		if (i != 0)
			bus_dmamap_destroy(sc->sc_dmat, ix->ix_map);
		if ((++ix)->ix_size == 0) 
			break;
		if (++i >= IOP_MAX_MSG_XFERS)
			break;
	}
}

/*
 * Post a message frame to the IOP's inbound queue.
 */
int
iop_post(struct iop_softc *sc, u_int32_t *mb)
{
	u_int32_t mfa;
	int s;

#ifdef I2ODEBUG
	if ((mb[0] >> 16) > (sc->sc_framesize >> 2))
		panic("iop_post: frame too large");
#endif

	s = splbio();

	/* Allocate a slot with the IOP. */
	if ((mfa = iop_inl(sc, IOP_REG_IFIFO)) == IOP_MFA_EMPTY)
		if ((mfa = iop_inl(sc, IOP_REG_IFIFO)) == IOP_MFA_EMPTY) {
			splx(s);
			printf("%s: mfa not forthcoming\n",
			    sc->sc_dv.dv_xname);
			return (EAGAIN);
		}

	/* Perform reply buffer DMA synchronisation. */
	if (sc->sc_curib++ == 0)
		bus_dmamap_sync(sc->sc_dmat, sc->sc_rep_dmamap, 0,
		    sc->sc_rep_size, BUS_DMASYNC_PREREAD);

	/* Copy out the message frame. */
	bus_space_write_region_4(sc->sc_iot, sc->sc_ioh, mfa, mb, mb[0] >> 16);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, mfa, (mb[0] >> 14) & ~3,
	    BUS_SPACE_BARRIER_WRITE);

	/* Post the MFA back to the IOP. */
	iop_outl(sc, IOP_REG_IFIFO, mfa);

	splx(s);
	return (0);
}

/*
 * Post a message to the IOP and deal with completion.
 */
int
iop_msg_post(struct iop_softc *sc, struct iop_msg *im, void *xmb, int timo)
{
	u_int32_t *mb;
	int rv, s;

	mb = xmb;

	/* Terminate the scatter/gather list chain. */
	if ((im->im_flags & IM_SGLOFFADJ) != 0)
		mb[(mb[0] >> 16) - 2] |= I2O_SGL_END;

	if ((rv = iop_post(sc, mb)) != 0)
		return (rv);

	if ((im->im_flags & (IM_POLL | IM_WAIT)) != 0) {
		if ((im->im_flags & IM_POLL) != 0)
			iop_msg_poll(sc, im, timo);
		else
			iop_msg_wait(sc, im, timo);

		s = splbio();
		if ((im->im_flags & IM_REPLIED) != 0) {
			if ((im->im_flags & IM_NOSTATUS) != 0)
				rv = 0;
			else if ((im->im_flags & IM_FAIL) != 0)
				rv = ENXIO;
			else if (im->im_reqstatus != I2O_STATUS_SUCCESS)
				rv = EIO;
			else
				rv = 0;
		} else
			rv = EBUSY;
		splx(s);
	} else
		rv = 0;

	return (rv);
}

/* 
 * Spin until the specified message is replied to.
 */
static void
iop_msg_poll(struct iop_softc *sc, struct iop_msg *im, int timo)
{
	u_int32_t rmfa;
	int s, status;

	s = splbio();

	/* Wait for completion. */
	for (timo *= 10; timo != 0; timo--) {
		if ((iop_inl(sc, IOP_REG_INTR_STATUS) & IOP_INTR_OFIFO) != 0) {
			/* Double read to account for IOP bug. */
			rmfa = iop_inl(sc, IOP_REG_OFIFO);
			if (rmfa == IOP_MFA_EMPTY)
				rmfa = iop_inl(sc, IOP_REG_OFIFO);
			if (rmfa != IOP_MFA_EMPTY) {
				status = iop_handle_reply(sc, rmfa);

				/*
				 * Return the reply frame to the IOP's
				 * outbound FIFO.
				 */
				iop_outl(sc, IOP_REG_OFIFO, rmfa);
			}
		}
		if ((im->im_flags & IM_REPLIED) != 0)
			break;
		DELAY(100);
	}

	if (timo == 0) {
#ifdef I2ODEBUG
		printf("%s: poll - no reply\n", sc->sc_dv.dv_xname);
		if (iop_status_get(sc, 1) != 0)
			printf("iop_msg_poll: unable to retrieve status\n");
		else
			printf("iop_msg_poll: IOP state = %d\n",
			    (le32toh(sc->sc_status.segnumber) >> 16) & 0xff); 
#endif
	}

	splx(s);
}

/*
 * Sleep until the specified message is replied to.
 */
static void
iop_msg_wait(struct iop_softc *sc, struct iop_msg *im, int timo)
{
	int s, rv;

	s = splbio();
	if ((im->im_flags & IM_REPLIED) != 0) {
		splx(s);
		return;
	}
	rv = tsleep(im, PRIBIO, "iopmsg", timo * hz / 1000);
	splx(s);

#ifdef I2ODEBUG
	if (rv != 0) {
		printf("iop_msg_wait: tsleep() == %d\n", rv);
		if (iop_status_get(sc, 0) != 0)
			printf("iop_msg_wait: unable to retrieve status\n");
		else
			printf("iop_msg_wait: IOP state = %d\n",
			    (le32toh(sc->sc_status.segnumber) >> 16) & 0xff); 
	}
#endif
}

/*
 * Release an unused message frame back to the IOP's inbound fifo.
 */
static void
iop_release_mfa(struct iop_softc *sc, u_int32_t mfa)
{

	/* Use the frame to issue a no-op. */
	iop_outl(sc, mfa, I2O_VERSION_11 | (4 << 16));
	iop_outl(sc, mfa + 4, I2O_MSGFUNC(I2O_TID_IOP, I2O_UTIL_NOP));
	iop_outl(sc, mfa + 8, 0);
	iop_outl(sc, mfa + 12, 0);

	iop_outl(sc, IOP_REG_IFIFO, mfa);
}

#ifdef I2ODEBUG
/*
 * Dump a reply frame header.
 */
static void
iop_reply_print(struct iop_softc *sc, struct i2o_reply *rb)
{
	u_int function, detail;
#ifdef I2OVERBOSE
	const char *statusstr;
#endif

	function = (le32toh(rb->msgfunc) >> 24) & 0xff;
	detail = le16toh(rb->detail);

	printf("%s: reply:\n", sc->sc_dv.dv_xname);

#ifdef I2OVERBOSE
	if (rb->reqstatus < sizeof(iop_status) / sizeof(iop_status[0]))
		statusstr = iop_status[rb->reqstatus];
	else
		statusstr = "undefined error code";

	printf("%s:   function=0x%02x status=0x%02x (%s)\n", 
	    sc->sc_dv.dv_xname, function, rb->reqstatus, statusstr);
#else
	printf("%s:   function=0x%02x status=0x%02x\n", 
	    sc->sc_dv.dv_xname, function, rb->reqstatus);
#endif
	printf("%s:   detail=0x%04x ictx=0x%08x tctx=0x%08x\n",
	    sc->sc_dv.dv_xname, detail, le32toh(rb->msgictx),
	    le32toh(rb->msgtctx));
	printf("%s:   tidi=%d tidt=%d flags=0x%02x\n", sc->sc_dv.dv_xname,
	    (le32toh(rb->msgfunc) >> 12) & 4095, le32toh(rb->msgfunc) & 4095,
	    (le32toh(rb->msgflags) >> 8) & 0xff);
}
#endif

/*
 * Dump a transport failure reply.
 */
static void
iop_tfn_print(struct iop_softc *sc, struct i2o_fault_notify *fn)
{

	printf("%s: WARNING: transport failure:\n", sc->sc_dv.dv_xname);

	printf("%s:  ictx=0x%08x tctx=0x%08x\n", sc->sc_dv.dv_xname,
	    le32toh(fn->msgictx), le32toh(fn->msgtctx));
	printf("%s:  failurecode=0x%02x severity=0x%02x\n",
	    sc->sc_dv.dv_xname, fn->failurecode, fn->severity);
	printf("%s:  highestver=0x%02x lowestver=0x%02x\n",
	    sc->sc_dv.dv_xname, fn->highestver, fn->lowestver);
}

/*
 * Translate an I2O ASCII field into a C string.
 */
void
iop_strvis(struct iop_softc *sc, const char *src, int slen, char *dst, int dlen)
{
	int hc, lc, i, nit;

	dlen--;
	lc = 0;
	hc = 0;
	i = 0;

	/*
	 * DPT use NUL as a space, whereas AMI use it as a terminator.  The
	 * spec has nothing to say about it.  Since AMI fields are usually
	 * filled with junk after the terminator, ...
	 */
	nit = (le16toh(sc->sc_status.orgid) != I2O_ORG_DPT);

	while (slen-- != 0 && dlen-- != 0) {
		if (nit && *src == '\0')
			break;
		else if (*src <= 0x20 || *src >= 0x7f) {
			if (hc)
				dst[i++] = ' ';
		} else {
			hc = 1;
			dst[i++] = *src;
			lc = i;
		}
		src++;
	}
	
	dst[lc] = '\0';
}

/*
 * Retrieve the DEVICE_IDENTITY parameter group from the target and dump it.
 */
int
iop_print_ident(struct iop_softc *sc, int tid)
{
	struct {
		struct	i2o_param_op_results pr;
		struct	i2o_param_read_results prr;
		struct	i2o_param_device_identity di;
	} __attribute__ ((__packed__)) p;
	char buf[32];
	int rv;

	rv = iop_field_get_all(sc, tid, I2O_PARAM_DEVICE_IDENTITY, &p,
	    sizeof(p), NULL);
	if (rv != 0)
		return (rv);

	iop_strvis(sc, p.di.vendorinfo, sizeof(p.di.vendorinfo), buf,
	    sizeof(buf));
	printf(" <%s, ", buf);
	iop_strvis(sc, p.di.productinfo, sizeof(p.di.productinfo), buf,
	    sizeof(buf));
	printf("%s, ", buf);
	iop_strvis(sc, p.di.revlevel, sizeof(p.di.revlevel), buf, sizeof(buf));
	printf("%s>", buf);

	return (0);
}

/*
 * Claim or unclaim the specified TID.
 */
int
iop_util_claim(struct iop_softc *sc, struct iop_initiator *ii, int release,
	       int flags)
{
	struct iop_msg *im;
	struct i2o_util_claim mf;
	int rv, func;

	func = release ? I2O_UTIL_CLAIM_RELEASE : I2O_UTIL_CLAIM;
	im = iop_msg_alloc(sc, IM_WAIT);

	/* We can use the same structure, as they're identical. */
	mf.msgflags = I2O_MSGFLAGS(i2o_util_claim);
	mf.msgfunc = I2O_MSGFUNC(ii->ii_tid, func);
	mf.msgictx = ii->ii_ictx;
	mf.msgtctx = im->im_tctx;
	mf.flags = flags;

	rv = iop_msg_post(sc, im, &mf, 5000);
	iop_msg_free(sc, im);
	return (rv);
}	

/*
 * Perform an abort.
 */
int iop_util_abort(struct iop_softc *sc, struct iop_initiator *ii, int func,
		   int tctxabort, int flags)
{
	struct iop_msg *im;
	struct i2o_util_abort mf;
	int rv;

	im = iop_msg_alloc(sc, IM_WAIT);

	mf.msgflags = I2O_MSGFLAGS(i2o_util_abort);
	mf.msgfunc = I2O_MSGFUNC(ii->ii_tid, I2O_UTIL_ABORT);
	mf.msgictx = ii->ii_ictx;
	mf.msgtctx = im->im_tctx;
	mf.flags = (func << 24) | flags;
	mf.tctxabort = tctxabort;

	rv = iop_msg_post(sc, im, &mf, 5000);
	iop_msg_free(sc, im);
	return (rv);
}

/*
 * Enable or disable reception of events for the specified device.
 */
int iop_util_eventreg(struct iop_softc *sc, struct iop_initiator *ii, int mask)
{
	struct i2o_util_event_register mf;

	mf.msgflags = I2O_MSGFLAGS(i2o_util_event_register);
	mf.msgfunc = I2O_MSGFUNC(ii->ii_tid, I2O_UTIL_EVENT_REGISTER);
	mf.msgictx = ii->ii_ictx;
	mf.msgtctx = 0;
	mf.eventmask = mask;

	/* This message is replied to only when events are signalled. */
	return (iop_post(sc, (u_int32_t *)&mf));
}

int
iopopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct iop_softc *sc;

	if ((sc = device_lookup(&iop_cd, minor(dev))) == NULL)
		return (ENXIO);
	if ((sc->sc_flags & IOP_ONLINE) == 0)
		return (ENXIO);
	if ((sc->sc_flags & IOP_OPEN) != 0)
		return (EBUSY);
	sc->sc_flags |= IOP_OPEN;

	return (0);
}

int
iopclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct iop_softc *sc;

	sc = device_lookup(&iop_cd, minor(dev));
	sc->sc_flags &= ~IOP_OPEN;

	return (0);
}

int
iopioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct iop_softc *sc;
	struct iovec *iov;
	int rv, i;

	if (securelevel >= 2)
		return (EPERM);

	sc = device_lookup(&iop_cd, minor(dev));

	switch (cmd) {
	case IOPIOCPT:
		return (iop_passthrough(sc, (struct ioppt *)data, p));

	case IOPIOCGSTATUS:
		iov = (struct iovec *)data;
		i = sizeof(struct i2o_status);
		if (i > iov->iov_len)
			i = iov->iov_len;
		else
			iov->iov_len = i;
		if ((rv = iop_status_get(sc, 0)) == 0)
			rv = copyout(&sc->sc_status, iov->iov_base, i);
		return (rv);

	case IOPIOCGLCT:
	case IOPIOCGTIDMAP:
	case IOPIOCRECONFIG:
		break;

	default:
#if defined(DIAGNOSTIC) || defined(I2ODEBUG)
		printf("%s: unknown ioctl %lx\n", sc->sc_dv.dv_xname, cmd);
#endif
		return (ENOTTY);
	}

	if ((rv = lockmgr(&sc->sc_conflock, LK_SHARED, NULL)) != 0)
		return (rv);

	switch (cmd) {
	case IOPIOCGLCT:
		iov = (struct iovec *)data;
		i = le16toh(sc->sc_lct->tablesize) << 2;
		if (i > iov->iov_len)
			i = iov->iov_len;
		else
			iov->iov_len = i;
		rv = copyout(sc->sc_lct, iov->iov_base, i);
		break;

	case IOPIOCRECONFIG:
		rv = iop_reconfigure(sc, 0);
		break;

	case IOPIOCGTIDMAP:
		iov = (struct iovec *)data;
		i = sizeof(struct iop_tidmap) * sc->sc_nlctent;
		if (i > iov->iov_len)
			i = iov->iov_len;
		else
			iov->iov_len = i;
		rv = copyout(sc->sc_tidmap, iov->iov_base, i);
		break;
	}

	lockmgr(&sc->sc_conflock, LK_RELEASE, NULL);
	return (rv);
}

static int
iop_passthrough(struct iop_softc *sc, struct ioppt *pt, struct proc *p)
{
	struct iop_msg *im;
	struct i2o_msg *mf;
	struct ioppt_buf *ptb;
	int rv, i, mapped;

	mf = NULL;
	im = NULL;
	mapped = 1;

	if (pt->pt_msglen > sc->sc_framesize ||
	    pt->pt_msglen < sizeof(struct i2o_msg) ||
	    pt->pt_nbufs > IOP_MAX_MSG_XFERS ||
	    pt->pt_nbufs < 0 || pt->pt_replylen < 0 ||
            pt->pt_timo < 1000 || pt->pt_timo > 5*60*1000)
		return (EINVAL);

	for (i = 0; i < pt->pt_nbufs; i++)
		if (pt->pt_bufs[i].ptb_datalen > IOP_MAX_XFER) {
			rv = ENOMEM;
			goto bad;
		}

	mf = malloc(sc->sc_framesize, M_DEVBUF, M_WAITOK);
	if (mf == NULL)
		return (ENOMEM);

	if ((rv = copyin(pt->pt_msg, mf, pt->pt_msglen)) != 0)
		goto bad;

	im = iop_msg_alloc(sc, IM_WAIT | IM_NOSTATUS);
	im->im_rb = (struct i2o_reply *)mf;
	mf->msgictx = IOP_ICTX;
	mf->msgtctx = im->im_tctx;

	for (i = 0; i < pt->pt_nbufs; i++) {
		ptb = &pt->pt_bufs[i];
		rv = iop_msg_map(sc, im, (u_int32_t *)mf, ptb->ptb_data,
		    ptb->ptb_datalen, ptb->ptb_out != 0, p);
		if (rv != 0)
			goto bad;
		mapped = 1;
	}

	if ((rv = iop_msg_post(sc, im, mf, pt->pt_timo)) != 0)
		goto bad;

	i = (le32toh(im->im_rb->msgflags) >> 14) & ~3;
	if (i > sc->sc_framesize)
		i = sc->sc_framesize;
	if (i > pt->pt_replylen)
		i = pt->pt_replylen;
	rv = copyout(im->im_rb, pt->pt_reply, i);

 bad:
	if (mapped != 0)
		iop_msg_unmap(sc, im);
	if (im != NULL)
		iop_msg_free(sc, im);
	if (mf != NULL)
		free(mf, M_DEVBUF);
	return (rv);
}
