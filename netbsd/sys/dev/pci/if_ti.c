/* $NetBSD: if_ti.c,v 1.8.4.3 2001/03/13 20:44:13 he Exp $ */

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	FreeBSD Id: if_ti.c,v 1.15 1999/08/14 15:45:03 wpaul Exp
 */

/*
 * Alteon Networks Tigon PCI gigabit ethernet driver for FreeBSD.
 * Manuals, sample driver and firmware source kits are available
 * from http://www.alteon.com/support/openkits.
 * 
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The Alteon Networks Tigon chip contains an embedded R4000 CPU,
 * gigabit MAC, dual DMA channels and a PCI interface unit. NICs
 * using the Tigon may have anywhere from 512K to 2MB of SRAM. The
 * Tigon supports hardware IP, TCP and UCP checksumming, multicast
 * filtering and jumbo (9014 byte) frames. The hardware is largely
 * controlled by firmware, which must be loaded into the NIC during
 * initialization.
 *
 * The Tigon 2 contains 2 R4000 CPUs and requires a newer firmware
 * revision, which supports new features such as extended commands,
 * extended jumbo receive ring desciptors and a mini receive ring.
 *
 * Alteon Networks is to be commended for releasing such a vast amount
 * of development material for the Tigon NIC without requiring an NDA
 * (although they really should have done it a long time ago). With
 * any luck, the other vendors will finally wise up and follow Alteon's
 * stellar example.
 *
 * The firmware for the Tigon 1 and 2 NICs is compiled directly into
 * this driver by #including it as a C header file. This bloats the
 * driver somewhat, but it's the easiest method considering that the
 * driver code and firmware code need to be kept in sync. The source
 * for the firmware is not provided with the FreeBSD distribution since
 * compiling it requires a GNU toolchain targeted for mips-sgi-irix5.3.
 *
 * The following people deserve special thanks:
 * - Terry Murphy of 3Com, for providing a 3c985 Tigon 1 board
 *   for testing
 * - Raymond Lee of Netgear, for providing a pair of Netgear
 *   GA620 Tigon 2 boards for testing
 * - Ulf Zimmermann, for bringing the GA620 to my attention and
 *   convincing me to write this driver.
 * - Andrew Gallatin for providing FreeBSD/Alpha support.
 */

#include "bpfilter.h"
#include "opt_inet.h"
#include "opt_ns.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_ether.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_inarp.h>
#endif

#ifdef NS
#include <netns/ns.h>
#include <netns/ns_if.h>
#endif

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_tireg.h>
#include <dev/pci/ti_fw.h>
#include <dev/pci/ti_fw2.h>

#ifdef M_HWCKSUM
/*#define TI_CSUM_OFFLOAD*/
#endif

#define bootverbose 1

/*
 * Various supported device vendors/types and their names.
 */

static struct ti_type ti_devs[] = {
	{ PCI_VENDOR_ALTEON,	PCI_PRODUCT_ALTEON_ACENIC,
		"Alteon AceNIC 1000baseSX Gigabit Ethernet" },
	{ PCI_VENDOR_ALTEON,	PCI_PRODUCT_ALTEON_ACENIC_COPPER,
		"Alteon AceNIC 1000baseT Gigabit Ethernet" },
	{ PCI_VENDOR_3COM,	PCI_PRODUCT_3COM_3C985,
		"3Com 3c985-SX Gigabit Ethernet" },
	{ PCI_VENDOR_NETGEAR, PCI_PRODUCT_NETGEAR_GA620,
		"Netgear GA620 1000baseSX Gigabit Ethernet" },
	{ PCI_VENDOR_NETGEAR, PCI_PRODUCT_NETGEAR_GA620T,
		"Netgear GA620 1000baseT Gigabit Ethernet" },
	{ PCI_VENDOR_SGI, PCI_PRODUCT_SGI_TIGON,
		"Silicon Graphics Gigabit Ethernet" },
	{ 0, 0, NULL }
};

static struct ti_type *ti_type_match __P((struct pci_attach_args *));
static int ti_probe	__P((struct device *, struct cfdata *, void *));
static void ti_attach	__P((struct device *, struct device *, void *));
static void ti_shutdown __P((void *));
static void ti_txeof		__P((struct ti_softc *));
static void ti_rxeof		__P((struct ti_softc *));

static void ti_stats_update	__P((struct ti_softc *));
static int ti_encap		__P((struct ti_softc *, struct mbuf *,
					u_int32_t *));

static int ti_intr		__P((void *));
static void ti_start		__P((struct ifnet *));
static int ti_ioctl		__P((struct ifnet *, u_long, caddr_t));
static void ti_init		__P((void *));
static void ti_init2		__P((struct ti_softc *));
static void ti_stop		__P((struct ti_softc *));
static void ti_watchdog		__P((struct ifnet *));
static int ti_ifmedia_upd	__P((struct ifnet *));
static void ti_ifmedia_sts	__P((struct ifnet *, struct ifmediareq *));

static u_int32_t ti_eeprom_putbyte	__P((struct ti_softc *, int));
static u_int8_t	ti_eeprom_getbyte	__P((struct ti_softc *,
						int, u_int8_t *));
static int ti_read_eeprom	__P((struct ti_softc *, caddr_t, int, int));

static void ti_add_mcast	__P((struct ti_softc *, struct ether_addr *));
static void ti_del_mcast	__P((struct ti_softc *, struct ether_addr *));
static void ti_setmulti		__P((struct ti_softc *));

static void ti_mem		__P((struct ti_softc *, u_int32_t,
					u_int32_t, caddr_t));
static void ti_loadfw		__P((struct ti_softc *));
static void ti_cmd		__P((struct ti_softc *, struct ti_cmd_desc *));
static void ti_cmd_ext		__P((struct ti_softc *, struct ti_cmd_desc *,
					caddr_t, int));
static void ti_handle_events	__P((struct ti_softc *));
static int ti_alloc_jumbo_mem	__P((struct ti_softc *));
static void *ti_jalloc		__P((struct ti_softc *));
static void ti_jfree		__P((caddr_t, u_int, void *));
static int ti_newbuf_std	__P((struct ti_softc *, int, struct mbuf *, bus_dmamap_t));
static int ti_newbuf_mini	__P((struct ti_softc *, int, struct mbuf *, bus_dmamap_t));
static int ti_newbuf_jumbo	__P((struct ti_softc *, int, struct mbuf *));
static int ti_init_rx_ring_std	__P((struct ti_softc *));
static void ti_free_rx_ring_std	__P((struct ti_softc *));
static int ti_init_rx_ring_jumbo	__P((struct ti_softc *));
static void ti_free_rx_ring_jumbo	__P((struct ti_softc *));
static int ti_init_rx_ring_mini	__P((struct ti_softc *));
static void ti_free_rx_ring_mini	__P((struct ti_softc *));
static void ti_free_tx_ring	__P((struct ti_softc *));
static int ti_init_tx_ring	__P((struct ti_softc *));

static int ti_64bitslot_war	__P((struct ti_softc *));
static int ti_chipinit		__P((struct ti_softc *));
static int ti_gibinit		__P((struct ti_softc *));

static int ti_ether_ioctl __P((struct ifnet *, u_long, caddr_t));

struct cfattach ti_ca = {
	sizeof(struct ti_softc), ti_probe, ti_attach
};

/*
 * Send an instruction or address to the EEPROM, check for ACK.
 */
static u_int32_t ti_eeprom_putbyte(sc, byte)
	struct ti_softc		*sc;
	int			byte;
{
	int		i, ack = 0;

	/*
	 * Make sure we're in TX mode.
	 */
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN);

	/*
	 * Feed in each bit and stobe the clock.
	 */
	for (i = 0x80; i; i >>= 1) {
		if (byte & i) {
			TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_DOUT);
		} else {
			TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_DOUT);
		}
		DELAY(1);
		TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
		DELAY(1);
		TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
	}

	/*
	 * Turn off TX mode.
	 */
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN);

	/*
	 * Check for ack.
	 */
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
	ack = CSR_READ_4(sc, TI_MISC_LOCAL_CTL) & TI_MLC_EE_DIN;
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);

	return(ack);
}

/*
 * Read a byte of data stored in the EEPROM at address 'addr.'
 * We have to send two address bytes since the EEPROM can hold
 * more than 256 bytes of data.
 */
static u_int8_t ti_eeprom_getbyte(sc, addr, dest)
	struct ti_softc		*sc;
	int			addr;
	u_int8_t		*dest;
{
	int		i;
	u_int8_t		byte = 0;

	EEPROM_START;

	/*
	 * Send write control code to EEPROM.
	 */
	if (ti_eeprom_putbyte(sc, EEPROM_CTL_WRITE)) {
		printf("%s: failed to send write command, status: %x\n",
		    sc->sc_dev.dv_xname, CSR_READ_4(sc, TI_MISC_LOCAL_CTL));
		return(1);
	}

	/*
	 * Send first byte of address of byte we want to read.
	 */
	if (ti_eeprom_putbyte(sc, (addr >> 8) & 0xFF)) {
		printf("%s: failed to send address, status: %x\n",
		    sc->sc_dev.dv_xname, CSR_READ_4(sc, TI_MISC_LOCAL_CTL));
		return(1);
	}
	/*
	 * Send second byte address of byte we want to read.
	 */
	if (ti_eeprom_putbyte(sc, addr & 0xFF)) {
		printf("%s: failed to send address, status: %x\n",
		    sc->sc_dev.dv_xname, CSR_READ_4(sc, TI_MISC_LOCAL_CTL));
		return(1);
	}

	EEPROM_STOP;
	EEPROM_START;
	/*
	 * Send read control code to EEPROM.
	 */
	if (ti_eeprom_putbyte(sc, EEPROM_CTL_READ)) {
		printf("%s: failed to send read command, status: %x\n",
		    sc->sc_dev.dv_xname, CSR_READ_4(sc, TI_MISC_LOCAL_CTL));
		return(1);
	}

	/*
	 * Start reading bits from EEPROM.
	 */
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN);
	for (i = 0x80; i; i >>= 1) {
		TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
		DELAY(1);
		if (CSR_READ_4(sc, TI_MISC_LOCAL_CTL) & TI_MLC_EE_DIN)
			byte |= i;
		TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
		DELAY(1);
	}

	EEPROM_STOP;

	/*
	 * No ACK generated for read, so just return byte.
	 */

	*dest = byte;

	return(0);
}

/*
 * Read a sequence of bytes from the EEPROM.
 */
static int ti_read_eeprom(sc, dest, off, cnt)
	struct ti_softc		*sc;
	caddr_t			dest;
	int			off;
	int			cnt;
{
	int			err = 0, i;
	u_int8_t		byte = 0;

	for (i = 0; i < cnt; i++) {
		err = ti_eeprom_getbyte(sc, off + i, &byte);
		if (err)
			break;
		*(dest + i) = byte;
	}

	return(err ? 1 : 0);
}

/*
 * NIC memory access function. Can be used to either clear a section
 * of NIC local memory or (if buf is non-NULL) copy data into it.
 */
static void ti_mem(sc, addr, len, buf)
	struct ti_softc		*sc;
	u_int32_t		addr, len;
	caddr_t			buf;
{
	int			segptr, segsize, cnt;
	caddr_t			ptr;

	segptr = addr;
	cnt = len;
	ptr = buf;

	while(cnt) {
		if (cnt < TI_WINLEN)
			segsize = cnt;
		else
			segsize = TI_WINLEN - (segptr % TI_WINLEN);
		CSR_WRITE_4(sc, TI_WINBASE, (segptr & ~(TI_WINLEN - 1)));
		if (buf == NULL) {
			bus_space_set_region_4(sc->ti_btag, sc->ti_bhandle,
			    TI_WINDOW + (segptr & (TI_WINLEN - 1)), 0,
			    segsize / 4);
		} else {
			bus_space_write_region_4(sc->ti_btag, sc->ti_bhandle,
			    TI_WINDOW + (segptr & (TI_WINLEN - 1)),
			    (u_int32_t *)ptr, segsize / 4);
			ptr += segsize;
		}
		segptr += segsize;
		cnt -= segsize;
	}

	return;
}

