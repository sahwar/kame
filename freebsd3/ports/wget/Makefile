# New ports collection makefile for:	wget
# Date created:		31 December 1996
# Whom:			Thomas Gellekum <tg@FreeBSD.ORG>
#
# $KAME: Makefile,v 1.5 2000/09/25 17:03:12 sumikawa Exp $
#

PORTNAME=	wget
PORTVERSION=	1.5.3
CATEGORIES=	net ipv6
MASTER_SITES=	${MASTER_SITE_GNU} \
		ftp://ftp.dl.ac.uk/ccp14/ftp-mirror/wget/pub/unix/util/wget/ \
		ftp://ftp.informatik.hu-berlin.de/gnu/wget/ \
		ftp://ftp.uni-mainz.de/pub/gnu/wget/ \
		ftp://ftp.max.irk.ru/pub/unix/net/www/wget/
MASTER_SITE_SUBDIR=	wget

PATCH_SITES=	ftp://ftp.kame.net/pub/kame/misc/
PATCHFILES=	wget-153-v6-20000915.diff.gz
PATCH_DIST_STRIP=	-p1

MAINTAINER=	sumikawa@kame.net

LIB_DEPENDS=	intl.1:${PORTSDIR}/devel/gettext

GNU_CONFIGURE=	yes

post-install:
	if [ ! -f ${PREFIX}/info/dir ]; then \
		${SED} -ne '1,/Menu:/p' /usr/share/info/dir > ${PREFIX}/info/dir; \
	fi
	-install-info ${PREFIX}/info/wget.info ${PREFIX}/info/dir

.include <bsd.port.mk>
