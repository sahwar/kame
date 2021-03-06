/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
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

#ifndef _DEV_IEEE1394_FWOHCIREG_H_
#define _DEV_IEEE1394_FWOHCIREG_H_

/* PCI/CardBus-Specific definitions
 */

/* In the PCI Class Code Register ...
 */
#define	PCI_INTERFACE_OHCI		0x10

/* The OHCI Regisers are in PCI BAR0.
 */
#define	PCI_OHCI_MAP_REGISTER		0x10

/* HCI Control Register (in PCI config space)
 */
#define	PCI_OHCI_CONTROL_REGISTER	0x40

/* If the following bit, all OHCI register access
 * and DMA transactions are byte swapped.
 */
#define	PCI_GLOBAL_SWAP_BE		0x00000001

/* Bus Independent Definitions */

/* OHCI Registers
 * OHCI Registers are divided into four spaces:
 *   1) 0x000 .. 0x17C = Control register space
 *   2) 0x180 .. 0x1FC = Asynchronous DMA context register space
 *			 (4 contexts)
 *   3) 0x200 .. 0x3FC = Isochronous Transmit DMA context register space
 *			 (32 contexts)
 *   4) 0x400 .. 0x7FC = Isochronous Receive DMA context register space
 *			 (32 contexts)
 */
#define	OHCI_REG_Version			0x000
#define	OHCI_REG_Guid_Rom			0x004
#define	OHCI_REG_ATRetries			0x008
#define	OHCI_REG_CsrReadData			0x00c
#define	OHCI_REG_CsrCompareData			0x010
#define	OHCI_REG_CsrControl			0x014
#define	OHCI_REG_ConfigROMhdr			0x018
#define	OHCI_REG_BusId				0x01c
#define	OHCI_REG_BusOptions			0x020
#define	OHCI_REG_GUIDHi				0x024
#define	OHCI_REG_GUIDLo				0x028
#define	OHCI_REG_reserved_02c			0x02c
#define	OHCI_REG_reserved_030			0x030
#define	OHCI_REG_ConfigROMmap			0x034
#define	OHCI_REG_PostedWriteAddressLo		0x038
#define	OHCI_REG_PostedWriteAddressHi		0x03c
#define	OHCI_REG_VendorId			0x040
#define	OHCI_REG_reserved_044			0x044
#define	OHCI_REG_reserved_048			0x048
#define	OHCI_REG_reserved_04c			0x04c
#define	OHCI_REG_HCControlSet			0x050
#define	OHCI_REG_HCControlClear			0x054
#define	OHCI_REG_reserved_058			0x058
#define	OHCI_REG_reserved_05c			0x05c
#define	OHCI_REG_reserved_060			0x060
#define	OHCI_REG_SelfIDBuffer			0x064
#define	OHCI_REG_SelfIDCount			0x068
#define	OHCI_REG_reserved_06c			0x06c
#define	OHCI_REG_IRMultiChanMaskHiSet		0x070
#define	OHCI_REG_IRMultiChanMaskHiClear		0x074
#define	OHCI_REG_IRMultiChanMaskLoSet		0x078
#define	OHCI_REG_IRMultiChanMaskLoClear		0x07c
#define	OHCI_REG_IntEventSet			0x080
#define	OHCI_REG_IntEventClear			0x084
#define	OHCI_REG_IntMaskSet			0x088
#define	OHCI_REG_IntMaskClear			0x08c
#define	OHCI_REG_IsoXmitIntEventSet		0x090
#define	OHCI_REG_IsoXmitIntEventClear		0x094
#define	OHCI_REG_IsoXmitIntMaskSet		0x098
#define	OHCI_REG_IsoXmitIntMaskClear		0x09c
#define	OHCI_REG_IsoRecvIntEventSet		0x0a0
#define	OHCI_REG_IsoRecvIntEventClear		0x0a4
#define	OHCI_REG_IsoRecvIntMaskSet		0x0a8
#define	OHCI_REG_IsoRecvIntMaskClear		0x0ac
#define	OHCI_REG_InitialBandwidthAvailable	0x0b0
#define	OHCI_REG_InitialChannelsAvailableHi	0x0b4
#define	OHCI_REG_InitialChannelsAvailableLo	0x0b8
#define	OHCI_REG_reserved_0bc			0x0bc
#define	OHCI_REG_reserved_0c0			0x0c0
#define	OHCI_REG_reserved_0c4			0x0c4
#define	OHCI_REG_reserved_0c8			0x0c8
#define	OHCI_REG_reserved_0cc			0x0cc
#define	OHCI_REG_reserved_0d0			0x0d0
#define	OHCI_REG_reserved_0d4			0x0d4
#define	OHCI_REG_reserved_0d8			0x0d8
#define	OHCI_REG_FairnessConctrol		0x0dc
#define	OHCI_REG_LinkControlSet			0x0e0
#define	OHCI_REG_LinkControlClear		0x0e4
#define	OHCI_REG_NodeId				0x0e8
#define	OHCI_REG_PhyControl			0x0ec
#define	OHCI_REG_IsochronousCycleTimer		0x0f0
#define	OHCI_REG_reserved_0f0			0x0f4
#define	OHCI_REG_reserved_0f8			0x0f8
#define	OHCI_REG_reserved_0fc			0x0fc
#define	OHCI_REG_AsynchronousRequestFilterHiSet	0x100
#define	OHCI_REG_AsynchronousRequestFilterHiClear	0x104
#define	OHCI_REG_AsynchronousRequestFilterLoSet	0x108
#define	OHCI_REG_AsynchronousRequestFilterLoClear	0x10c
#define	OHCI_REG_PhysicalRequestFilterHiSet	0x110
#define	OHCI_REG_PhysicalRequestFilterHiClear	0x114
#define	OHCI_REG_PhysicalRequestFilterLoSet	0x118
#define	OHCI_REG_PhysicalRequestFilterLoCLear	0x11c
#define	OHCI_REG_PhysicalUpperBound		0x120
#define	OHCI_REG_reserved_124			0x124
#define	OHCI_REG_reserved_128			0x128
#define	OHCI_REG_reserved_12c			0x12c
#define	OHCI_REG_reserved_130			0x130
#define	OHCI_REG_reserved_134			0x134
#define	OHCI_REG_reserved_138			0x138
#define	OHCI_REG_reserved_13c			0x13c
#define	OHCI_REG_reserved_140			0x140
#define	OHCI_REG_reserved_144			0x144
#define	OHCI_REG_reserved_148			0x148
#define	OHCI_REG_reserved_14c			0x14c
#define	OHCI_REG_reserved_150			0x150
#define	OHCI_REG_reserved_154			0x154
#define	OHCI_REG_reserved_158			0x158
#define	OHCI_REG_reserved_15c			0x15c
#define	OHCI_REG_reserved_160			0x160
#define	OHCI_REG_reserved_164			0x164
#define	OHCI_REG_reserved_168			0x168
#define	OHCI_REG_reserved_16c			0x16c
#define	OHCI_REG_reserved_170			0x170
#define	OHCI_REG_reserved_174			0x174
#define	OHCI_REG_reserved_178			0x178
#define	OHCI_REG_reserved_17c			0x17c


