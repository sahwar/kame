/*	$NetBSD: ninjascsi32reg.h,v 1.1.2.1 2004/08/30 09:24:58 tron Exp $	*/

/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by ITOH Yasufumi.
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

#ifndef _NJSC32REG_H_
#define _NJSC32REG_H_

/*
 * Workbit NinjaSCSI (32bit versions), Ultra Narrow SCSI3 host adapters:
 *	NJSC-32Bi	PCMCIA/CardBus dual mode device ("DuoSCSI")
 *			(CardBus mode only)
 *	NJSC-32UDE	PCI/CardBus device, DualEdge transfer support
 */

#define NJSC32_REGSIZE		128	/* size of register set */
#define NJSC32_MEMOFFSET_REG	0x800	/* offset of memory mapped register */

/*
 * Direct registers
 */
#define NJSC32_REG_IRQ			0x00	/* len=2 R/W */
# define NJSC32_IRQ_MSG			0x0001
# define NJSC32_IRQ_IO			0x0002
# define NJSC32_IRQ_CD			0x0004
# define NJSC32_IRQ_BUS_FREE		0x0008
# define NJSC32_IRQ_RESELECT		0x0010
# define NJSC32_IRQ_PHASE_CHANGE	0x0020
# define NJSC32_IRQ_SCSIRESET		0x0040
# define NJSC32_IRQ_TIMER		0x0080
# define NJSC32_IRQ_FIFO_THRESHOLD	0x0100
# define NJSC32_IRQ_PCI			0x0200
# define NJSC32_IRQ_BMCNTERR		0x0400
# define NJSC32_IRQ_AUTOSCSI		0x0800
# define NJSC32_IRQ_MASK_PCI		0x1000
# define NJSC32_IRQ_MASK_TIMER		0x2000
# define NJSC32_IRQ_MASK_FIFO		0x4000
# define NJSC32_IRQ_MASK_SCSI		0x8000

# define NJSC32_IRQ_MASK_ALL		0xf000
# define NJSC32_IRQ_INTR_PENDING	0x0ff0

#define NJSC32_REG_TRANSFER		0x02	/* len=2 R/W */
# define NJSC32_XFR_CB_MMIO_MODE	0x0001
# define NJSC32_XFR_CB_PIO_MODE		0x0002
# define NJSC32_XFR_BM_TEST		0x0004
# define NJSC32_XFR_BM_TEST_DIR		0x0008
# define NJSC32_XFR_DUALEDGE_ENABLE	0x0010	/* (UDE) */
# define NJSC32_XFR_NO_XFER_TO_HOST	0x0020	/* (UDE) */
					/* reserved */
# define NJSC32_XFR_TRANSFER_GO		0x0080
# define NJSC32_XFR_BLIND_MODE		0x0100
# define NJSC32_XFR_BM_START		0x0200
# define NJSC32_XFR_ADVANCED_BM_WRITE	0x0400
# define NJSC32_XFR_BM_SINGLE_MODE	0x0800
# define NJSC32_XFR_FIFO_FULL		0x1000	/* RO */
# define NJSC32_XFR_FIFO_EMPTY		0x2000	/* RO */
# define NJSC32_XFR_ALL_COUNT_CLR	0x4000
# define NJSC32_XFR_FIFO_TEST		0x8000

#define NJSC32_REG_INDEX		0x04	/* len=1 R/W, len=2 RO */
# define NJSC32_INDEX_GAREV(x)		((x) >> 8)
# define NJSC32_INDEX_GAREV_MIN		0x51

#define NJSC32_REG_TIMER		0x06	/* len=2 R/W */
# define NJSC32_TIMER_CNT_MASK		0x00ff
# define NJSC32_TIMER_STOP		0x0100

#define NJSC32_REG_DATA_LOW		0x08	/* len=2 R/W */
#define NJSC32_REG_DATA_HIGH		0x0a	/* len=2 R/W */

#define NJSC32_REG_FIFO_REST_CNT	0x0c	/* len=2 R/W */
# define NJSC32_FIFOCNT_MASK		0x01ff
# define NJSC32_FIFOCNT_LOW_WATER	0x4000
# define NJSC32_FIFOCNT_HIGH_WATER	0x8000

#define NJSC32_REG_SREQ_SAMPLING	0x0f	/* len=1 R/W */
# define NJSC32_SREQ_SAMPLING_RATE0	0x01
# define NJSC32_SREQ_SAMPLING_RATE1	0x02
# define NJSC32_SREQ_SAMPLING_ENABLE	0x04

