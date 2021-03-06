/*	$NetBSD: stpcide.c,v 1.4.2.2 2004/08/22 13:26:06 tron Exp $	*/

/*
 * Copyright (c) 2003 Tohru Nishimura
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
 *	This product includes software developed by Tohru Nishimura.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,     
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/pciidereg.h>
#include <dev/pci/pciidevar.h>

static void stpc_chip_map(struct pciide_softc *, struct pci_attach_args *);
static void stpc_setup_channel(struct wdc_channel *);

static int  stpcide_match(struct device *, struct cfdata *, void *);
static void stpcide_attach(struct device *, struct device *, void *);

const struct pciide_product_desc pciide_stpc_products[] = {
	{ 0x0228,
	  0,
	  "STMicroelectronics STPC IDE Controller",
	  stpc_chip_map,
	},
	{ 0, 0, NULL, NULL },
};

CFATTACH_DECL(stpcide, sizeof(struct pciide_softc),
    stpcide_match, stpcide_attach, NULL, NULL);

static int
stpcide_match(struct device *parent, struct cfdata *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_SGSTHOMSON) {
		if (pciide_lookup_product(pa->pa_id, pciide_stpc_products))
			return (2);
	}
	return (0);
}

static void
stpcide_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct pciide_softc *sc = (struct pciide_softc *)self;

	pciide_common_attach(sc, pa,
	    pciide_lookup_product(pa->pa_id, pciide_stpc_products));

}

static void
stpc_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;

	if (pciide_chipen(sc, pa) == 0)
		return;

	aprint_normal("%s: bus-master DMA support present",
	    sc->sc_wdcdev.sc_dev.dv_xname);
	pciide_mapreg_dma(sc, pa);
	aprint_normal("\n");
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 0;
	sc->sc_wdcdev.set_modes = stpc_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
	}
}

/*
 * IDE timing register (0x40, 0x42, 0x44, and 0x46) assignment.
 * 33MHz PCI system will have;
 *	DMA0 01-11-11
 *	DMA1 00-01-10
 *	DMA2 00-00-10
 *	PIO0          111-100
 *	PIO1          100-011
 *	PIO2          011-010
 *	PIO3          010-001
 *	PIO4          000-001
 *	MISC                  XYZW
 */
static const u_int16_t dmatbl[] = { 0x7C00, 0x1800, 0x0800 };
static const u_int16_t piotbl[] = { 0x03C0, 0x0230, 0x01A0, 0x0110, 0x0010 };

static void
stpc_setup_channel(struct wdc_channel *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.ch_wdc;
	struct wdc_softc *wdc = &sc->sc_wdcdev;
	int channel = chp->ch_channel;
	struct ata_drive_datas *drvp;
	u_int32_t idedma_ctl, idetim;
	int drive, bits[2];

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	idedma_ctl = 0;
	bits[0] = bits[1] = 0x7F60; /* assume PIO2/DMA0 */

	/* Per drive settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* add timing values, setup DMA if needed */
		if ((wdc->cap & WDC_CAPABILITY_DMA) &&
		    (drvp->drive_flags & DRIVE_DMA)) {
			/* use Multiword DMA */
			drvp->drive_flags &= ~DRIVE_UDMA;
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			bits[drive] = 0xe; /* IOCHRDY,wr/post,rd/prefetch */
		}
		else {
			/* PIO only */
			drvp->drive_flags &= ~(DRIVE_UDMA | DRIVE_DMA);
			bits[drive] = 0x8; /* IOCHRDY */
		}
		bits[drive] |= dmatbl[drvp->DMA_mode] | piotbl[drvp->PIO_mode];
	}
#if 0
	idetim = pci_conf_read(sc->sc_pc, sc->sc_tag,
	    (channel == 0) ? 0x40 : 0x44);
	aprint_normal("wdc%d: IDETIM %08x -> %08x\n",
	    channel, idetim, (bits[1] << 16) | bits[0]);
#endif
	idetim = (bits[1] << 16) | bits[0];
	pci_conf_write(sc->sc_pc, sc->sc_tag,
	    (channel == 0) ? 0x40 : 0x44, idetim);

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, cp->dma_iohs[IDEDMA_CTL], 0,
		    idedma_ctl);
	}
}
