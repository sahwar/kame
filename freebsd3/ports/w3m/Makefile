# New ports collection makefile for:   w3m
# Date Created:			7 Oct 1999
# Whom:				MANTANI Nobutaka <nobutaka@nobutaka.com>
#
# $FreeBSD: ports/www/w3m/Makefile,v 1.19 2000/07/08 07:52:15 knu Exp $
#

PORTNAME=	w3m
PORTVERSION=	0.1.11.p
CATEGORIES+=	www ipv6
MASTER_SITES=	ftp://ei5nazha.yz.yamagata-u.ac.jp/w3m/dev/ \
		ftp://ftp.firedrake.org/w3m/dev/ \
		http://www.instinct.org/w3m/dev/ \
		http://grilli.net/mirrors/w3m/download/dev/
DISTNAME=	${PORTNAME}-${PORTVERSION:S/.p/-pre/}

MAINTAINER=	sumikawa@kame.net

BUILD_DEPENDS=	${LOCALBASE}/lib/libgc.a:${PORTSDIR}/devel/boehm-gc

MAN1=		w3m.1
MANLANG=	""
CFLAGS=		-pipe

TARGET=		${WRKSRC}/config.h
DOCS=		FAQ.html HISTORY MANUAL.html README \
		README.dict README.func STORY.html keymap.default \
		keymap.lynx menu.default menu.submenu
SED_CMD=	-e "s,%%PREFIX%%,${PREFIX},g" \
		-e "s,%%LOCALBASE%%,${LOCALBASE},g"

.include <bsd.port.pre.mk>

#.if ${OSVERSION} >= 400014
SED_CMD+=	-e "s,undef INET6,define INET6,g"
#.endif

.if defined(JAPANESE)
SED_CMD+=	-e "s,undef JAPANESE,define JAPANESE,g"
MANLANG+=	ja
DOCS_JP=	${DOCS} HISTORY.kokb README.keymap README.kokb \
		README.menu
.endif

.if defined(USE_OPENSSL)
SSL_CFLAGS=	-I${OPENSSLINC}/openssl -I${OPENSSLINC} ${OPENSSL_CFLAGS}
SSL_LIBS=	-L${OPENSSLLIB} -lssl -lcrypto

SED_CMD+=	-e "s,undef USE_SSL,define USE_SSL,g" \
		-e "s,undef USE_SSL_VERIFY, define USE_SSL_VERIFY,g"
.if defined(OPENSSL_RSAREF)
EXTRA_SSL_LIBS=	-lRSAglue -lrsaref
.endif
MAKE_FLAGS+=	DEFS="${SSL_CFLAGS} -I${LOCALBASE}/include" \
		LIBS="${SSL_LIBS} ${EXTRA_SSL_LIBS} -L${LOCALBASE}/lib"
.else
MAKE_FLAGS+=	DEFS="-I${LOCALBASE}/include" LIBS="-L${LOCALBASE}/lib"
.endif

post-configure:
	@${CP} ${TARGET} ${TARGET}.in
	@${SED} ${SED_CMD} < ${TARGET}.in > ${TARGET}
	@${SED} -e "s,@PERL@,${PERL},g" \
		-e "s,@CYGWIN@,0,g" \
		< ${WRKSRC}/scripts/dirlist.in > ${WRKSRC}/scripts/dirlist.cgi

post-install:
.if !defined(NOPORTDOCS)
.if defined(JAPANESE)
	@${MKDIR} ${PREFIX}/share/doc/ja/w3m
	@cd ${WRKSRC}/doc-jp; \
	for i in ${DOCS_JP} ; do \
		${INSTALL_DATA} $$i ${PREFIX}/share/doc/ja/w3m/ ; \
	done
.endif
	@cd ${WRKSRC}/doc; \
	for i in ${DOCS} ; do \
		${INSTALL_DATA} $$i ${PREFIX}/share/doc/w3m/ ; \
	done
.endif
	@${INSTALL_MAN} ${WRKSRC}/doc/w3m.1 ${PREFIX}/man/man1
.if defined(JAPANESE)
	@${INSTALL_MAN} ${WRKSRC}/doc-jp/w3m.1 ${PREFIX}/man/ja/man1
.endif

.include <bsd.port.post.mk>