# define NJSC32_SREQ_SAMPLING_1CLK	0
# define NJSC32_SREQ_SAMPLING_2CLK	NJSC32_SREQ_SAMPLING_RATE0
# define NJSC32_SREQ_SAMPLING_4CLK	NJSC32_SREQ_SAMPLING_RATE1

#define NJSC32_REG_SCSI_BUS_CONTROL	0x10	/* len=1 R/W */
# define NJSC32_SBCTL_SEL		0x01
# define NJSC32_SBCTL_RST		0x02
# define NJSC32_SBCTL_DATAOUT_ENABLE	0x04
# define NJSC32_SBCTL_ATN		0x08
# define NJSC32_SBCTL_ACK		0x10
# define NJSC32_SBCTL_BSY		0x20
# define NJSC32_SBCTL_AUTODIRECTION	0x40
# define NJSC32_SBCTL_ACK_ENABLE	0x80

#define NJSC32_REG_CLR_COUNTER		0x12	/* len=1 WO */
# define NJSC32_CLRCNT_ACK		0x01
# define NJSC32_CLRCNT_REQ		0x02
# define NJSC32_CLRCNT_FIFO_HOST_PTR	0x04
# define NJSC32_CLRCNT_FIFO_REST	0x08
# define NJSC32_CLRCNT_BM		0x10
# define NJSC32_CLRCNT_SAVED_ACK	0x20

# define NJSC32_CLCNT_ALL		0x3f

#define NJSC32_REG_SCSI_BUS_MONITOR	0x12	/* len=1 RO */
# define NJSC32_BUSMON_MSG		0x01
# define NJSC32_BUSMON_IO		0x02
# define NJSC32_BUSMON_CD		0x04
# define NJSC32_BUSMON_BSY		0x08
# define NJSC32_BUSMON_ACK		0x10
# define NJSC32_BUSMON_REQ		0x20
# define NJSC32_BUSMON_SEL		0x40
# define NJSC32_BUSMON_ATN		0x80

# define NJSC32_BUSMON_BUSFREE		0x00
# define NJSC32_BUSMON_COMMAND		(NJSC32_BUSMON_CD | \
	NJSC32_BUSMON_BSY | NJSC32_BUSMON_REQ)
# define NJSC32_BUSMON_MESSAGE_IN	(NJSC32_BUSMON_MSG | \
	NJSC32_BUSMON_BSY | NJSC32_BUSMON_IO | NJSC32_BUSMON_CD | \
	NJSC32_BUSMON_REQ)
# define NJSC32_BUSMON_MESSAGE_OUT	(NJSC32_BUSMON_MSG | \
	NJSC32_BUSMON_BSY | NJSC32_BUSMON_CD | NJSC32_BUSMON_REQ)
# define NJSC32_BUSMON_DATA_IN		(NJSC32_BUSMON_IO | \
	NJSC32_BUSMON_BSY | NJSC32_BUSMON_REQ)
# define NJSC32_BUSMON_DATA_OUT	(NJSC32_BUSMON_BSY | \
	NJSC32_BUSMON_REQ)
# define NJSC32_BUSMON_STATUS		(NJSC32_BUSMON_IO | \
	NJSC32_BUSMON_CD | NJSC32_BUSMON_BSY | NJSC32_BUSMON_REQ)
# define NJSC32_BUSMON_RESELECT	(NJSC32_BUSMON_IO | NJSC32_BUSMON_SEL)

# define NJSC32_BUSMON_PHASE_MASK	(NJSC32_BUSMON_MSG | \
	NJSC32_BUSMON_IO | NJSC32_BUSMON_BSY | NJSC32_BUSMON_CD | \
	NJSC32_BUSMON_SEL)

# define NJSC32_PHASE_BUSFREE		\
	(NJSC32_BUSMON_BUSFREE & NJSC32_BUSMON_PHASE_MASK)
# define NJSC32_PHASE_COMMAND		\
	(NJSC32_BUSMON_COMMAND & NJSC32_BUSMON_PHASE_MASK)
# define NJSC32_PHASE_MESSAGE_IN	\
	(NJSC32_BUSMON_MESSAGE_IN & NJSC32_BUSMON_PHASE_MASK)
# define NJSC32_PHASE_MESSAGE_OUT	\
	(NJSC32_BUSMON_MESSAGE_OUT & NJSC32_BUSMON_PHASE_MASK)
