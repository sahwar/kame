/*	$NetBSD: usbdivar.h,v 1.46 2000/01/19 01:16:40 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbdivar.h,v 1.14 2000/01/23 19:26:16 peter Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@carlstedt.se) at
 * Carlstedt Research & Technology.
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

/* From usb_mem.h */
DECLARE_USB_DMA_T;

struct usbd_xfer;
struct usbd_pipe;

struct usbd_endpoint {
	usb_endpoint_descriptor_t *edesc;
	int			refcnt;
};

struct usbd_bus_methods {
	usbd_status	      (*open_pipe)__P((struct usbd_pipe *pipe));
	void		      (*do_poll)__P((struct usbd_bus *));
	usbd_status	      (*allocm)__P((struct usbd_bus *, usb_dma_t *,
					    u_int32_t bufsize));
	void		      (*freem)__P((struct usbd_bus *, usb_dma_t *));
	struct usbd_xfer *    (*allocx)__P((struct usbd_bus *));
	void		      (*freex)__P((struct usbd_bus *,
					   struct usbd_xfer *));
};

struct usbd_pipe_methods {
	usbd_status	      (*transfer)__P((usbd_xfer_handle xfer));
	usbd_status	      (*start)__P((usbd_xfer_handle xfer));
	void		      (*abort)__P((usbd_xfer_handle xfer));
	void		      (*close)__P((usbd_pipe_handle pipe));
	void		      (*cleartoggle)__P((usbd_pipe_handle pipe));
	void		      (*done)__P((usbd_xfer_handle xfer));
};

struct usbd_port {
	usb_port_status_t	status;
	u_int16_t		power;	/* mA of current on port */
	u_int8_t		portno;
	u_int8_t		restartcnt;
#define USBD_RESTART_MAX 5
	struct usbd_device     *device;
	struct usbd_device     *parent;	/* The ports hub */
};

struct usbd_hub {
	usbd_status	      (*explore)__P((usbd_device_handle hub));
	void		       *hubsoftc;
	usb_hub_descriptor_t	hubdesc;
	struct usbd_port        ports[1];
};

struct usb_softc;

/*****/

struct usbd_bus {
	/* Filled by HC driver */
	USBBASEDEVICE		bdev; /* base device, host adapter */
	struct usbd_bus_methods	*methods;
	u_int32_t		pipe_size; /* size of a pipe struct */
	/* Filled by usb driver */
	struct usbd_device     *root_hub;
	usbd_device_handle	devices[USB_MAX_DEVICES];
	char			needs_explore;/* a hub a signalled a change */
	char			use_polling;
	struct usb_softc       *usbctl;
	struct usb_device_stats	stats;
	int 			intr_context;
	u_int			no_intrs;
	int			usbrev;	/* USB revision */
#define USBREV_UNKNOWN	0
#define USBREV_PRE_1_0	1
#define USBREV_1_0	2
#define USBREV_1_1	3
#define USBREV_STR { "unknown", "pre 1.0", "1.0", "1.1" }

#if defined(__NetBSD__) || defined(__OpenBSD__)
	bus_dma_tag_t		dmatag;	/* DMA tag */
#endif
};

struct usbd_device {
	struct usbd_bus	       *bus;           /* our controller */
	struct usbd_pipe       *default_pipe;  /* pipe 0 */
	u_int8_t		address;       /* device addess */
	u_int8_t		config;	       /* current configuration # */
	u_int8_t		depth;         /* distance from root hub */
	u_int8_t		lowspeed;      /* lowspeed flag */
	u_int8_t		self_powered;  /* flag for self powered */
	u_int16_t		power;         /* mA the device uses */
	int16_t			langid;	       /* language for strings */
#define USBD_NOLANG (-1)
	usb_event_cookie_t	cookie;	       /* unique connection id */
	struct usbd_port       *powersrc;      /* upstream hub port, or 0 */
	struct usbd_endpoint	def_ep;	       /* for pipe 0 */
	usb_endpoint_descriptor_t def_ep_desc; /* for pipe 0 */
	struct usbd_interface  *ifaces;        /* array of all interfaces */
	usb_device_descriptor_t ddesc;         /* device descriptor */
	usb_config_descriptor_t *cdesc;	       /* full config descr */
	struct usbd_quirks     *quirks;        /* device quirks, always set */
	struct usbd_hub	       *hub;           /* only if this is a hub */
	device_ptr_t	       *subdevs;       /* sub-devices, 0 terminated */
};

struct usbd_interface {
	struct usbd_device     *device;
	usb_interface_descriptor_t *idesc;
	int			index;
	int			altindex;
	struct usbd_endpoint   *endpoints;
	void		       *priv;
	LIST_HEAD(, usbd_pipe)	pipes;
};

struct usbd_pipe {
	struct usbd_interface  *iface;
	struct usbd_device     *device;
	struct usbd_endpoint   *endpoint;
	int			refcnt;
	char			running;
	SIMPLEQ_HEAD(, usbd_xfer) queue;
	LIST_ENTRY(usbd_pipe)	next;

	usbd_xfer_handle	intrxfer; /* used for repeating requests */
	char			repeat;
	int			interval;