#define	OHCI_REG_ASYNC_DMA_BASE			0x180
#define	OHCI_CTX_ASYNC_TX_REQUEST		0
#define	OHCI_CTX_ASYNC_TX_RESPONSE		1
#define	OHCI_CTX_ASYNC_RX_REQUEST		2
#define	OHCI_CTX_ASYNC_RX_RESPONSE		3
#define	OHCI_SUBREG_ContextControlSet		0x000
#define	OHCI_SUBREG_ContextControlClear		0x004
#define	OHCI_SUBREG_reserved_008		0x008
#define	OHCI_SUBREG_CommandPtr			0x00c
#define	OHCI_SUBREG_CommandMatch		0x010
#define	OHCI_SUBREG_reserved_014		0x014
#define	OHCI_SUBREG_reserved_018		0x018
#define	OHCI_SUBREG_reserved_01c		0x01c
#define	OHCI_ASYNC_DMA_WRITE(sc, ctx, reg, val) \
	OHCI_CSR_WRITE(sc, OHCI_REG_ASYNC_DMA_BASE + 8*(ctx) + (reg), val)
#define	OHCI_ASYNC_DMA_READ (sc, ctx, reg) \
	OHCI_CSR_READ (sc, OHCI_REG_ASYNC_DMA_BASE + 8*(ctx) + (reg))