# define NJSC32_PHASE_DATA_IN		\
	(NJSC32_BUSMON_DATA_IN & NJSC32_BUSMON_PHASE_MASK)
# define NJSC32_PHASE_DATA_OUT		\
	(NJSC32_BUSMON_DATA_OUT & NJSC32_BUSMON_PHASE_MASK)
# define NJSC32_PHASE_STATUS		\
	(NJSC32_BUSMON_STATUS & NJSC32_BUSMON_PHASE_MASK)
# define NJSC32_PHASE_RESELECT		\
	(NJSC32_BUSMON_RESELECT & NJSC32_BUSMON_PHASE_MASK)

#define NJSC32_REG_COMMAND_DATA		0x14	/* len=1 R/W */

#define NJSC32_REG_PARITY_CONTROL	0x16	/* len=1 WO */
# define NJSC32_PARITYCTL_CHECK_ENABLE	0x01
# define NJSC32_PARITYCTL_CLEAR_ERROR	0x02

#define NJSC32_REG_PARITY_STATUS	0x16	/* len=1 RO */
# define NJSC32_PARITYSTATUS_ERROR_LSB	0x02
# define NJSC32_PARITYSTATUS_ERROR_MSB	0x04	/* (UDE) */

#define NJSC32_REG_RESELECT_ID		0x18	/* len=1 RO */

#define NJSC32_REG_COMMAND_CONTROL	0x18	/* len=2 WO */
# define NJSC32_CMD_CLEAR_CDB_FIFO_PTR	0x0001
# define NJSC32_CMD_AUTO_COMMAND_PHASE	0x0002
# define NJSC32_CMD_AUTO_SCSI_START	0x0004
# define NJSC32_CMD_AUTO_SCSI_RESTART	0x0008
# define NJSC32_CMD_AUTO_PARAMETER	0x0010	/* load parameters via DMA */
# define NJSC32_CMD_AUTO_ATN		0x0020
# define NJSC32_CMD_AUTO_MSGIN_00_04	0x0040	/* Command Complete (00)
						   or Disconnect (04) */
# define NJSC32_CMD_AUTO_MSGIN_02	0x0080	/* Save Data Pointer */
# define NJSC32_CMD_AUTO_MSGIN_03	0x0100	/* Restore Pointers */

#define NJSC32_REG_SET_ARBITRATION	0x1a	/* len=1 WO */
# define NJSC32_SETARB_GO		0x01
# define NJSC32_SETARB_CLEAR		0x02

#define NJSC32_REG_ARBITRATION_STAT	0x1a	/* len=1 RO */
# define NJSC32_ARBSTAT_WIN		0x02
# define NJSC32_ARBSTAT_FAIL		0x04
# define NJSC32_ARBSTAT_AUTOPARAM_VALID	0x08
# define NJSC32_ARBSTAT_SG_TABLE_VALID	0x10

#define NJSC32_REG_SYNC			0x1c	/* len=1 R/W */
# define NJSC32_SYNC_VAL(periodnum, syncoffset) ((periodnum)<<4 | (syncoffset))

# define NJSC32_SYNCPERIOD_ASYNC	0
# define NJSC32_SYNCOFFSET_ASYNC	0
# define NJSC32_SYNCOFFSET_MAX		15

#define NJSC32_REG_ACK_WIDTH		0x1d	/* len=1 R/W */
# define NJSC32_ACK_WIDTH_1CLK		0
# define NJSC32_ACK_WIDTH_2CLK		1
# define NJSC32_ACK_WIDTH_3CLK		2
# define NJSC32_ACK_WIDTH_4CLK		3

#define NJSC32_REG_SCSI_DATA_WITH_ACK	0x20	/* len=1 R/W */

#define NJSC32_REG_SCSI_OUT_LATCH	0x22	/* len=1 W */
#define NJSC32_REG_TARGET_ID		0x22	/* len=1 W */
#define NJSC32_REG_DATA_IN		0x22	/* len=1 R */

#define NJSC32_REG_SCAM_CONTROL		0x24	/* len=1 R/W */
# define NJSC32_SCAMCTL_MSG		0x01
# define NJSC32_SCAMCTL_IO		0x02
# define NJSC32_SCAMCTL_CD		0x04
# define NJSC32_SCAMCTL_BSY		0x08
# define NJSC32_SCAMCTL_SEL		0x10
# define NJSC32_SCAMCTL_XFEROK		0x20