	/* Filled by HC driver. */
	struct usbd_pipe_methods *methods;
};

struct usbd_xfer {
	struct usbd_pipe       *pipe;
	void		       *priv;
	void		       *buffer;
	u_int32_t		length;
	u_int32_t		actlen;
	u_int16_t		flags;
	u_int32_t		timeout;
	usbd_status		status;
	usbd_callback		callback;
	__volatile char		done;

	/* For control pipe */
	usb_device_request_t	request;

	/* For isoc */
	u_int16_t		*frlengths;
	int			nframes;

	/* For memory allocation */
	struct usbd_device     *device;
	usb_dma_t		dmabuf;

	int			rqflags;
#define URQ_REQUEST	0x01
#define URQ_AUTO_DMABUF	0x10
#define URQ_DEV_DMABUF	0x20

	SIMPLEQ_ENTRY(usbd_xfer) next;

	void		       *hcpriv; /* private use by the HC driver */
	int			hcprivint;

#if defined(__FreeBSD__)
	struct callout_handle  timo_handle;
#endif
};

void usbd_init __P((void));
void usbd_finish __P((void));

/* Routines from usb_subr.c */
int		usbctlprint __P((void *, const char *));
void		usb_delay_ms __P((usbd_bus_handle, u_int));
void		usbd_devinfo_vp __P((usbd_device_handle, char *, char *));
usbd_status	usbd_reset_port __P((usbd_device_handle dev,
				     int port, usb_port_status_t *ps));
usbd_status	usbd_setup_pipe __P((usbd_device_handle dev,
				     usbd_interface_handle iface,
				     struct usbd_endpoint *, int,
				     usbd_pipe_handle *pipe));
usbd_status	usbd_new_device __P((device_ptr_t parent, 
				     usbd_bus_handle bus, int depth,
				     int lowspeed, int port, 
				     struct usbd_port *));
void		usbd_remove_device __P((usbd_device_handle,
					struct usbd_port *));
int		usbd_printBCD __P((char *cp, int bcd));
usbd_status	usbd_fill_iface_data __P((usbd_device_handle dev, 
					  int i, int a));
void		usb_free_device __P((usbd_device_handle));

usbd_status	usb_insert_transfer __P((usbd_xfer_handle xfer));
void		usb_transfer_complete __P((usbd_xfer_handle xfer));
void		usb_disconnect_port __P((struct usbd_port *up, device_ptr_t));

/* Routines from usb.c */
int		usb_bus_count __P((void));
void		usb_needs_explore __P((usbd_bus_handle));

#ifdef DIAGNOSTIC
#define SPLUSBCHECK \
	do { int _s = splusb(), _su = splusb(); \
             if (!cold && _s != _su) printf("SPLUSBCHECK failed 0x%x!=0x%x, %s:%d\n", \
				   _s, _su, __FILE__, __LINE__); \
	     splx(_s); \
        } while (0)
#else
#define SPLUSBCHECK
#endif

/* Locator stuff. */

#if defined(__NetBSD__)
#include "locators.h"
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
/* XXX these values are used to statically bind some elements in the USB tree
 * to specific driver instances. This should be somehow emulated in FreeBSD
 * but can be done later on.
 * The values are copied from the files.usb file in the NetBSD sources.
 */
#define UHUBCF_PORT_DEFAULT -1
#define UHUBCF_CONFIGURATION_DEFAULT -1
#define UHUBCF_INTERFACE_DEFAULT -1
#define UHUBCF_VENDOR_DEFAULT -1
#define UHUBCF_PRODUCT_DEFAULT -1
#define UHUBCF_RELEASE_DEFAULT -1
#endif

#if defined (__OpenBSD__)
#define	UHUBCF_PORT		0
#define	UHUBCF_CONFIGURATION	1
#define	UHUBCF_INTERFACE	2
#define	UHUBCF_VENDOR		3
#define	UHUBCF_PRODUCT		4
#define	UHUBCF_RELEASE		5
#endif

#define	uhubcf_port		cf_loc[UHUBCF_PORT]
#define	uhubcf_configuration	cf_loc[UHUBCF_CONFIGURATION]
#define	uhubcf_interface	cf_loc[UHUBCF_INTERFACE]
#define	uhubcf_vendor		cf_loc[UHUBCF_VENDOR]
#define	uhubcf_product		cf_loc[UHUBCF_PRODUCT]
#define	uhubcf_release		cf_loc[UHUBCF_RELEASE]
#define	UHUB_UNK_PORT		UHUBCF_PORT_DEFAULT /* wildcarded 'port' */
#define	UHUB_UNK_CONFIGURATION	UHUBCF_CONFIGURATION_DEFAULT /* wildcarded 'configuration' */
#define	UHUB_UNK_INTERFACE	UHUBCF_INTERFACE_DEFAULT /* wildcarded 'interface' */
#define	UHUB_UNK_VENDOR		UHUBCF_VENDOR_DEFAULT /* wildcarded 'vendor' */
#define	UHUB_UNK_PRODUCT	UHUBCF_PRODUCT_DEFAULT /* wildcarded 'product' */
#define	UHUB_UNK_RELEASE	UHUBCF_RELEASE_DEFAULT /* wildcarded 'release' */