/*
 * Load firmware image into the NIC. Check that the firmware revision
 * is acceptable and see if we want the firmware for the Tigon 1 or
 * Tigon 2.
 */
static void ti_loadfw(sc)
	struct ti_softc		*sc;
{
	switch(sc->ti_hwrev) {
	case TI_HWREV_TIGON:
		if (tigonFwReleaseMajor != TI_FIRMWARE_MAJOR ||
		    tigonFwReleaseMinor != TI_FIRMWARE_MINOR ||
		    tigonFwReleaseFix != TI_FIRMWARE_FIX) {
			printf("%s: firmware revision mismatch; want "
			    "%d.%d.%d, got %d.%d.%d\n", sc->sc_dev.dv_xname,
			    TI_FIRMWARE_MAJOR, TI_FIRMWARE_MINOR,
			    TI_FIRMWARE_FIX, tigonFwReleaseMajor,
			    tigonFwReleaseMinor, tigonFwReleaseFix);
			return;
		}
		ti_mem(sc, tigonFwTextAddr, tigonFwTextLen,
		    (caddr_t)tigonFwText);
		ti_mem(sc, tigonFwDataAddr, tigonFwDataLen,
		    (caddr_t)tigonFwData);
		ti_mem(sc, tigonFwRodataAddr, tigonFwRodataLen,
		    (caddr_t)tigonFwRodata);
		ti_mem(sc, tigonFwBssAddr, tigonFwBssLen, NULL);
		ti_mem(sc, tigonFwSbssAddr, tigonFwSbssLen, NULL);
		CSR_WRITE_4(sc, TI_CPU_PROGRAM_COUNTER, tigonFwStartAddr);
		break;
	case TI_HWREV_TIGON_II:
		if (tigon2FwReleaseMajor != TI_FIRMWARE_MAJOR ||
		    tigon2FwReleaseMinor != TI_FIRMWARE_MINOR ||
		    tigon2FwReleaseFix != TI_FIRMWARE_FIX) {
			printf("%s: firmware revision mismatch; want "
			    "%d.%d.%d, got %d.%d.%d\n", sc->sc_dev.dv_xname,
			    TI_FIRMWARE_MAJOR, TI_FIRMWARE_MINOR,
			    TI_FIRMWARE_FIX, tigon2FwReleaseMajor,
			    tigon2FwReleaseMinor, tigon2FwReleaseFix);
			return;
		}
		ti_mem(sc, tigon2FwTextAddr, tigon2FwTextLen,
		    (caddr_t)tigon2FwText);
		ti_mem(sc, tigon2FwDataAddr, tigon2FwDataLen,
		    (caddr_t)tigon2FwData);
		ti_mem(sc, tigon2FwRodataAddr, tigon2FwRodataLen,
		    (caddr_t)tigon2FwRodata);
		ti_mem(sc, tigon2FwBssAddr, tigon2FwBssLen, NULL);
		ti_mem(sc, tigon2FwSbssAddr, tigon2FwSbssLen, NULL);
		CSR_WRITE_4(sc, TI_CPU_PROGRAM_COUNTER, tigon2FwStartAddr);
		break;
	default:
		printf("%s: can't load firmware: unknown hardware rev\n",
		    sc->sc_dev.dv_xname);
		break;
	}

	return;
}

/*
 * Send the NIC a command via the command ring.
 */
static void ti_cmd(sc, cmd)
	struct ti_softc		*sc;
	struct ti_cmd_desc	*cmd;
{
	u_int32_t		index;

	index = sc->ti_cmd_saved_prodidx;
	CSR_WRITE_4(sc, TI_GCR_CMDRING + (index * 4), *(u_int32_t *)(cmd));
	TI_INC(index, TI_CMD_RING_CNT);
	CSR_WRITE_4(sc, TI_MB_CMDPROD_IDX, index);
	sc->ti_cmd_saved_prodidx = index;

	return;
}

/*
 * Send the NIC an extended command. The 'len' parameter specifies the
 * number of command slots to include after the initial command.
 */
static void ti_cmd_ext(sc, cmd, arg, len)
	struct ti_softc		*sc;
	struct ti_cmd_desc	*cmd;
	caddr_t			arg;
	int			len;
{
	u_int32_t		index;
	int		i;

	index = sc->ti_cmd_saved_prodidx;
	CSR_WRITE_4(sc, TI_GCR_CMDRING + (index * 4), *(u_int32_t *)(cmd));
	TI_INC(index, TI_CMD_RING_CNT);
	for (i = 0; i < len; i++) {
		CSR_WRITE_4(sc, TI_GCR_CMDRING + (index * 4),
		    *(u_int32_t *)(&arg[i * 4]));
		TI_INC(index, TI_CMD_RING_CNT);
	}
	CSR_WRITE_4(sc, TI_MB_CMDPROD_IDX, index);
	sc->ti_cmd_saved_prodidx = index;

	return;
}

/*
 * Handle events that have triggered interrupts.
 */
static void ti_handle_events(sc)
	struct ti_softc		*sc;
{
	struct ti_event_desc	*e;

	if (sc->ti_rdata->ti_event_ring == NULL)
		return;

	while (sc->ti_ev_saved_considx != sc->ti_ev_prodidx.ti_idx) {
		e = &sc->ti_rdata->ti_event_ring[sc->ti_ev_saved_considx];
		switch(e->ti_event) {
		case TI_EV_LINKSTAT_CHANGED:
			sc->ti_linkstat = e->ti_code;
			if (e->ti_code == TI_EV_CODE_LINK_UP)
				printf("%s: 10/100 link up\n",
				       sc->sc_dev.dv_xname);
			else if (e->ti_code == TI_EV_CODE_GIG_LINK_UP)
				printf("%s: gigabit link up\n",
				       sc->sc_dev.dv_xname);
			else if (e->ti_code == TI_EV_CODE_LINK_DOWN)
				printf("%s: link down\n",
				       sc->sc_dev.dv_xname);
			break;
		case TI_EV_ERROR:
			if (e->ti_code == TI_EV_CODE_ERR_INVAL_CMD)
				printf("%s: invalid command\n",
				       sc->sc_dev.dv_xname);
			else if (e->ti_code == TI_EV_CODE_ERR_UNIMP_CMD)
				printf("%s: unknown command\n",
				       sc->sc_dev.dv_xname);
			else if (e->ti_code == TI_EV_CODE_ERR_BADCFG)
				printf("%s: bad config data\n",
				       sc->sc_dev.dv_xname);
			break;
		case TI_EV_FIRMWARE_UP:
			ti_init2(sc);
			break;
		case TI_EV_STATS_UPDATED:
			ti_stats_update(sc);
			break;
		case TI_EV_RESET_JUMBO_RING:
		case TI_EV_MCAST_UPDATED:
			/* Who cares. */
			break;
		default:
			printf("%s: unknown event: %d\n",
			    sc->sc_dev.dv_xname, e->ti_event);
			break;
		}
		/* Advance the consumer index. */
		TI_INC(sc->ti_ev_saved_considx, TI_EVENT_RING_CNT);
		CSR_WRITE_4(sc, TI_GCR_EVENTCONS_IDX, sc->ti_ev_saved_considx);
	}

	return;
}

/*
 * Memory management for the jumbo receive ring is a pain in the
 * butt. We need to allocate at least 9018 bytes of space per frame,
 * _and_ it has to be contiguous (unless you use the extended
 * jumbo descriptor format). Using malloc() all the time won't
 * work: malloc() allocates memory in powers of two, which means we
 * would end up wasting a considerable amount of space by allocating
 * 9K chunks. We don't have a jumbo mbuf cluster pool. Thus, we have
 * to do our own memory management.
 *
 * The driver needs to allocate a contiguous chunk of memory at boot
 * time. We then chop this up ourselves into 9K pieces and use them
 * as external mbuf storage.
 *
 * One issue here is how much memory to allocate. The jumbo ring has
 * 256 slots in it, but at 9K per slot than can consume over 2MB of
 * RAM. This is a bit much, especially considering we also need
 * RAM for the standard ring and mini ring (on the Tigon 2). To
 * save space, we only actually allocate enough memory for 64 slots
 * by default, which works out to between 500 and 600K. This can
 * be tuned by changing a #define in if_tireg.h.
 */

static int ti_alloc_jumbo_mem(sc)
	struct ti_softc		*sc;
{
	caddr_t			ptr;
	int		i;
	struct ti_jpool_entry   *entry;
	bus_dma_segment_t dmaseg;
	int error, dmanseg;