#define NJSC32_REG_SCAM_DATA		0x26	/* len=1 R/W */

#define NJSC32_REG_SACK_CNT		0x28	/* len=4 R/W */

#define NJSC32_REG_SREQ_CNT		0x2c	/* len=4 R/W */

#define NJSC32_REG_FIFO_DATA		0x30	/* len=4 R/W */
#define NJSC32_REG_FIFO_ADR		0x34	/* len=4 R/W */

#define NJSC32_REG_BM_CNT		0x38	/* len=4 R/W */
# define NJSC32_BMCNT_MASK		0x0001ffff

#define NJSC32_REG_SGT_ADR		0x3c	/* len=4 R/W */

#define NJSC32_REG_EXECUTE_PHASE	0x40	/* len=2 RO */
# define NJSC32_XPHASE_COMMAND		0x0001
# define NJSC32_XPHASE_DATA_IN		0x0002
# define NJSC32_XPHASE_DATA_OUT		0x0004
# define NJSC32_XPHASE_MSGOUT		0x0008
# define NJSC32_XPHASE_STATUS		0x0010
# define NJSC32_XPHASE_ILLEGAL		0x0020
# define NJSC32_XPHASE_BUS_FREE		0x0040
# define NJSC32_XPHASE_PAUSED_MSG_IN	0x0080
# define NJSC32_XPHASE_PAUSED_MSG_OUT	0x0100
# define NJSC32_XPHASE_SEL_TIMEOUT	0x0200
# define NJSC32_XPHASE_MSGIN_00		0x0400	/* Command Complete */
# define NJSC32_XPHASE_MSGIN_02		0x0800	/* Save Data Pointer */
# define NJSC32_XPHASE_MSGIN_03		0x1000	/* Restore Pointers */
# define NJSC32_XPHASE_MSGIN_04		0x2000	/* Disconnect */
					/* reserved */
# define NJSC32_XPHASE_AUTOSCSI_BUSY	0x8000

#define NJSC32_REG_SCSI_CSB_IN		0x42	/* len=1 RO */

#define NJSC32_REG_SCSI_MSG_OUT		0x44	/* len=4 R/W */
# define NJSC32_MSGOUT_COUNT_MASK	0x00000003
# define NJSC32_MSGOUT_MAX_AUTO		3
# define NJSC32_MSGOUT_VALID		0x00000080
# define NJSC32_MSGOUT_MSG1_SHIFT	8	/* used only if cnt == 3 */
# define NJSC32_MSGOUT_MSG2_SHIFT	16	/* used if cnt == 2 or 3 */
# define NJSC32_MSGOUT_MSG3_SHIFT	24

#define NJSC32_REG_SEL_TIMEOUT		0x48	/* len=2 R/W */

#define NJSC32_REG_SAVED_ACK_CNT	0x4c	/* len=4 RO */

#define NJSC32_REG_HTOS_DATA_DELAY	0x50	/* len=1 R/W (UDE) */
# define NJSC32_HTOSDATADELAY_FACTOR	0x07
# define NJSC32_HTOSDATADELAY_DATA_SEL	0x80

#define NJSC32_REG_STOH_DATA_DELAY	0x54	/* len=1 R/W (UDE) */
#define NJSC32_REG_ACK_SUM_CHECK_RD	0x58	/* len=2 RO (UDE) */
#define NJSC32_REG_REQ_SUM_CHECK_RD	0x5c	/* len=2 RO (UDE) */

/*
 * Indexed registers
 */
#define NJSC32_IREG_CLOCK		0x00	/* len=1 R/W */
# define NJSC32_CLOCK_DIV_2		0x01	/* external 20MHz (FAST SCSI)*/
# define NJSC32_CLOCK_DIV_4		0x02	/* external 40MHz (Ultra SCSI)*/
# define NJSC32_CLOCK_PCICLK		0x80	/* PCI 33.3MHz */

#define NJSC32_IREG_TERM_PWR		0x01	/* len=1 R/W */
# define NJSC32_TERMPWR_BPWR		0x01	/* supply termination power */
# define NJSC32_TERMPWR_SENSE		0x02	/* RO */

#define NJSC32_IREG_EXT_PORT_DDR	0x02	/* len=1 R/W */
#define NJSC32_IREG_EXT_PORT		0x03	/* len=1 R/W */
# define NJSC32_EXTPORT_LED_ON		0x00
# define NJSC32_EXTPORT_LED_OFF		0x01