#define	OHCI_REG_SYNC_TX_DMA_BASE		0x200
#define	OHCI_SYNC_TX_DMA_WRITE(sc, ctx, reg, val) \
	OHCI_CSR_WRITE(sc, OHCI_REG_SYNC_TX_DMA_BASE + 16*(ctx) + (reg), val)
#define	OHCI_SYNC_TX_DMA_READ (sc, ctx, reg) \
	OHCI_CSR_READ (sc, OHCI_REG_SYNC_TX_DMA_BASE + 16*(ctx) + (reg))

#define	OHCI_REG_SYNC_RX_DMA_BASE	0x400
#define	OHCI_SYNC_RX_DMA_WRITE(sc, ctx, reg, val) \
	OHCI_CSR_WRITE(sc, OHCI_REG_SYNC_RX_DMA_BASE + 32*(ctx) + (reg), val)
#define	OHCI_SYNC_RX_DMA_READ (sc, ctx, reg) \
	OHCI_CSR_READ (sc, OHCI_REG_SYNC_RX_DMA_BASE + 32*(ctx) + (reg))

/* OHCI_REG_Version
 */
#define	OHCI_Version_GUID_ROM		0x01000000
#define	OHCI_Version_GET_Version(x)	((((x) >> 16) & 0xf) + (((x) >> 20) & 0xf) * 10)
#define	OHCI_Version_GET_Revision(x)	((((x) >> 4) & 0xf) + ((x) & 0xf) * 10)

/* OHCI_REG_GUIDxx
 */

/* OHCI_REG_BusOptions
 */
#define	OHCI_BusOptions_LinkSpd_MASK	0x00000007
#define	OHCI_BusOptions_LinkSpd_BITPOS	0
#define	OHCI_BusOptions_G_MASK		0x000000c0
#define	OHCI_BusOptions_G_BITPOS	6
#define	OHCI_BusOptions_MaxRec_MASK	0x0000f000
#define	OHCI_BusOptions_MaxRec_BITPOS	12
#define	OHCI_BusOptions_CycClkAcc_MASK	0x00ff0000
#define	OHCI_BusOptions_CycClkAcc_BITPOS 16
#define	OHCI_BusOptions_PMC		0x08000000
#define	OHCI_BusOptions_BMC		0x10000000
#define	OHCI_BusOptions_ISC		0x20000000
#define	OHCI_BusOptions_CMC		0x40000000
#define	OHCI_BusOptions_IRMC		0x80000000
#define	OHCI_BusOptions_reserved	0x07000f38

/* OCHI_REG_Int{Event|Mask}*
 */
#define	OHCI_Int_MasterEnable		0x80000000

/*
 * Section 3.1.1: ContextControl register
 *
 *
 */
#define	OHCI_CTXCTL_RUN			0x00008000
#define	OHCI_CTXCTL_WAKE		0x00001000
#define	OHCI_CTXCTL_DEAD		0x00000800
#define	OHCI_CTXCTL_ACTIVE		0x00000400

#define	OHCI_CTXCTL_SPD_BITLEN		3
#define	OHCI_CTXCTL_SPD_BITPOS		5

#define	OHCI_CTXCTL_SPD_100		0
#define	OHCI_CTXCTL_SPD_200		1
#define	OHCI_CTXCTL_SPD_400		2

#define	OHCI_CTXCTL_EVENT_BITLEN	5
#define	OHCI_CTXCTL_EVENT_BITPOS	0

/* Events from 0 to 15 are generated by the OpenHCI controller.
 * Events from 16 to 31 are four-bit IEEE 1394 ack codes or'ed with bit 4 set.
 */
#define	OHCI_CTXCTL_EVENT_NO_STATUS		0
#define	OHCI_CTXCTL_EVENT_RESERVED1		1

/* The received data length was greater than the buffer's data_length.
 */
#define	OHCI_CTXCTL_EVENT_LONG_PACKET		2

/* A subaction gap was detected before an ack arrived or the received
 * ack had a parity error.
 */
#define	OHCI_CTXCTL_EVENT_MISSING_ACK		3

/* Underrun on the corresponding FIFO. The packet was truncated.
 */
#define	OHCI_CTXCTL_EVENT_UNDERRUN		4

/* A receive FIFO overflowed during the reception of an isochronous packet.
 */
#define	OHCI_CTXCTL_EVENT_OVERRUN		5

/* An unrecoverable error occurred while the Host Controller was reading
 * a descriptor block.
 */