	/* Grab a big chunk o' storage. */
	if ((error = bus_dmamem_alloc(sc->sc_dmat,
	    TI_JMEM, NBPG, 0, &dmaseg, 1, &dmanseg,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("%s: can't allocate jumbo buffer, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &dmaseg, dmanseg,
	    TI_JMEM, (caddr_t *)&sc->ti_cdata.ti_jumbo_buf,
	    BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("%s: can't map jumbo buffer, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}

	if ((error = bus_dmamap_create(sc->sc_dmat,
	    TI_JMEM, 1,
	    TI_JMEM, 0, BUS_DMA_NOWAIT,
	    &sc->jumbo_dmamap)) != 0) {
		printf("%s: can't create jumbo buffer DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->jumbo_dmamap,
	    sc->ti_cdata.ti_jumbo_buf, TI_JMEM, NULL,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("%s: can't load jumbo buffer DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}
	sc->jumbo_dmaaddr = sc->jumbo_dmamap->dm_segs[0].ds_addr;

	SIMPLEQ_INIT(&sc->ti_jfree_listhead);
	SIMPLEQ_INIT(&sc->ti_jinuse_listhead);

	/*
	 * Now divide it up into 9K pieces and save the addresses
	 * in an array.
	 */
	ptr = sc->ti_cdata.ti_jumbo_buf;
	for (i = 0; i < TI_JSLOTS; i++) {
		sc->ti_cdata.ti_jslots[i] = ptr;
		ptr += TI_JLEN;
		entry = malloc(sizeof(struct ti_jpool_entry), 
			       M_DEVBUF, M_NOWAIT);
		if (entry == NULL) {
			free(sc->ti_cdata.ti_jumbo_buf, M_DEVBUF);
			sc->ti_cdata.ti_jumbo_buf = NULL;
			printf("%s: no memory for jumbo "
			    "buffer queue!\n", sc->sc_dev.dv_xname);
			return(ENOBUFS);
		}
		entry->slot = i;
		SIMPLEQ_INSERT_HEAD(&sc->ti_jfree_listhead, entry,
				    jpool_entries);
	}

	return(0);
}

/*
 * Allocate a jumbo buffer.
 */
static void *ti_jalloc(sc)
	struct ti_softc		*sc;
{
	struct ti_jpool_entry   *entry;
	
	entry = SIMPLEQ_FIRST(&sc->ti_jfree_listhead);
	
	if (entry == NULL) {
		printf("%s: no free jumbo buffers\n", sc->sc_dev.dv_xname);
		return(NULL);
	}

	SIMPLEQ_REMOVE_HEAD(&sc->ti_jfree_listhead, entry, jpool_entries);
	SIMPLEQ_INSERT_HEAD(&sc->ti_jinuse_listhead, entry, jpool_entries);
	return(sc->ti_cdata.ti_jslots[entry->slot]);
}

/*
 * Release a jumbo buffer.
 */
static void ti_jfree(buf, size, arg)
	caddr_t			buf;
	u_int			size;
	void *arg;
{
	struct ti_softc		*sc;
	int		        i;
	struct ti_jpool_entry   *entry;

	/* Extract the softc struct pointer. */
	sc = (struct ti_softc *)arg;

	if (sc == NULL)
		panic("ti_jfree: didn't get softc pointer!");

	/* calculate the slot this buffer belongs to */

	i = ((caddr_t)buf
	     - (caddr_t)sc->ti_cdata.ti_jumbo_buf) / TI_JLEN;

	if ((i < 0) || (i >= TI_JSLOTS))
		panic("ti_jfree: asked to free buffer that we don't manage!");
	entry = SIMPLEQ_FIRST(&sc->ti_jinuse_listhead);
	if (entry == NULL)
		panic("ti_jfree: buffer not in use!");
	entry->slot = i;
	SIMPLEQ_REMOVE_HEAD(&sc->ti_jinuse_listhead, 
	    entry, jpool_entries);
	SIMPLEQ_INSERT_HEAD(&sc->ti_jfree_listhead, 
	     entry, jpool_entries);

	return;
}


/*
 * Intialize a standard receive ring descriptor.
 */
static int ti_newbuf_std(sc, i, m, dmamap)
	struct ti_softc		*sc;
	int			i;
	struct mbuf		*m;
	bus_dmamap_t dmamap; /* required if (m != NULL) */
{
	struct mbuf		*m_new = NULL;
	struct ti_rx_desc	*r;
	int error;

	if (dmamap == NULL) {
		/* if (m) panic() */

		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
					       MCLBYTES, 0, BUS_DMA_NOWAIT,
					       &dmamap)) != 0) {
			printf("%s: can't create recv map, error = %d\n",
			       sc->sc_dev.dv_xname, error);
			return(ENOMEM);
		}
	}
	sc->std_dmamap[i] = dmamap;

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			printf("%s: mbuf allocation failed "
			    "-- packet dropped!\n", sc->sc_dev.dv_xname);
			return(ENOBUFS);
		}

		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			printf("%s: cluster allocation failed "
			    "-- packet dropped!\n", sc->sc_dev.dv_xname);
			m_freem(m_new);
			return(ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_adj(m_new, ETHER_ALIGN);

		if ((error = bus_dmamap_load(sc->sc_dmat, dmamap,
				mtod(m_new, caddr_t), m_new->m_len, NULL,
				BUS_DMA_NOWAIT)) != 0) {
			printf("%s: can't load recv map, error = %d\n",
			       sc->sc_dev.dv_xname, error);
			return (ENOMEM);
		}
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
		m_adj(m_new, ETHER_ALIGN);

		/* reuse the dmamap */
	}

	sc->ti_cdata.ti_rx_std_chain[i] = m_new;
	r = &sc->ti_rdata->ti_rx_std_ring[i];
	TI_HOSTADDR(r->ti_addr) = dmamap->dm_segs[0].ds_addr;
	r->ti_type = TI_BDTYPE_RECV_BD;
#ifdef TI_CSUM_OFFLOAD
	r->ti_flags = TI_BDFLAG_TCP_UDP_CKSUM|TI_BDFLAG_IP_CKSUM;
#else
	r->ti_flags = 0;
#endif
	r->ti_len = m_new->m_len; /* == ds_len */
	r->ti_idx = i;

	return(0);
}

/*
 * Intialize a mini receive ring descriptor. This only applies to
 * the Tigon 2.
 */
static int ti_newbuf_mini(sc, i, m, dmamap)
	struct ti_softc		*sc;
	int			i;
	struct mbuf		*m;
	bus_dmamap_t dmamap; /* required if (m != NULL) */
{
	struct mbuf		*m_new = NULL;
	struct ti_rx_desc	*r;
	int error;

	if (dmamap == NULL) {
		/* if (m) panic() */

		if ((error = bus_dmamap_create(sc->sc_dmat, MHLEN, 1,
					       MHLEN, 0, BUS_DMA_NOWAIT,
					       &dmamap)) != 0) {
			printf("%s: can't create recv map, error = %d\n",
			       sc->sc_dev.dv_xname, error);
			return(ENOMEM);
		}
	}
	sc->mini_dmamap[i] = dmamap;

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			printf("%s: mbuf allocation failed "
			    "-- packet dropped!\n", sc->sc_dev.dv_xname);
			return(ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MHLEN;
		m_adj(m_new, ETHER_ALIGN);

		if ((error = bus_dmamap_load(sc->sc_dmat, dmamap,
				mtod(m_new, caddr_t), m_new->m_len, NULL,
				BUS_DMA_NOWAIT)) != 0) {
			printf("%s: can't load recv map, error = %d\n",
			       sc->sc_dev.dv_xname, error);
			return (ENOMEM);
		}
	} else {
		m_new = m;
		m_new->m_data = m_new->m_pktdat;
		m_new->m_len = m_new->m_pkthdr.len = MHLEN;
		m_adj(m_new, ETHER_ALIGN);

		/* reuse the dmamap */
	}

	r = &sc->ti_rdata->ti_rx_mini_ring[i];
	sc->ti_cdata.ti_rx_mini_chain[i] = m_new;
	TI_HOSTADDR(r->ti_addr) = dmamap->dm_segs[0].ds_addr;
	r->ti_type = TI_BDTYPE_RECV_BD;
	r->ti_flags = TI_BDFLAG_MINI_RING;
#ifdef TI_CSUM_OFFLOAD
	r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM|TI_BDFLAG_IP_CKSUM;
#endif
	r->ti_len = m_new->m_len; /* == ds_len */
	r->ti_idx = i;

	return(0);
}

/*
 * Initialize a jumbo receive ring descriptor. This allocates
 * a jumbo buffer from the pool managed internally by the driver.
 */
static int ti_newbuf_jumbo(sc, i, m)
	struct ti_softc		*sc;
	int			i;
	struct mbuf		*m;
{
	struct mbuf		*m_new = NULL;
	struct ti_rx_desc	*r;

	if (m == NULL) {
		caddr_t			*buf = NULL;

		/* Allocate the mbuf. */
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			printf("%s: mbuf allocation failed "
			    "-- packet dropped!\n", sc->sc_dev.dv_xname);
			return(ENOBUFS);
		}

		/* Allocate the jumbo buffer */
		buf = ti_jalloc(sc);
		if (buf == NULL) {
			m_freem(m_new);
			printf("%s: jumbo allocation failed "
			    "-- packet dropped!\n", sc->sc_dev.dv_xname);
			return(ENOBUFS);
		}

		/* Attach the buffer to the mbuf. */
		m_new->m_data = m_new->m_ext.ext_buf = (void *)buf;
		m_new->m_flags |= M_EXT;
		m_new->m_len = m_new->m_pkthdr.len =
		    m_new->m_ext.ext_size = TI_JUMBO_FRAMELEN;
		m_new->m_ext.ext_free = ti_jfree;
		m_new->m_ext.ext_arg = sc;
		MCLINITREFERENCE(m_new);
	} else {
		m_new = m;
		m_new->m_data = m_new->m_ext.ext_buf;
		m_new->m_ext.ext_size = TI_JUMBO_FRAMELEN;
	}

	m_adj(m_new, ETHER_ALIGN);
	/* Set up the descriptor. */
	r = &sc->ti_rdata->ti_rx_jumbo_ring[i];
	sc->ti_cdata.ti_rx_jumbo_chain[i] = m_new;
	TI_HOSTADDR(r->ti_addr) = sc->jumbo_dmaaddr +
		((caddr_t)mtod(m_new, caddr_t)
		 - (caddr_t)sc->ti_cdata.ti_jumbo_buf);
	r->ti_type = TI_BDTYPE_RECV_JUMBO_BD;
	r->ti_flags = TI_BDFLAG_JUMBO_RING;
#ifdef TI_CSUM_OFFLOAD
	r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM|TI_BDFLAG_IP_CKSUM;
#endif
	r->ti_len = m_new->m_len;
	r->ti_idx = i;

	return(0);
}

/*
 * The standard receive ring has 512 entries in it. At 2K per mbuf cluster,
 * that's 1MB or memory, which is a lot. For now, we fill only the first
 * 256 ring entries and hope that our CPU is fast enough to keep up with
 * the NIC.
 */
static int ti_init_rx_ring_std(sc)
	struct ti_softc		*sc;
{
	int		i;
	struct ti_cmd_desc	cmd;

	for (i = 0; i < TI_SSLOTS; i++) {
		if (ti_newbuf_std(sc, i, NULL, 0) == ENOBUFS)
			return(ENOBUFS);
	};

	TI_UPDATE_STDPROD(sc, i - 1);
	sc->ti_std = i - 1;

	return(0);
}

static void ti_free_rx_ring_std(sc)
	struct ti_softc		*sc;
{
	int		i;

	for (i = 0; i < TI_STD_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_std_chain[i] != NULL) {
			m_freem(sc->ti_cdata.ti_rx_std_chain[i]);
			sc->ti_cdata.ti_rx_std_chain[i] = NULL;

			/* if (sc->std_dmamap[i] == 0) panic() */
			bus_dmamap_destroy(sc->sc_dmat, sc->std_dmamap[i]);
			sc->std_dmamap[i] = 0;
		}
		bzero((char *)&sc->ti_rdata->ti_rx_std_ring[i],
		    sizeof(struct ti_rx_desc));
	}

	return;
}

static int ti_init_rx_ring_jumbo(sc)
	struct ti_softc		*sc;
{
	int		i;
	struct ti_cmd_desc	cmd;

	for (i = 0; i < (TI_JSLOTS - 20); i++) {
		if (ti_newbuf_jumbo(sc, i, NULL) == ENOBUFS)
			return(ENOBUFS);
	};

	TI_UPDATE_JUMBOPROD(sc, i - 1);
	sc->ti_jumbo = i - 1;

	return(0);
}

static void ti_free_rx_ring_jumbo(sc)
	struct ti_softc		*sc;
{
	int		i;

	for (i = 0; i < TI_JUMBO_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_jumbo_chain[i] != NULL) {
			m_freem(sc->ti_cdata.ti_rx_jumbo_chain[i]);
			sc->ti_cdata.ti_rx_jumbo_chain[i] = NULL;
		}
		bzero((char *)&sc->ti_rdata->ti_rx_jumbo_ring[i],
		    sizeof(struct ti_rx_desc));
	}

	return;
}

static int ti_init_rx_ring_mini(sc)
	struct ti_softc		*sc;
{
	int		i;

	for (i = 0; i < TI_MSLOTS; i++) {
		if (ti_newbuf_mini(sc, i, NULL, 0) == ENOBUFS)
			return(ENOBUFS);
	};

	TI_UPDATE_MINIPROD(sc, i - 1);
	sc->ti_mini = i - 1;

	return(0);
}

static void ti_free_rx_ring_mini(sc)
	struct ti_softc		*sc;
{
	int		i;

	for (i = 0; i < TI_MINI_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_mini_chain[i] != NULL) {
			m_freem(sc->ti_cdata.ti_rx_mini_chain[i]);
			sc->ti_cdata.ti_rx_mini_chain[i] = NULL;

			/* if (sc->mini_dmamap[i] == 0) panic() */
			bus_dmamap_destroy(sc->sc_dmat, sc->mini_dmamap[i]);
			sc->mini_dmamap[i] = 0;
		}
		bzero((char *)&sc->ti_rdata->ti_rx_mini_ring[i],
		    sizeof(struct ti_rx_desc));
	}

	return;
}

static void ti_free_tx_ring(sc)
	struct ti_softc		*sc;
{
	int		i;
	struct txdmamap_pool_entry *dma;

	if (sc->ti_rdata->ti_tx_ring == NULL)
		return;

	for (i = 0; i < TI_TX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_tx_chain[i] != NULL) {
			m_freem(sc->ti_cdata.ti_tx_chain[i]);
			sc->ti_cdata.ti_tx_chain[i] = NULL;

			/* if (sc->txdma[i] == 0) panic() */
			SIMPLEQ_INSERT_HEAD(&sc->txdma_list, sc->txdma[i],
					    link);
			sc->txdma[i] = 0;
		}
		bzero((char *)&sc->ti_rdata->ti_tx_ring[i],
		    sizeof(struct ti_tx_desc));
	}

	while ((dma = SIMPLEQ_FIRST(&sc->txdma_list))) {
		SIMPLEQ_REMOVE_HEAD(&sc->txdma_list, dma, link);
		bus_dmamap_destroy(sc->sc_dmat, dma->dmamap);
		free(dma, M_DEVBUF);
	}

	return;
}

