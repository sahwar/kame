# $FreeBSD: src/sys/modules/vx/Makefile,v 1.2 2001/01/06 14:00:41 obrien Exp $

.PATH: ${.CURDIR}/../../dev/vx

KMOD=	if_vx
SRCS=	if_vx.c if_vx_pci.c
.if ${MACHINE_ARCH} == "i386"
SRCS+=	if_vx_eisa.c
.endif
SRCS+=	opt_bdg.h device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
