# $FreeBSD: src/sys/modules/aac/Makefile,v 1.5 2002/09/25 05:00:25 scottl Exp $

.PATH: ${.CURDIR}/../../dev/aac

SUBDIR= aac_linux

KMOD=	aac
SRCS=	aac.c aac_pci.c aac_disk.c aac_cam.c
SRCS+=	opt_scsi.h opt_cam.h opt_aac.h
SRCS+=	device_if.h bus_if.h pci_if.h

# To enable debug output from the driver, uncomment these two lines.
#CFLAGS+= -DAAC_DEBUG=2
#SRCS+=	aac_debug.c

.include <bsd.kmod.mk>