static int ti_init_tx_ring(sc)
	struct ti_softc		*sc;
{
	int i, error;
	bus_dmamap_t dmamap;
	struct txdmamap_pool_entry *dma;

	sc->ti_txcnt = 0;
	sc->ti_tx_saved_considx = 0;
	CSR_WRITE_4(sc, TI_MB_SENDPROD_IDX, 0);

	SIMPLEQ_INIT(&sc->txdma_list);
	for (i = 0; i < TI_RSLOTS; i++) {
		/* I've seen mbufs with 30 fragments. */
		if ((error = bus_dmamap_create(sc->sc_dmat, TI_JUMBO_FRAMELEN,
					       40, TI_JUMBO_FRAMELEN, 0,
					       BUS_DMA_NOWAIT, &dmamap)) != 0) {
			printf("%s: can't create tx map, error = %d\n",
			       sc->sc_dev.dv_xname, error);
			return(ENOMEM);
		}
		dma = malloc(sizeof(*dma), M_DEVBUF, M_NOWAIT);
		if (!dma) {
			printf("%s: can't alloc txdmamap_pool_entry\n",
			       sc->sc_dev.dv_xname);
			bus_dmamap_destroy(sc->sc_dmat, dmamap);
			return (ENOMEM);
		}
		dma->dmamap = dmamap;
		SIMPLEQ_INSERT_HEAD(&sc->txdma_list, dma, link);
	}

	return(0);
}

/*
 * The Tigon 2 firmware has a new way to add/delete multicast addresses,
 * but we have to support the old way too so that Tigon 1 cards will
 * work.
 */
void ti_add_mcast(sc, addr)
	struct ti_softc		*sc;
	struct ether_addr	*addr;
{
	struct ti_cmd_desc	cmd;
	u_int16_t		*m;
	u_int32_t		ext[2] = {0, 0};

	m = (u_int16_t *)&addr->ether_addr_octet[0]; /* XXX */

	switch(sc->ti_hwrev) {
	case TI_HWREV_TIGON:
		CSR_WRITE_4(sc, TI_GCR_MAR0, htons(m[0]));
		CSR_WRITE_4(sc, TI_GCR_MAR1, (htons(m[1]) << 16) | htons(m[2]));
		TI_DO_CMD(TI_CMD_ADD_MCAST_ADDR, 0, 0);
		break;
	case TI_HWREV_TIGON_II:
		ext[0] = htons(m[0]);
		ext[1] = (htons(m[1]) << 16) | htons(m[2]);
		TI_DO_CMD_EXT(TI_CMD_EXT_ADD_MCAST, 0, 0, (caddr_t)&ext, 2);
		break;
	default:
		printf("%s: unknown hwrev\n", sc->sc_dev.dv_xname);
		break;
	}

	return;
}

void ti_del_mcast(sc, addr)
	struct ti_softc		*sc;
	struct ether_addr	*addr;
{
	struct ti_cmd_desc	cmd;
	u_int16_t		*m;
	u_int32_t		ext[2] = {0, 0};

	m = (u_int16_t *)&addr->ether_addr_octet[0]; /* XXX */

	switch(sc->ti_hwrev) {
	case TI_HWREV_TIGON:
		CSR_WRITE_4(sc, TI_GCR_MAR0, htons(m[0]));
		CSR_WRITE_4(sc, TI_GCR_MAR1, (htons(m[1]) << 16) | htons(m[2]));
		TI_DO_CMD(TI_CMD_DEL_MCAST_ADDR, 0, 0);
		break;
	case TI_HWREV_TIGON_II:
		ext[0] = htons(m[0]);
		ext[1] = (htons(m[1]) << 16) | htons(m[2]);
		TI_DO_CMD_EXT(TI_CMD_EXT_DEL_MCAST, 0, 0, (caddr_t)&ext, 2);
		break;
	default:
		printf("%s: unknown hwrev\n", sc->sc_dev.dv_xname);
		break;
	}

	return;
}

/*
 * Configure the Tigon's multicast address filter.
 *
 * The actual multicast table management is a bit of a pain, thanks to
 * slight brain damage on the part of both Alteon and us. With our
 * multicast code, we are only alerted when the multicast address table
 * changes and at that point we only have the current list of addresses:
 * we only know the current state, not the previous state, so we don't
 * actually know what addresses were removed or added. The firmware has
 * state, but we can't get our grubby mits on it, and there is no 'delete
 * all multicast addresses' command. Hence, we have to maintain our own
 * state so we know what addresses have been programmed into the NIC at
 * any given time.
 */
static void ti_setmulti(sc)
	struct ti_softc		*sc;
{
	struct ifnet		*ifp;
	struct ti_cmd_desc	cmd;
	struct ti_mc_entry	*mc;
	u_int32_t		intrs;
	struct ether_multi *enm;
	struct ether_multistep step;

	ifp = &sc->ethercom.ec_if;

	/* Disable interrupts. */
	intrs = CSR_READ_4(sc, TI_MB_HOSTINTR);
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 1);

	/* First, zot all the existing filters. */
	while ((mc = SIMPLEQ_FIRST(&sc->ti_mc_listhead)) != NULL) {
		ti_del_mcast(sc, &mc->mc_addr);
		SIMPLEQ_REMOVE_HEAD(&sc->ti_mc_listhead, mc, mc_entries);
		free(mc, M_DEVBUF);
	}

	/*
	 * Remember all multicast addresses so that we can delete them
	 * later.  Punt if there is a range of addresses or memory shortage.
	 */
	ETHER_FIRST_MULTI(step, &sc->ethercom, enm);
	while (enm != NULL) {
		if (memcmp(enm->enm_addrlo, enm->enm_addrhi,
		    ETHER_ADDR_LEN) != 0)
			goto allmulti;
		if ((mc = malloc(sizeof(struct ti_mc_entry), M_DEVBUF,
		    M_NOWAIT)) == NULL)
			goto allmulti;
		memcpy(&mc->mc_addr, enm->enm_addrlo, ETHER_ADDR_LEN);
		SIMPLEQ_INSERT_HEAD(&sc->ti_mc_listhead, mc, mc_entries);
		ETHER_NEXT_MULTI(step, enm);
	}

	/* Accept only programmed multicast addresses */
	ifp->if_flags &= ~IFF_ALLMULTI;
	TI_DO_CMD(TI_CMD_SET_ALLMULTI, TI_CMD_CODE_ALLMULTI_DIS, 0);

	/* Now program new ones. */
	for (mc = SIMPLEQ_FIRST(&sc->ti_mc_listhead); mc != NULL;
	    mc = SIMPLEQ_NEXT(mc, mc_entries))
		ti_add_mcast(sc, &mc->mc_addr);

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, intrs);

	return;

allmulti:
	/* No need to keep individual multicast addresses */
	while ((mc = SIMPLEQ_FIRST(&sc->ti_mc_listhead)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->ti_mc_listhead, mc,
		    mc_entries);
		free(mc, M_DEVBUF);
	}

	/* Accept all multicast addresses */
	ifp->if_flags |= IFF_ALLMULTI;
	TI_DO_CMD(TI_CMD_SET_ALLMULTI, TI_CMD_CODE_ALLMULTI_ENB, 0);

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, intrs);
}

/*
 * Check to see if the BIOS has configured us for a 64 bit slot when
 * we aren't actually in one. If we detect this condition, we can work
 * around it on the Tigon 2 by setting a bit in the PCI state register,
 * but for the Tigon 1 we must give up and abort the interface attach.
 */
static int ti_64bitslot_war(sc)
	struct ti_softc		*sc;
{
	if (!(CSR_READ_4(sc, TI_PCI_STATE) & TI_PCISTATE_32BIT_BUS)) {
		CSR_WRITE_4(sc, 0x600, 0);
		CSR_WRITE_4(sc, 0x604, 0);
		CSR_WRITE_4(sc, 0x600, 0x5555AAAA);
		if (CSR_READ_4(sc, 0x604) == 0x5555AAAA) {
			if (sc->ti_hwrev == TI_HWREV_TIGON)
				return(EINVAL);
			else {
				TI_SETBIT(sc, TI_PCI_STATE,
				    TI_PCISTATE_32BIT_BUS);
				return(0);
			}
		}
	}

	return(0);
}

/*
 * Do endian, PCI and DMA initialization. Also check the on-board ROM
 * self-test results.
 */
static int ti_chipinit(sc)
	struct ti_softc		*sc;
{
	u_int32_t		cacheline;
	u_int32_t		pci_writemax = 0;

	/* Initialize link to down state. */
	sc->ti_linkstat = TI_EV_CODE_LINK_DOWN;

	/* Set endianness before we access any non-PCI registers. */
#if BYTE_ORDER == BIG_ENDIAN
	CSR_WRITE_4(sc, TI_MISC_HOST_CTL,
	    TI_MHC_BIGENDIAN_INIT | (TI_MHC_BIGENDIAN_INIT << 24));
#else
	CSR_WRITE_4(sc, TI_MISC_HOST_CTL,
	    TI_MHC_LITTLEENDIAN_INIT | (TI_MHC_LITTLEENDIAN_INIT << 24));
#endif

	/* Check the ROM failed bit to see if self-tests passed. */
	if (CSR_READ_4(sc, TI_CPU_STATE) & TI_CPUSTATE_ROMFAIL) {
		printf("%s: board self-diagnostics failed!\n",
		       sc->sc_dev.dv_xname);
		return(ENODEV);
	}

	/* Halt the CPU. */
	TI_SETBIT(sc, TI_CPU_STATE, TI_CPUSTATE_HALT);

	/* Figure out the hardware revision. */
	switch(CSR_READ_4(sc, TI_MISC_HOST_CTL) & TI_MHC_CHIP_REV_MASK) {
	case TI_REV_TIGON_I:
		sc->ti_hwrev = TI_HWREV_TIGON;
		break;
	case TI_REV_TIGON_II:
		sc->ti_hwrev = TI_HWREV_TIGON_II;
		break;
	default:
		printf("%s: unsupported chip revision\n", sc->sc_dev.dv_xname);
		return(ENODEV);
	}

	/* Do special setup for Tigon 2. */
	if (sc->ti_hwrev == TI_HWREV_TIGON_II) {
		TI_SETBIT(sc, TI_CPU_CTL_B, TI_CPUSTATE_HALT);
		TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_SRAM_BANK_256K);
		TI_SETBIT(sc, TI_MISC_CONF, TI_MCR_SRAM_SYNCHRONOUS);
	}

	/* Set up the PCI state register. */
	CSR_WRITE_4(sc, TI_PCI_STATE, TI_PCI_READ_CMD|TI_PCI_WRITE_CMD);
	if (sc->ti_hwrev == TI_HWREV_TIGON_II) {
		TI_SETBIT(sc, TI_PCI_STATE, TI_PCISTATE_USE_MEM_RD_MULT);
	}

	/* Clear the read/write max DMA parameters. */
	TI_CLRBIT(sc, TI_PCI_STATE, (TI_PCISTATE_WRITE_MAXDMA|
	    TI_PCISTATE_READ_MAXDMA));

	/* Get cache line size. */
	cacheline = PCI_CACHELINE(CSR_READ_4(sc, PCI_BHLC_REG));

	/*
	 * If the system has set enabled the PCI memory write
	 * and invalidate command in the command register, set
	 * the write max parameter accordingly. This is necessary
	 * to use MWI with the Tigon 2.
	 */
	if (CSR_READ_4(sc, PCI_COMMAND_STATUS_REG)
	    & PCI_COMMAND_INVALIDATE_ENABLE) {
		switch(cacheline) {
		case 1:
		case 4:
		case 8:
		case 16:
		case 32:
		case 64:
			break;
		default:
		/* Disable PCI memory write and invalidate. */
			if (bootverbose)
				printf("%s: cache line size %d not "
				    "supported; disabling PCI MWI\n",
				    sc->sc_dev.dv_xname, cacheline);
			CSR_WRITE_4(sc, PCI_COMMAND_STATUS_REG,
				    CSR_READ_4(sc, PCI_COMMAND_STATUS_REG)
				    & ~PCI_COMMAND_INVALIDATE_ENABLE);
			break;
		}
	}