#define NJSC32_IREG_IRQ_SELECT		0x04	/* len=2 R/W */
# define NJSC32_IRQSEL_RESELECT		0x0001
# define NJSC32_IRQSEL_PHASE_CHANGE	0x0002
# define NJSC32_IRQSEL_SCSIRESET	0x0004
# define NJSC32_IRQSEL_TIMER		0x0008
# define NJSC32_IRQSEL_FIFO_THRESHOLD	0x0010
# define NJSC32_IRQSEL_TARGET_ABORT	0x0020
# define NJSC32_IRQSEL_MASTER_ABORT	0x0040
# define NJSC32_IRQSEL_SERR		0x0080
# define NJSC32_IRQSEL_PERR		0x0100
# define NJSC32_IRQSEL_BMCNTERR		0x0200
# define NJSC32_IRQSEL_AUTO_SCSI_SEQ	0x0400

#define NJSC32_IREG_OLD_SCSI_PHASE	0x05	/* len=1 R/W */
# define NJSC32_OLDSCSI_PHASE_MSG	0x01
# define NJSC32_OLDSCSI_PHASE_IO	0x02
# define NJSC32_OLDSCSI_PHASE_CD	0x04
# define NJSC32_OLDSCSI_PHASE_BUSY	0x08

#define NJSC32_IREG_FIFO_THRESHOLD_FULL		0x06	/* len=1 R/W */
# define NJSC32_FIFO_FULL_PIO_MMIO	0x40
# define NJSC32_FIFO_FULL_BUSMASTER	0x10
#define NJSC32_IREG_FIFO_THRESHOLD_EMPTY	0x07	/* len=1 R/W */
# define NJSC32_FIFO_EMPTY_PIO_MMIO	0x40
# define NJSC32_FIFO_EMPTY_BUSMASTER	0x60

#define NJSC32_IREG_EXP_ROM		0x08	/* len=1 R/W */
# define NJSC32_EXPROM_WRITE_ENB	0x01
# define NJSC32_EXPROM_IO_ACCESS_ENB	0x02
# define NJSC32_EXPROM_ADR_CLEAR	0x04

#define NJSC32_IREG_EXP_ROM_ADR		0x09	/* len=1 R/W */
#define NJSC32_IREG_EXP_ROM_DATA	0x0a	/* len=1 R/W */

#define NJSC32_IREG_CHIP_MODE		0x0b	/* len=1 RO (Bi only)*/
# define NJSC32_CHIPMODE_OEM_MASK	0x06
#  define NJSC32_CHIPMODE_OEM_IODATA	0x00	/* I-O DATA */
#  define NJSC32_CHIPMODE_OEM_KME	0x02	/* Kyushu Matsushita Electric */
#  define NJSC32_CHIPMODE_OEM_WORKBIT	0x04	/* Workbit */
#  define NJSC32_CHIPMODE_OEM_EXTROM	0x06
# define NJSC32_CHIPMODE_OPTB		0x08
# define NJSC32_CHIPMODE_OPTC		0x10
# define NJSC32_CHIPMODE_OPTD		0x20
# define NJSC32_CHIPMODE_OPTE		0x40
# define NJSC32_CHIPMODE_OPTF		0x80

#define NJSC32_IREG_MISC		0x0c	/* len=2 R/W */
# define NJSC32_MISC_SCSI_DIRECTION_DETECTOR_SELECT	0x0001
# define NJSC32_MISC_SCSI2HOST_DIRECTION_VALID		0x0002	/* RO */
# define NJSC32_MISC_HOST2SCSI_DIRECTION_VALID		0x0004	/* RO */
# define NJSC32_MISC_DELAYED_BMSTART			0x0008
# define NJSC32_MISC_MASTER_TERMINATION_SELECT		0x0010
# define NJSC32_MISC_BMREQ_NEGATE_TIMING_SEL		0x0020
# define NJSC32_MISC_AUTOSEL_TIMING_SEL			0x0040
# define NJSC32_MISC_MABORT_MASK			0x0080	/* (UDE) */
# define NJSC32_MISC_BMSTOP_CHANGE2_NONDATA_PHASE	0x0100	/* (UDE) */