#define	OHCI_CTXCTL_EVENT_DESCRIPTOR_READ	6

/* An error occurred while the Host Controller was attempting to read
 * from host memory in the data stage of descriptor processing.
 */
#define	OHCI_CTXCTL_EVENT_DATA_READ		7

/* An error occurred while the Host Controller was attempting to write
 * to host memory either in the data stage of descriptor processing
 * (AR, IR), or when processing a single 16-bit host * memory write (IT).
 */
#define	OHCI_CTXCTL_EVENT_DATA_WRITE		8

/* Identifies a PHY packet in the receive buffer as being the synthesized
 * bus reset packet.  (See section 8.4.2.3).
 */
#define	OHCI_CTXCTL_EVENT_BUS_RESET		9

/* Indicates that the asynchronous transmit response packet expired and
 * was not transmitted, or that an IT DMA context experienced a skip
 * processing overflow (See section 9.3.3).
 */
#define	OHCI_CTXCTL_EVENT_TIMEOUT		10

/* A bad tCode is associated with this packet. The packet was flushed.
 */
#define	OHCI_CTXCTL_EVENT_TCODE_ERR		11
#define	OHCI_CTXCTL_EVENT_RESERVED12		12
#define	OHCI_CTXCTL_EVENT_RESERVED13		13

/* An error condition has occurred that cannot be represented
 * by any other event codes defined herein.
 */
#define	OHCI_CTXCTL_EVENT_UNKNOWN		14

/* Sent by the link side of the output FIFO when asynchronous
 * packets are being flushed due to a bus reset.
 */
#define	OHCI_CTXCTL_EVENT_FLUSHED		15

/* IEEE1394 derived ACK codes follow
 */
#define	OHCI_CTXCTL_EVENT_RESERVED16		16

/* For asynchronous request and response packets, this event
 * indicates the destination node has successfully accepted
 * the packet. If the packet was a request subaction, the
 * destination node has successfully completed the transaction
 * and no response subaction shall follow.  The event code for
 * transmitted PHY, isochronous, asynchronous stream and broadcast
 * packets, none of which yields a 1394 ack code, shall be set
 * by hardware to ack_complete unless an event occurs.
 */
#define	OHCI_CTXCTL_EVENT_ACK_COMPLETE		17

/* The destination node has successfully accepted the packet.
 * If the packet was a request subaction, a response subaction
 * should follow at a later time. This code is not returned for
 * a response subaction.
 */
#define	OHCI_CTXCTL_EVENT_ACK_PENDING		18
#define	OHCI_CTXCTL_EVENT_RESERVED19		19

/* The packet could not be accepted after max ATRetries (see
 * section 5.4) attempts, and the last ack received was ack_busy_X.
 */
#define	OHCI_CTXCTL_EVENT_ACK_BUSY_X		20

/* The packet could not be accepted after max ATRetries (see
 * section 5.4) attempts, and the last ack received was ack_busy_A.
 */
#define	OHCI_CTXCTL_EVENT_ACK_BUSY_A		21

/* The packet could not be accepted after max AT Retries (see
 * section 5.4) attempts, and the last ack received was ack_busy_B.
 */
#define	OHCI_CTXCTL_EVENT_ACK_BUSY_B		22
#define	OHCI_CTXCTL_EVENT_RESERVED23		23
#define	OHCI_CTXCTL_EVENT_RESERVED24		24
#define	OHCI_CTXCTL_EVENT_RESERVED25		25
#define	OHCI_CTXCTL_EVENT_RESERVED26		26

/* The destination node could not accept the packet because
 * the link and higher layers are in a suspended state.
 */
#define	OHCI_CTXCTL_EVENT_ACK_TARDY		27
#define	OHCI_CTXCTL_EVENT_RESERVED28		28

/* An AT context received an ack_data_error, or an IR context
 * in packet-per-buffer mode detected a data field CRC or
 * data_length error.
 */
#define	OHCI_CTXCTL_EVENT_ACK_DATA_ERROR	29

/* A field in the request packet header was set to an unsupported or
 * incorrect value, or an invalid transaction was attempted (e.g., a
 * write to a read-only address).
 */
#define	OHCI_CTXCTL_EVENT_ACK_TYPE_ERROR	30
#define	OHCI_CTXCTL_EVENT_RESERVED31		31

#endif	/* _DEV_IEEE1394_FWOHCIREG_ */