#ifdef __brokenalpha__
	/*
	 * From the Alteon sample driver:
	 * Must insure that we do not cross an 8K (bytes) boundary
	 * for DMA reads.  Our highest limit is 1K bytes.  This is a 
	 * restriction on some ALPHA platforms with early revision 
	 * 21174 PCI chipsets, such as the AlphaPC 164lx 
	 */
	TI_SETBIT(sc, TI_PCI_STATE, pci_writemax|TI_PCI_READMAX_1024);
#else
	TI_SETBIT(sc, TI_PCI_STATE, pci_writemax);
#endif

	/* This sets the min dma param all the way up (0xff). */
	TI_SETBIT(sc, TI_PCI_STATE, TI_PCISTATE_MINDMA);

	/* Configure DMA variables. */
#if BYTE_ORDER == BIG_ENDIAN
	CSR_WRITE_4(sc, TI_GCR_OPMODE, TI_OPMODE_BYTESWAP_BD |
	    TI_OPMODE_BYTESWAP_DATA | TI_OPMODE_WORDSWAP_BD |
	    TI_OPMODE_WARN_ENB | TI_OPMODE_FATAL_ENB |
	    TI_OPMODE_DONT_FRAG_JUMBO);
#else
	CSR_WRITE_4(sc, TI_GCR_OPMODE, TI_OPMODE_BYTESWAP_DATA|
	    TI_OPMODE_WORDSWAP_BD|TI_OPMODE_DONT_FRAG_JUMBO|
	    TI_OPMODE_WARN_ENB|TI_OPMODE_FATAL_ENB);
#endif

	/*
	 * Only allow 1 DMA channel to be active at a time.
	 * I don't think this is a good idea, but without it
	 * the firmware racks up lots of nicDmaReadRingFull
	 * errors.
	 */
#ifndef TI_CSUM_OFFLOAD
	TI_SETBIT(sc, TI_GCR_OPMODE, TI_OPMODE_1_DMA_ACTIVE);
#endif

	/* Recommended settings from Tigon manual. */
	CSR_WRITE_4(sc, TI_GCR_DMA_WRITECFG, TI_DMA_STATE_THRESH_8W);
	CSR_WRITE_4(sc, TI_GCR_DMA_READCFG, TI_DMA_STATE_THRESH_8W);

	if (ti_64bitslot_war(sc)) {
		printf("%s: bios thinks we're in a 64 bit slot, "
		    "but we aren't", sc->sc_dev.dv_xname);
		return(EINVAL);
	}

	return(0);
}

/*
 * Initialize the general information block and firmware, and
 * start the CPU(s) running.
 */
static int ti_gibinit(sc)
	struct ti_softc		*sc;
{
	struct ti_rcb		*rcb;
	int			i;
	struct ifnet		*ifp;

	ifp = &sc->ethercom.ec_if;

	/* Disable interrupts for now. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 1);

	/* Tell the chip where to find the general information block. */
	CSR_WRITE_4(sc, TI_GCR_GENINFO_HI, 0);
	CSR_WRITE_4(sc, TI_GCR_GENINFO_LO, sc->info_dmaaddr +
		    ((caddr_t)&sc->ti_rdata->ti_info - (caddr_t)sc->ti_rdata));

	/* Load the firmware into SRAM. */
	ti_loadfw(sc);

	/* Set up the contents of the general info and ring control blocks. */

	/* Set up the event ring and producer pointer. */
	rcb = &sc->ti_rdata->ti_info.ti_ev_rcb;

	TI_HOSTADDR(rcb->ti_hostaddr) = sc->info_dmaaddr +
		((caddr_t)&sc->ti_rdata->ti_event_ring - (caddr_t)sc->ti_rdata);
	rcb->ti_flags = 0;
	TI_HOSTADDR(sc->ti_rdata->ti_info.ti_ev_prodidx_ptr) =
	    sc->info_dmaaddr + ((caddr_t)&sc->ti_rdata->ti_ev_prodidx_r
				- (caddr_t)sc->ti_rdata);
	sc->ti_ev_prodidx.ti_idx = 0;
	CSR_WRITE_4(sc, TI_GCR_EVENTCONS_IDX, 0);
	sc->ti_ev_saved_considx = 0;

	/* Set up the command ring and producer mailbox. */
	rcb = &sc->ti_rdata->ti_info.ti_cmd_rcb;

	TI_HOSTADDR(rcb->ti_hostaddr) = TI_GCR_NIC_ADDR(TI_GCR_CMDRING);
	rcb->ti_flags = 0;
	rcb->ti_max_len = 0;
	for (i = 0; i < TI_CMD_RING_CNT; i++) {
		CSR_WRITE_4(sc, TI_GCR_CMDRING + (i * 4), 0);
	}
	CSR_WRITE_4(sc, TI_GCR_CMDCONS_IDX, 0);
	CSR_WRITE_4(sc, TI_MB_CMDPROD_IDX, 0);
	sc->ti_cmd_saved_prodidx = 0;

	/*
	 * Assign the address of the stats refresh buffer.
	 * We re-use the current stats buffer for this to
	 * conserve memory.
	 */
	TI_HOSTADDR(sc->ti_rdata->ti_info.ti_refresh_stats_ptr) =
	    sc->info_dmaaddr + ((caddr_t)&sc->ti_rdata->ti_info.ti_stats
		    - (caddr_t)sc->ti_rdata);

	/* Set up the standard receive ring. */
	rcb = &sc->ti_rdata->ti_info.ti_std_rx_rcb;
	TI_HOSTADDR(rcb->ti_hostaddr) = sc->info_dmaaddr +
		((caddr_t)&sc->ti_rdata->ti_rx_std_ring
		 - (caddr_t)sc->ti_rdata);
	rcb->ti_max_len = TI_FRAMELEN;
	rcb->ti_flags = 0;
#ifdef TI_CSUM_OFFLOAD
	rcb->ti_flags |= TI_RCB_FLAG_TCP_UDP_CKSUM|TI_RCB_FLAG_IP_CKSUM;
#endif
	rcb->ti_flags |= TI_RCB_FLAG_VLAN_ASSIST;

	/* Set up the jumbo receive ring. */
	rcb = &sc->ti_rdata->ti_info.ti_jumbo_rx_rcb;
	TI_HOSTADDR(rcb->ti_hostaddr) = sc->info_dmaaddr +
	    ((caddr_t)&sc->ti_rdata->ti_rx_jumbo_ring - (caddr_t)sc->ti_rdata);
	rcb->ti_max_len = TI_JUMBO_FRAMELEN;
	rcb->ti_flags = 0;
#ifdef TI_CSUM_OFFLOAD
	rcb->ti_flags |= TI_RCB_FLAG_TCP_UDP_CKSUM|TI_RCB_FLAG_IP_CKSUM;
#endif
	rcb->ti_flags |= TI_RCB_FLAG_VLAN_ASSIST;

	/*
	 * Set up the mini ring. Only activated on the
	 * Tigon 2 but the slot in the config block is
	 * still there on the Tigon 1.
	 */
	rcb = &sc->ti_rdata->ti_info.ti_mini_rx_rcb;
	TI_HOSTADDR(rcb->ti_hostaddr) = sc->info_dmaaddr +
	    ((caddr_t)&sc->ti_rdata->ti_rx_mini_ring - (caddr_t)sc->ti_rdata);
	rcb->ti_max_len = MHLEN - ETHER_ALIGN;
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		rcb->ti_flags = TI_RCB_FLAG_RING_DISABLED;
	else
		rcb->ti_flags = 0;
#ifdef TI_CSUM_OFFLOAD
	rcb->ti_flags |= TI_RCB_FLAG_TCP_UDP_CKSUM|TI_RCB_FLAG_IP_CKSUM;
#endif
	rcb->ti_flags |= TI_RCB_FLAG_VLAN_ASSIST;

	/*
	 * Set up the receive return ring.
	 */
	rcb = &sc->ti_rdata->ti_info.ti_return_rcb;
	TI_HOSTADDR(rcb->ti_hostaddr) = sc->info_dmaaddr +
	    ((caddr_t)&sc->ti_rdata->ti_rx_return_ring - (caddr_t)sc->ti_rdata);
	rcb->ti_flags = 0;
	rcb->ti_max_len = TI_RETURN_RING_CNT;
	TI_HOSTADDR(sc->ti_rdata->ti_info.ti_return_prodidx_ptr) =
	    sc->info_dmaaddr + ((caddr_t)&sc->ti_rdata->ti_return_prodidx_r
		    - (caddr_t)sc->ti_rdata);

	/*
	 * Set up the tx ring. Note: for the Tigon 2, we have the option
	 * of putting the transmit ring in the host's address space and
	 * letting the chip DMA it instead of leaving the ring in the NIC's
	 * memory and accessing it through the shared memory region. We
	 * do this for the Tigon 2, but it doesn't work on the Tigon 1,
	 * so we have to revert to the shared memory scheme if we detect
	 * a Tigon 1 chip.
	 */
	CSR_WRITE_4(sc, TI_WINBASE, TI_TX_RING_BASE);
	if (sc->ti_hwrev == TI_HWREV_TIGON) {
		sc->ti_rdata->ti_tx_ring_nic =
		    (struct ti_tx_desc *)(sc->ti_vhandle + TI_WINDOW);
	}
	bzero((char *)sc->ti_rdata->ti_tx_ring,
	    TI_TX_RING_CNT * sizeof(struct ti_tx_desc));
	rcb = &sc->ti_rdata->ti_info.ti_tx_rcb;
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		rcb->ti_flags = 0;
	else
		rcb->ti_flags = TI_RCB_FLAG_HOST_RING;
	rcb->ti_flags |= TI_RCB_FLAG_VLAN_ASSIST;
	rcb->ti_max_len = TI_TX_RING_CNT;
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		TI_HOSTADDR(rcb->ti_hostaddr) = TI_TX_RING_BASE;
	else
		TI_HOSTADDR(rcb->ti_hostaddr) = sc->info_dmaaddr +
		    ((caddr_t)&sc->ti_rdata->ti_tx_ring
		     - (caddr_t)sc->ti_rdata);
	TI_HOSTADDR(sc->ti_rdata->ti_info.ti_tx_considx_ptr) =
	    sc->info_dmaaddr + ((caddr_t)&sc->ti_rdata->ti_tx_considx_r
		    - (caddr_t)sc->ti_rdata);

	/* Set up tuneables */
	if (ifp->if_mtu > (ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN) ||
	    (sc->ethercom.ec_capenable & ETHERCAP_VLAN_MTU))
		CSR_WRITE_4(sc, TI_GCR_RX_COAL_TICKS,
		    (sc->ti_rx_coal_ticks / 10));
	else
		CSR_WRITE_4(sc, TI_GCR_RX_COAL_TICKS, sc->ti_rx_coal_ticks);
	CSR_WRITE_4(sc, TI_GCR_TX_COAL_TICKS, sc->ti_tx_coal_ticks);
	CSR_WRITE_4(sc, TI_GCR_STAT_TICKS, sc->ti_stat_ticks);
	CSR_WRITE_4(sc, TI_GCR_RX_MAX_COAL_BD, sc->ti_rx_max_coal_bds);
	CSR_WRITE_4(sc, TI_GCR_TX_MAX_COAL_BD, sc->ti_tx_max_coal_bds);
	CSR_WRITE_4(sc, TI_GCR_TX_BUFFER_RATIO, sc->ti_tx_buf_ratio);

	/* Turn interrupts on. */
	CSR_WRITE_4(sc, TI_GCR_MASK_INTRS, 0);
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 0);

	/* Start CPU. */
	TI_CLRBIT(sc, TI_CPU_STATE, (TI_CPUSTATE_HALT|TI_CPUSTATE_STEP));

	return(0);
}