#define NJSC32_IREG_BM			0x0d	/* len=1 R/W */
# define NJSC32_BM_CYCLE0			0x01
# define NJSC32_BM_CYCLE1			0x02
# define NJSC32_BM_FRAME_ASSERT_TIMING		0x04
# define NJSC32_BM_IRDY_ASSERT_TIMING		0x08
# define NJSC32_BM_SINGLE_MASTER		0x10
# define NJSC32_BM_MEMRD_CMD0			0x20
# define NJSC32_BM_SGT_AUTO_PARA_MEMRD_CMD	0x40
# define NJSC32_BM_MEMRD_CMD1			0x80

#define NJSC32_IREG_UP_CNT		0x0f	/* len=1 WO */
# define NJSC32_UPCNT_REQCNT		0x01
# define NJSC32_UPCNT_ACKCNT		0x02
# define NJSC32_UPCNT_BMADR		0x10
# define NJSC32_UPCNT_BMCNT		0x20
# define NJSC32_UPCNT_SGTCNT		0x80

#define NJSC32_IREG_CFG_CMD_STR		0x10	/* len=2 RO */

#define NJSC32_IREG_CFG_LATE_CACHE	0x11	/* len=2 R/W */

#define NJSC32_IREG_CFG_BASE_ADR1	0x12	/* len=2 RO */
#define NJSC32_IREG_CFG_BASE_ADR2	0x13	/* len=2 RO */

#define NJSC32_IREG_CFG_INLINE		0x14	/* len=2 RO */

#define NJSC32_IREG_SERIAL_ROM		0x15	/* len=1 R/W (UDE) */
# define NJSC32_SERIALROM_CLOCK		0x01
# define NJSC32_SERIALROM_ENABLE	0x02
# define NJSC32_SERIALROM_DATA		0x04

#define NJSC32_IREG_HST_POINTER	0x16	/* len=1 R/W */

#define NJSC32_IREG_SREQ_DELAY		0x17	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SACK_DELAY		0x18	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SREQ_NOISE_CANCEL	0x19	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SDP_NOISE_CANCEL	0x1a	/* len=1 R/W (UDE) */
#define NJSC32_IREG_DELAY_TEST		0x1b	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SD0_NOISE_CANCEL	0x20	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SD1_NOISE_CANCEL	0x21	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SD2_NOISE_CANCEL	0x22	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SD3_NOISE_CANCEL	0x23	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SD4_NOISE_CANCEL	0x24	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SD5_NOISE_CANCEL	0x25	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SD6_NOISE_CANCEL	0x26	/* len=1 R/W (UDE) */
#define NJSC32_IREG_SD7_NOISE_CANCEL	0x27	/* len=1 R/W (UDE) */

/*
 * DMA data structure
 */

/* scatter/gather transfer table entry (8 bytes) */
struct njsc32_sgtable {
	u_int32_t	sg_addr;	/* transfer address (little endian) */
	u_int32_t	sg_len;		/* transfer length (little endian) */
#define NJSC32_SGT_ENDMARK	0x80000000
#define NJSC32_SGT_MAXSEGLEN	0x10000
};
#define NJSC32_SGT_MAXENTRY	18

/* autoparam (88 bytes) */
#define NJSC32_AUTOPARAM_CDBLEN	16
struct njsc32_autoparam {
	struct njsc32_autoparam_cdb {
		u_int8_t	cdb_data;
		u_int8_t	cdb_reserved0, cdb_reserved1, cdb_reserved2;
	} ap_cdb[NJSC32_AUTOPARAM_CDBLEN];	/* Command Descriptor Block */
	u_int32_t	ap_msgout;	/* msgout buffer (little endian) */
	u_int8_t	ap_sync;	/* NJSC32_REG_SYNC */
	u_int8_t	ap_ackwidth;	/* NJSC32_REG_ACK_WIDTH */
	u_int8_t	ap_targetid;	/* initiator and target id */
	u_int8_t	ap_sample;	/* NJSC32_REG_SREQ_SAMPLING */
	u_int16_t	ap_cmdctl;	/* command control (little endian) */
	u_int16_t	ap_xferctl;	/* transfer control (little endian) */
	u_int32_t	ap_sgtdmaaddr;	/* SG table addr (little endian) */
	u_int32_t	ap_pad[2];
};

/*
 * device specific constants
 */

#define NJSC32_NTARGET		8	/* Narrow SCSI */
#define NJSC32_NLU		8

#define NJSC32_INITIATOR_ID	7	/* fixed value? */
#define NJSC32_MAX_TARGET_ID	6	/* 0..6 */

#endif	/* _NJSC32REG_H_ */