/*
 * look for id in the device list, returning the first match
 */
static struct ti_type * ti_type_match(pa)
	struct pci_attach_args *pa;
{
	struct ti_type          *t;

	t = ti_devs;
	while(t->ti_name != NULL) {
		if ((PCI_VENDOR(pa->pa_id) == t->ti_vid) &&
		    (PCI_PRODUCT(pa->pa_id) == t->ti_did)) {
			return (t);
		}
		t++;
	}

	return(NULL);
}

/*
 * Probe for a Tigon chip. Check the PCI vendor and device IDs
 * against our list and return its name if we find a match.
 */
static int ti_probe(parent, match, aux)
	struct device *parent;
	struct cfdata *match;
	void *aux;
{
	struct pci_attach_args *pa = aux;
	struct ti_type		*t;

	t = ti_type_match(pa);

	return((t == NULL) ? 0 : 1);
}

static void ti_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	u_int32_t		command;
	struct ifnet		*ifp;
	struct ti_softc		*sc;
	u_char eaddr[ETHER_ADDR_LEN];
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	bus_dma_segment_t dmaseg;
	int error, dmanseg, nolinear;
	struct ti_type		*t;

	t = ti_type_match(pa);
	if (t == NULL) {
		printf("ti_attach: were did the card go ?\n");
		return;
	}

	printf(": %s (rev. 0x%02x)\n", t->ti_name, PCI_REVISION(pa->pa_class));

	sc = (struct ti_softc *)self;

	/*
	 * Map control/status registers.
	 */
	nolinear = 0;
	if (pci_mapreg_map(pa, 0x10,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT,
	    BUS_SPACE_MAP_LINEAR , &sc->ti_btag, &sc->ti_bhandle,
	    NULL, NULL)) {
		nolinear = 1;
		if (pci_mapreg_map(pa, 0x10,
		    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT,
		    0 , &sc->ti_btag, &sc->ti_bhandle, NULL, NULL)) {
			printf(": can't map memory space\n");
			return;
		}
	}
	if (nolinear == 0)
		sc->ti_vhandle = (void *)(sc->ti_bhandle); /* XXX XXX XXX */
	else 
		sc->ti_vhandle = NULL;

	command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	command |= PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, command);

	/* Allocate interrupt */
	if (pci_intr_map(pc, pa->pa_intrtag, pa->pa_intrpin,
	    pa->pa_intrline, &ih)) {
		printf("%s: couldn't map interrupt\n", sc->sc_dev.dv_xname);
		return;;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, ti_intr, sc);
	if (sc->sc_ih == NULL) {
		printf("%s: couldn't establish interrupt",
		    sc->sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;;
	}
	printf("%s: interrupting at %s\n", sc->sc_dev.dv_xname, intrstr);
	/*
	 * Add shutdown hook so that DMA is disabled prior to reboot. Not
	 * doing do could allow DMA to corrupt kernel memory during the
	 * reboot before the driver initializes.
	 */ 
	(void) shutdownhook_establish(ti_shutdown, sc);

	if (ti_chipinit(sc)) {
		printf("%s: chip initialization failed\n", self->dv_xname);
		goto fail2;
	}
	if (sc->ti_hwrev == TI_HWREV_TIGON && nolinear == 1) {
		printf("%s: memory space not mapped linear\n", self->dv_xname);
	}

	/* Zero out the NIC's on-board SRAM. */
	ti_mem(sc, 0x2000, 0x100000 - 0x2000,  NULL);

	/* Init again -- zeroing memory may have clobbered some registers. */
	if (ti_chipinit(sc)) {
		printf("%s: chip initialization failed\n", self->dv_xname);
		goto fail2;
	}

	/*
	 * Get station address from the EEPROM. Note: the manual states
	 * that the MAC address is at offset 0x8c, however the data is
	 * stored as two longwords (since that's how it's loaded into
	 * the NIC). This means the MAC address is actually preceeded
	 * by two zero bytes. We need to skip over those.
	 */
	if (ti_read_eeprom(sc, (caddr_t)&eaddr,
				TI_EE_MAC_OFFSET + 2, ETHER_ADDR_LEN)) {
		printf("%s: failed to read station address\n", self->dv_xname);
		goto fail2;
	}

	/*
	 * A Tigon chip was detected. Inform the world.
	 */
	printf("%s: Ethernet address: %s\n", self->dv_xname,
				ether_sprintf(eaddr));

	sc->sc_dmat = pa->pa_dmat;

	/* Allocate the general information block and ring buffers. */
	if ((error = bus_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct ti_ring_data), NBPG, 0, &dmaseg, 1, &dmanseg,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("%s: can't allocate ring buffer, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		goto fail2;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &dmaseg, dmanseg,
	    sizeof(struct ti_ring_data), (caddr_t *)&sc->ti_rdata,
	    BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("%s: can't map ring buffer, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		goto fail2;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat,
	    sizeof(struct ti_ring_data), 1,
	    sizeof(struct ti_ring_data), 0, BUS_DMA_NOWAIT,
	    &sc->info_dmamap)) != 0) {
		printf("%s: can't create ring buffer DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		goto fail2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->info_dmamap,
	    sc->ti_rdata, sizeof(struct ti_ring_data), NULL,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("%s: can't load ring buffer DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		goto fail2;
	}

	sc->info_dmaaddr = sc->info_dmamap->dm_segs[0].ds_addr;

	bzero(sc->ti_rdata, sizeof(struct ti_ring_data));

	/* Try to allocate memory for jumbo buffers. */
	if (ti_alloc_jumbo_mem(sc)) {
		printf("%s: jumbo buffer allocation failed\n", self->dv_xname);
		goto fail2;
	}

	SIMPLEQ_INIT(&sc->ti_mc_listhead);

	/*
	 * We really need a better way to tell a 1000baseTX card
	 * from a 1000baseSX one, since in theory there could be
	 * OEMed 1000baseTX cards from lame vendors who aren't
	 * clever enough to change the PCI ID. For the moment
	 * though, the AceNIC is the only copper card available.
	 */
	if ((PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ALTEON &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ALTEON_ACENIC_COPPER) ||
	    (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_NETGEAR &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_NETGEAR_GA620T))
		sc->ti_copper = 1;
	else
		sc->ti_copper = 0;

	/* Set default tuneable values. */
	sc->ti_stat_ticks = 2 * TI_TICKS_PER_SEC;
	sc->ti_rx_coal_ticks = TI_TICKS_PER_SEC / 5000;
	sc->ti_tx_coal_ticks = TI_TICKS_PER_SEC / 500;
	sc->ti_rx_max_coal_bds = 64;
	sc->ti_tx_max_coal_bds = 128;
	sc->ti_tx_buf_ratio = 21;

	/* Set up ifnet structure */
	ifp = &sc->ethercom.ec_if;
	ifp->if_softc = sc;
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ti_ioctl;
	ifp->if_start = ti_start;
	ifp->if_watchdog = ti_watchdog;
	IFQ_SET_MAXLEN(&ifp->if_snd, TI_TX_RING_CNT - 1);
	IFQ_SET_READY(&ifp->if_snd);

	/*
	 * We can support 802.1Q VLAN-sized frames.
	 */
	sc->ethercom.ec_capabilities |=
	    ETHERCAP_VLAN_MTU | ETHERCAP_VLAN_HWTAGGING;

	/* Set up ifmedia support. */
	ifmedia_init(&sc->ifmedia, IFM_IMASK, ti_ifmedia_upd, ti_ifmedia_sts);
	if (sc->ti_copper) {
                /*
                 * Copper cards allow manual 10/100 mode selection,
                 * but not manual 1000baseTX mode selection. Why?
                 * Becuase currently there's no way to specify the
                 * master/slave setting through the firmware interface,
                 * so Alteon decided to just bag it and handle it
                 * via autonegotiation.
                 */
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T, 0, NULL);
                ifmedia_add(&sc->ifmedia,
                    IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_100_TX, 0, NULL);
                ifmedia_add(&sc->ifmedia,
                    IFM_ETHER|IFM_100_TX|IFM_FDX, 0, NULL);
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_1000_TX, 0, NULL);
                ifmedia_add(&sc->ifmedia,
                    IFM_ETHER|IFM_1000_TX|IFM_FDX, 0, NULL);
	} else {
		/* Fiber cards don't support 10/100 modes. */
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_1000_SX, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_1000_SX|IFM_FDX, 0, NULL);
	}
	ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->ifmedia, IFM_ETHER|IFM_AUTO);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp, eaddr);

#if NBPFILTER > 0
	bpfattach(&sc->ethercom.ec_if.if_bpf, ifp, DLT_EN10MB,
		  sizeof(struct ether_header));
#endif

	return;
fail2:
	pci_intr_disestablish(pc, sc->sc_ih);
	return;
}

/*
 * Frame reception handling. This is called if there's a frame
 * on the receive return list.
 *
 * Note: we have to be able to handle three possibilities here:
 * 1) the frame is from the mini receive ring (can only happen)
 *    on Tigon 2 boards)
 * 2) the frame is from the jumbo recieve ring
 * 3) the frame is from the standard receive ring
 */

static void ti_rxeof(sc)
	struct ti_softc		*sc;
{
	struct ifnet		*ifp;
	struct ti_cmd_desc	cmd;

	ifp = &sc->ethercom.ec_if;

	while(sc->ti_rx_saved_considx != sc->ti_return_prodidx.ti_idx) {
		struct ti_rx_desc	*cur_rx;
		u_int32_t		rxidx;
		struct ether_header	*eh;
		struct mbuf		*m = NULL;
		u_int16_t		vlan_tag = 0;
		int			have_tag = 0;
#ifdef TI_CSUM_OFFLOAD
		struct ip		*ip;
#endif
		bus_dmamap_t dmamap;

		cur_rx =
		    &sc->ti_rdata->ti_rx_return_ring[sc->ti_rx_saved_considx];
		rxidx = cur_rx->ti_idx;
		TI_INC(sc->ti_rx_saved_considx, TI_RETURN_RING_CNT);

		if (cur_rx->ti_flags & TI_BDFLAG_VLAN_TAG) {
			have_tag = 1;
			vlan_tag = cur_rx->ti_vlan_tag;
		}

		if (cur_rx->ti_flags & TI_BDFLAG_JUMBO_RING) {
			TI_INC(sc->ti_jumbo, TI_JUMBO_RX_RING_CNT);
			m = sc->ti_cdata.ti_rx_jumbo_chain[rxidx];
			sc->ti_cdata.ti_rx_jumbo_chain[rxidx] = NULL;
			if (cur_rx->ti_flags & TI_BDFLAG_ERROR) {
				ifp->if_ierrors++;
				ti_newbuf_jumbo(sc, sc->ti_jumbo, m);
				continue;
			}
			if (ti_newbuf_jumbo(sc, sc->ti_jumbo, NULL)
			    == ENOBUFS) {
				ifp->if_ierrors++;
				ti_newbuf_jumbo(sc, sc->ti_jumbo, m);
				continue;
			}
		} else if (cur_rx->ti_flags & TI_BDFLAG_MINI_RING) {
			TI_INC(sc->ti_mini, TI_MINI_RX_RING_CNT);
			m = sc->ti_cdata.ti_rx_mini_chain[rxidx];
			sc->ti_cdata.ti_rx_mini_chain[rxidx] = NULL;
			dmamap = sc->mini_dmamap[rxidx];
			sc->mini_dmamap[rxidx] = 0;
			if (cur_rx->ti_flags & TI_BDFLAG_ERROR) {
				ifp->if_ierrors++;
				ti_newbuf_mini(sc, sc->ti_mini, m, dmamap);
				continue;
			}
			if (ti_newbuf_mini(sc, sc->ti_mini, NULL, dmamap)
			    == ENOBUFS) {
				ifp->if_ierrors++;
				ti_newbuf_mini(sc, sc->ti_mini, m, dmamap);
				continue;
			}
		} else {
			TI_INC(sc->ti_std, TI_STD_RX_RING_CNT);
			m = sc->ti_cdata.ti_rx_std_chain[rxidx];
			sc->ti_cdata.ti_rx_std_chain[rxidx] = NULL;
			dmamap = sc->std_dmamap[rxidx];
			sc->std_dmamap[rxidx] = 0;
			if (cur_rx->ti_flags & TI_BDFLAG_ERROR) {
				ifp->if_ierrors++;
				ti_newbuf_std(sc, sc->ti_std, m, dmamap);
				continue;
			}
			if (ti_newbuf_std(sc, sc->ti_std, NULL, dmamap)
			    == ENOBUFS) {
				ifp->if_ierrors++;
				ti_newbuf_std(sc, sc->ti_std, m, dmamap);
				continue;
			}
		}

		m->m_pkthdr.len = m->m_len = cur_rx->ti_len;
		ifp->if_ipackets++;
		eh = mtod(m, struct ether_header *);
		m->m_pkthdr.rcvif = ifp;

#if NBPFILTER > 0
		/*
	 	 * Handle BPF listeners. Let the BPF user see the packet, but
	 	 * don't pass it up to the ether_input() layer unless it's
	 	 * a broadcast packet, multicast packet, matches our ethernet
	 	 * address or the interface is in promiscuous mode.
	 	 */
		if (ifp->if_bpf) {
			bpf_mtap(ifp->if_bpf, m);
			if (ifp->if_flags & IFF_PROMISC &&
				(bcmp(eh->ether_dhost, LLADDR(ifp->if_sadl),
		 			ETHER_ADDR_LEN) &&
					(eh->ether_dhost[0] & 1) == 0)) {
				m_freem(m);
				continue;
			}
		}
#endif

#ifdef TI_CSUM_OFFLOAD /* XXX NetBSD: broken because m points to ether pkt */
		ip = mtod(m, struct ip *);
		if (!(cur_rx->ti_tcp_udp_cksum ^ 0xFFFF) &&
		    !(ip->ip_off & htons(IP_MF | IP_OFFMASK | IP_RF)))
			m->m_flags |= M_HWCKSUM;
#endif

		if (have_tag) {
			struct mbuf *n;
			n = m_aux_add(m, AF_LINK, ETHERTYPE_VLAN);
			if (n) {
				*mtod(n, int *) = vlan_tag;
				n->m_len = sizeof(int);
			} else {
				printf("%s: no mbuf for tag\n", ifp->if_xname);
				m_freem(m);
				continue;
			}
			have_tag = vlan_tag = 0;
		}
		(*ifp->if_input)(ifp, m);
	}

	/* Only necessary on the Tigon 1. */
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		CSR_WRITE_4(sc, TI_GCR_RXRETURNCONS_IDX,
		    sc->ti_rx_saved_considx);

	TI_UPDATE_STDPROD(sc, sc->ti_std);
	TI_UPDATE_MINIPROD(sc, sc->ti_mini);
	TI_UPDATE_JUMBOPROD(sc, sc->ti_jumbo);

	return;
}

static void ti_txeof(sc)
	struct ti_softc		*sc;
{
	struct ti_tx_desc	*cur_tx = NULL;
	struct ifnet		*ifp;

	ifp = &sc->ethercom.ec_if;

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	while (sc->ti_tx_saved_considx != sc->ti_tx_considx.ti_idx) {
		u_int32_t		idx = 0;

		idx = sc->ti_tx_saved_considx;
		if (sc->ti_hwrev == TI_HWREV_TIGON) {
			if (idx > 383)
				CSR_WRITE_4(sc, TI_WINBASE,
				    TI_TX_RING_BASE + 6144);
			else if (idx > 255)
				CSR_WRITE_4(sc, TI_WINBASE,
				    TI_TX_RING_BASE + 4096);
			else if (idx > 127)
				CSR_WRITE_4(sc, TI_WINBASE,
				    TI_TX_RING_BASE + 2048);
			else
				CSR_WRITE_4(sc, TI_WINBASE,
				    TI_TX_RING_BASE);
			cur_tx = &sc->ti_rdata->ti_tx_ring_nic[idx % 128];
		} else
			cur_tx = &sc->ti_rdata->ti_tx_ring[idx];
		if (cur_tx->ti_flags & TI_BDFLAG_END)
			ifp->if_opackets++;
		if (sc->ti_cdata.ti_tx_chain[idx] != NULL) {
			m_freem(sc->ti_cdata.ti_tx_chain[idx]);
			sc->ti_cdata.ti_tx_chain[idx] = NULL;

			/* if (sc->txdma[idx] == 0) panic() */
			SIMPLEQ_INSERT_HEAD(&sc->txdma_list, sc->txdma[idx],
					    link);
			sc->txdma[idx] = 0;
		}
		sc->ti_txcnt--;
		TI_INC(sc->ti_tx_saved_considx, TI_TX_RING_CNT);
		ifp->if_timer = 0;
	}

	if (cur_tx != NULL)
		ifp->if_flags &= ~IFF_OACTIVE;

	return;
}

static int ti_intr(xsc)
	void			*xsc;
{
	struct ti_softc		*sc;
	struct ifnet		*ifp;

	sc = xsc;
	ifp = &sc->ethercom.ec_if;

#ifdef notdef
	/* Avoid this for now -- checking this register is expensive. */
	/* Make sure this is really our interrupt. */
	if (!(CSR_READ_4(sc, TI_MISC_HOST_CTL) & TI_MHC_INTSTATE))
		return (0);
#endif

	/* Ack interrupt and stop others from occuring. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 1);

	if (ifp->if_flags & IFF_RUNNING) {
		/* Check RX return ring producer/consumer */
		ti_rxeof(sc);

		/* Check TX ring producer/consumer */
		ti_txeof(sc);
	}

	ti_handle_events(sc);

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 0);

	if ((ifp->if_flags & IFF_RUNNING) != 0 &&
	    IFQ_IS_EMPTY(&ifp->if_snd) == 0)
		ti_start(ifp);

	return (1);
}

static void ti_stats_update(sc)
	struct ti_softc		*sc;
{
	struct ifnet		*ifp;

	ifp = &sc->ethercom.ec_if;

	ifp->if_collisions +=
	   (sc->ti_rdata->ti_info.ti_stats.dot3StatsSingleCollisionFrames +
	   sc->ti_rdata->ti_info.ti_stats.dot3StatsMultipleCollisionFrames +
	   sc->ti_rdata->ti_info.ti_stats.dot3StatsExcessiveCollisions +
	   sc->ti_rdata->ti_info.ti_stats.dot3StatsLateCollisions) -
	   ifp->if_collisions;

	return;
}

/*
 * Encapsulate an mbuf chain in the tx ring  by coupling the mbuf data
 * pointers to descriptors.
 */
static int ti_encap(sc, m_head, txidx)
	struct ti_softc		*sc;
	struct mbuf		*m_head;
	u_int32_t		*txidx;
{
	struct ti_tx_desc	*f = NULL;
	u_int32_t		frag, cur, cnt = 0;
	struct txdmamap_pool_entry *dma;
	bus_dmamap_t dmamap;
	int error, i;
	struct mbuf *n;

	dma = SIMPLEQ_FIRST(&sc->txdma_list);
	if (dma == NULL) {
		return ENOMEM;
	}
	dmamap = dma->dmamap;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, dmamap, m_head, 0);
	if (error) {
		struct mbuf *m;
		int i = 0;
		for (m = m_head; m; m = m->m_next)
			i++;
		printf("ti_encap: bus_dmamap_load_mbuf (len %d, %d frags) "
		       "error %d\n", m_head->m_pkthdr.len, i, error);
		return (ENOMEM);
	}

	cur = frag = *txidx;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	for (i = 0; i < dmamap->dm_nsegs; i++) {
			if (sc->ti_hwrev == TI_HWREV_TIGON) {
				if (frag > 383)
					CSR_WRITE_4(sc, TI_WINBASE,
					    TI_TX_RING_BASE + 6144);
				else if (frag > 255)
					CSR_WRITE_4(sc, TI_WINBASE,
					    TI_TX_RING_BASE + 4096);
				else if (frag > 127)
					CSR_WRITE_4(sc, TI_WINBASE,
					    TI_TX_RING_BASE + 2048);
				else
					CSR_WRITE_4(sc, TI_WINBASE,
					    TI_TX_RING_BASE);
				f = &sc->ti_rdata->ti_tx_ring_nic[frag % 128];
			} else
				f = &sc->ti_rdata->ti_tx_ring[frag];
			if (sc->ti_cdata.ti_tx_chain[frag] != NULL)
				break;
			TI_HOSTADDR(f->ti_addr) = dmamap->dm_segs[i].ds_addr;
			f->ti_len = dmamap->dm_segs[i].ds_len;
			f->ti_flags = 0;
			n = m_aux_find(m_head, AF_LINK, ETHERTYPE_VLAN);
			if (n) {
				f->ti_flags |= TI_BDFLAG_VLAN_TAG;
				f->ti_vlan_tag = *mtod(n, int *);
			} else {
				f->ti_vlan_tag = 0;
			}
			/*
			 * Sanity check: avoid coming within 16 descriptors
			 * of the end of the ring.
			 */
			if ((TI_TX_RING_CNT - (sc->ti_txcnt + cnt)) < 16)
				return(ENOBUFS);
			cur = frag;
			TI_INC(frag, TI_TX_RING_CNT);
			cnt++;
	}

	if (i < dmamap->dm_nsegs)
		return(ENOBUFS);

	if (frag == sc->ti_tx_saved_considx)
		return(ENOBUFS);

	if (sc->ti_hwrev == TI_HWREV_TIGON)
		sc->ti_rdata->ti_tx_ring_nic[cur % 128].ti_flags |=
		    TI_BDFLAG_END;
	else
		sc->ti_rdata->ti_tx_ring[cur].ti_flags |= TI_BDFLAG_END;
	sc->ti_cdata.ti_tx_chain[cur] = m_head;
	SIMPLEQ_REMOVE_HEAD(&sc->txdma_list, dma, link);
	sc->txdma[cur] = dma;
	sc->ti_txcnt += cnt;

	*txidx = frag;

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit descriptors.
 */
static void ti_start(ifp)
	struct ifnet		*ifp;
{
	struct ti_softc		*sc;
	struct mbuf		*m_head = NULL;
	u_int32_t		prodidx = 0;
	int			pkts = 0;

	sc = ifp->if_softc;

	prodidx = CSR_READ_4(sc, TI_MB_SENDPROD_IDX);

	while(sc->ti_cdata.ti_tx_chain[prodidx] == NULL) {
		IFQ_POLL(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (ti_encap(sc, m_head, &prodidx)) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

		IFQ_DEQUEUE(&ifp->if_snd, m_head);
		pkts++;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m_head);
#endif
	}
	if (pkts == 0)
		return;

	/* Transmit */
	CSR_WRITE_4(sc, TI_MB_SENDPROD_IDX, prodidx);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;

	return;
}

static void ti_init(xsc)
	void			*xsc;
{
	struct ti_softc		*sc = xsc;
        int			s;

	s = splimp();

	/* Cancel pending I/O and flush buffers. */
	ti_stop(sc);

	/* Init the gen info block, ring control blocks and firmware. */
	if (ti_gibinit(sc)) {
		printf("%s: initialization failure\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	splx(s);

	return;
}

static void ti_init2(sc)
	struct ti_softc		*sc;
{
	struct ti_cmd_desc	cmd;
	struct ifnet		*ifp;
	u_int8_t		*m;
	struct ifmedia		*ifm;
	int			tmp;

	ifp = &sc->ethercom.ec_if;

	/* Specify MTU and interface index. */
	CSR_WRITE_4(sc, TI_GCR_IFINDEX, sc->sc_dev.dv_unit); /* ??? */
	if ((sc->ethercom.ec_capenable & ETHERCAP_VLAN_MTU) &&
	    ifp->if_mtu < ETHERMTU + ETHER_VLAN_ENCAP_LEN)
		CSR_WRITE_4(sc, TI_GCR_IFMTU, ETHER_MAX_LEN +
		    ETHER_VLAN_ENCAP_LEN);
	else
		CSR_WRITE_4(sc, TI_GCR_IFMTU, ifp->if_mtu +
		    ETHER_HDR_LEN + ETHER_CRC_LEN);
	TI_DO_CMD(TI_CMD_UPDATE_GENCOM, 0, 0);

	/* Load our MAC address. */
	m = (u_int8_t *)LLADDR(ifp->if_sadl);
	CSR_WRITE_4(sc, TI_GCR_PAR0, (m[0] << 8) | m[1]);
	CSR_WRITE_4(sc, TI_GCR_PAR1, (m[2] << 24) | (m[3] << 16)
		    | (m[4] << 8) | m[5]);
	TI_DO_CMD(TI_CMD_SET_MAC_ADDR, 0, 0);

	/* Enable or disable promiscuous mode as needed. */
	if (ifp->if_flags & IFF_PROMISC) {
		TI_DO_CMD(TI_CMD_SET_PROMISC_MODE, TI_CMD_CODE_PROMISC_ENB, 0);
	} else {
		TI_DO_CMD(TI_CMD_SET_PROMISC_MODE, TI_CMD_CODE_PROMISC_DIS, 0);
	}

	/* Program multicast filter. */
	ti_setmulti(sc);

	/*
	 * If this is a Tigon 1, we should tell the
	 * firmware to use software packet filtering.
	 */
	if (sc->ti_hwrev == TI_HWREV_TIGON) {
		TI_DO_CMD(TI_CMD_FDR_FILTERING, TI_CMD_CODE_FILT_ENB, 0);
	}

	/* Init RX ring. */
	ti_init_rx_ring_std(sc);

	/* Init jumbo RX ring. */
	if (ifp->if_mtu > (MCLBYTES - ETHER_HDR_LEN - ETHER_CRC_LEN))
		ti_init_rx_ring_jumbo(sc);

	/*
	 * If this is a Tigon 2, we can also configure the
	 * mini ring.
	 */
	if (sc->ti_hwrev == TI_HWREV_TIGON_II)
		ti_init_rx_ring_mini(sc);

	CSR_WRITE_4(sc, TI_GCR_RXRETURNCONS_IDX, 0);
	sc->ti_rx_saved_considx = 0;

	/* Init TX ring. */
	ti_init_tx_ring(sc);

	/* Tell firmware we're alive. */
	TI_DO_CMD(TI_CMD_HOST_STATE, TI_CMD_CODE_STACK_UP, 0);

	/* Enable host interrupts. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 0);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	/*
	 * Make sure to set media properly. We have to do this
	 * here since we have to issue commands in order to set
	 * the link negotiation and we can't issue commands until
	 * the firmware is running.
	 */
	ifm = &sc->ifmedia;
	tmp = ifm->ifm_media;
	ifm->ifm_media = ifm->ifm_cur->ifm_media;
	ti_ifmedia_upd(ifp);
	ifm->ifm_media = tmp;

	return;
}

/*
 * Set media options.
 */
static int ti_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct ti_softc		*sc;
	struct ifmedia		*ifm;
	struct ti_cmd_desc	cmd;

	sc = ifp->if_softc;
	ifm = &sc->ifmedia;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return(EINVAL);

	switch(IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		CSR_WRITE_4(sc, TI_GCR_GLINK, TI_GLNK_PREF|TI_GLNK_1000MB|
		    TI_GLNK_FULL_DUPLEX|TI_GLNK_RX_FLOWCTL_Y|
		    TI_GLNK_AUTONEGENB|TI_GLNK_ENB);
		CSR_WRITE_4(sc, TI_GCR_LINK, TI_LNK_100MB|TI_LNK_10MB|
		    TI_LNK_FULL_DUPLEX|TI_LNK_HALF_DUPLEX|
		    TI_LNK_AUTONEGENB|TI_LNK_ENB);
		TI_DO_CMD(TI_CMD_LINK_NEGOTIATION,
		    TI_CMD_CODE_NEGOTIATE_BOTH, 0);
		break;
	case IFM_1000_SX:
	case IFM_1000_TX:
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX) {
			CSR_WRITE_4(sc, TI_GCR_GLINK,
			    TI_GLNK_PREF|TI_GLNK_1000MB|TI_GLNK_FULL_DUPLEX|
			    TI_GLNK_RX_FLOWCTL_Y|TI_GLNK_ENB);
		} else {
			CSR_WRITE_4(sc, TI_GCR_GLINK,
			    TI_GLNK_PREF|TI_GLNK_1000MB|
			    TI_GLNK_RX_FLOWCTL_Y|TI_GLNK_ENB);
		}
		CSR_WRITE_4(sc, TI_GCR_LINK, 0);
		TI_DO_CMD(TI_CMD_LINK_NEGOTIATION,
		    TI_CMD_CODE_NEGOTIATE_GIGABIT, 0);
		break;
	case IFM_100_FX:
	case IFM_10_FL:
	case IFM_100_TX:
	case IFM_10_T:
		CSR_WRITE_4(sc, TI_GCR_GLINK, 0);
		CSR_WRITE_4(sc, TI_GCR_LINK, TI_LNK_ENB|TI_LNK_PREF);
		if (IFM_SUBTYPE(ifm->ifm_media) == IFM_100_FX ||
		    IFM_SUBTYPE(ifm->ifm_media) == IFM_100_TX) {
			TI_SETBIT(sc, TI_GCR_LINK, TI_LNK_100MB);
		} else {
			TI_SETBIT(sc, TI_GCR_LINK, TI_LNK_10MB);
		}
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX) {
			TI_SETBIT(sc, TI_GCR_LINK, TI_LNK_FULL_DUPLEX);
		} else {
			TI_SETBIT(sc, TI_GCR_LINK, TI_LNK_HALF_DUPLEX);
		}
		TI_DO_CMD(TI_CMD_LINK_NEGOTIATION,
		    TI_CMD_CODE_NEGOTIATE_10_100, 0);
		break;
	}

	sc->ethercom.ec_if.if_baudrate =
	    ifmedia_baudrate(ifm->ifm_media);

	return(0);
}

/*
 * Report current media status.
 */
static void ti_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct ti_softc		*sc;
	u_int32_t               media = 0;

	sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (sc->ti_linkstat == TI_EV_CODE_LINK_DOWN)
		return;

	ifmr->ifm_status |= IFM_ACTIVE;

	if (sc->ti_linkstat == TI_EV_CODE_GIG_LINK_UP) {
		media = CSR_READ_4(sc, TI_GCR_GLINK_STAT);
		if (sc->ti_copper)
			ifmr->ifm_active |= IFM_1000_TX;
		else
			ifmr->ifm_active |= IFM_1000_SX;
		if (media & TI_GLNK_FULL_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;
	} else if (sc->ti_linkstat == TI_EV_CODE_LINK_UP) {
		media = CSR_READ_4(sc, TI_GCR_LINK_STAT);
		if (sc->ti_copper) {
			if (media & TI_LNK_100MB)
				ifmr->ifm_active |= IFM_100_TX;
			if (media & TI_LNK_10MB)
				ifmr->ifm_active |= IFM_10_T;
		} else {
			if (media & TI_LNK_100MB)
				ifmr->ifm_active |= IFM_100_FX;
			if (media & TI_LNK_10MB)
				ifmr->ifm_active |= IFM_10_FL;
		}
		if (media & TI_LNK_FULL_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		if (media & TI_LNK_HALF_DUPLEX)
			ifmr->ifm_active |= IFM_HDX;
	}

	sc->ethercom.ec_if.if_baudrate =
	    ifmedia_baudrate(sc->ifmedia.ifm_media);

	return;
}

static int
ti_ether_ioctl(ifp, cmd, data)
	struct ifnet *ifp;
	u_long cmd;
	caddr_t data;
{
	struct ifaddr *ifa = (struct ifaddr *) data;
	struct ti_softc *sc = ifp->if_softc;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;

		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			ti_init(sc);
			arp_ifinit(ifp, ifa);
			break;
#endif
#ifdef NS
		case AF_NS:
		    {
			 struct ns_addr *ina = &IA_SNS(ifa)->sns_addr;

			 if (ns_nullhost(*ina))
				ina->x_host = *(union ns_host *)
				    LLADDR(ifp->if_sadl);
			 else
				bcopy(ina->x_host.c_host, LLADDR(ifp->if_sadl),
				    ifp->if_addrlen);
			 /* Set new address. */
			 ti_init(sc);
			 break;
		    }
#endif
		default:
			ti_init(sc);
			break;
		}
		break;

	default:
		return (EINVAL);
	}

	return (0);
}

static int ti_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct ti_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			s, error = 0;
	struct ti_cmd_desc	cmd;

	s = splimp();

	switch(command) {
	case SIOCSIFADDR:
	case SIOCGIFADDR:
		error = ti_ether_ioctl(ifp, command, data);
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > TI_JUMBO_MTU)
			error = EINVAL;
		else {
			ifp->if_mtu = ifr->ifr_mtu;
			ti_init(sc);
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			/*
			 * If only the state of the PROMISC flag changed,
			 * then just use the 'set promisc mode' command
			 * instead of reinitializing the entire NIC. Doing
			 * a full re-init means reloading the firmware and
			 * waiting for it to start up, which may take a
			 * second or two.
			 */
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->ti_if_flags & IFF_PROMISC)) {
				TI_DO_CMD(TI_CMD_SET_PROMISC_MODE,
				    TI_CMD_CODE_PROMISC_ENB, 0);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->ti_if_flags & IFF_PROMISC) {
				TI_DO_CMD(TI_CMD_SET_PROMISC_MODE,
				    TI_CMD_CODE_PROMISC_DIS, 0);
			} else
				ti_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				ti_stop(sc);
			}
		}
		sc->ti_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = (command == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &sc->ethercom) :
		    ether_delmulti(ifr, &sc->ethercom);
		if (error == ENETRESET) {
			if (ifp->if_flags & IFF_RUNNING)
				ti_setmulti(sc);
			error = 0;
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, command);
		break;
	default:
		error = EINVAL;
		break;
	}

	(void)splx(s);

	return(error);
}

static void ti_watchdog(ifp)
	struct ifnet		*ifp;
{
	struct ti_softc		*sc;

	sc = ifp->if_softc;

	printf("%s: watchdog timeout -- resetting\n", sc->sc_dev.dv_xname);
	ti_stop(sc);
	ti_init(sc);

	ifp->if_oerrors++;

	return;
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void ti_stop(sc)
	struct ti_softc		*sc;
{
	struct ifnet		*ifp;
	struct ti_cmd_desc	cmd;

	ifp = &sc->ethercom.ec_if;

	/* Disable host interrupts. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 1);
	/*
	 * Tell firmware we're shutting down.
	 */
	TI_DO_CMD(TI_CMD_HOST_STATE, TI_CMD_CODE_STACK_DOWN, 0);

	/* Halt and reinitialize. */
	ti_chipinit(sc);
	ti_mem(sc, 0x2000, 0x100000 - 0x2000, NULL);
	ti_chipinit(sc);

	/* Free the RX lists. */
	ti_free_rx_ring_std(sc);

	/* Free jumbo RX list. */
	ti_free_rx_ring_jumbo(sc);

	/* Free mini RX list. */
	ti_free_rx_ring_mini(sc);

	/* Free TX buffers. */
	ti_free_tx_ring(sc);

	sc->ti_ev_prodidx.ti_idx = 0;
	sc->ti_return_prodidx.ti_idx = 0;
	sc->ti_tx_considx.ti_idx = 0;
	sc->ti_tx_saved_considx = TI_TXCONS_UNSET;

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	return;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void ti_shutdown(v)
	void *v;
{
	struct ti_softc		*sc = v;

	ti_chipinit(sc);

	return;
}