/*	$OpenBSD: netdb.h,v 1.11 2000/10/04 22:54:23 espie Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * ++Copyright++ 1980, 1983, 1988, 1993
 * -
 * Copyright (c) 1980, 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */

/*
 * %%% portions-copyright-cmetz-96
 * Portions of this software are Copyright 1996-1998 by Craig Metz, All Rights
 * Reserved. The Inner Net License Version 2 applies to these portions of
 * the software.
 * You should have received a copy of the license with this software. If
 * you didn't get a copy, you may request one from <license@inner.net>.
 */

/*
 *      @(#)netdb.h	8.1 (Berkeley) 6/2/93
 *	$From: netdb.h,v 8.7 1996/05/09 05:59:09 vixie Exp $
 */

#ifndef _NETDB_H_
#define _NETDB_H_

#include <sys/param.h>
#if (!defined(BSD)) || (BSD < 199306)
# include <sys/bitypes.h>
#endif
#include <sys/cdefs.h>
#include <sys/types.h>

#define	_PATH_HEQUIV	"/etc/hosts.equiv"
#define	_PATH_HOSTS	"/etc/hosts"
#define	_PATH_NETWORKS	"/etc/networks"
#define	_PATH_PROTOCOLS	"/etc/protocols"
#define	_PATH_SERVICES	"/etc/services"

extern int h_errno;

/*
 * Structures returned by network data base library.  All addresses are
 * supplied in host order, and returned in network order (suitable for
 * use in system calls).
 */
struct	hostent {
	char	*h_name;	/* official name of host */
	char	**h_aliases;	/* alias list */
	int	h_addrtype;	/* host address type */
	int	h_length;	/* length of address */
	char	**h_addr_list;	/* list of addresses from name server */
#define	h_addr	h_addr_list[0]	/* address, for backward compatiblity */
};

/*
 * Assumption here is that a network number
 * fits in an in_addr_t -- probably a poor one.
 */
struct	netent {
	char		*n_name;	/* official name of net */
	char		**n_aliases;	/* alias list */
	int		n_addrtype;	/* net address type */
	in_addr_t	n_net;		/* network # */
};

struct	servent {
	char	*s_name;	/* official service name */
	char	**s_aliases;	/* alias list */
	int	s_port;		/* port # */
	char	*s_proto;	/* protocol to use */
};

struct	protoent {
	char	*p_name;	/* official protocol name */
	char	**p_aliases;	/* alias list */
	int	p_proto;	/* protocol # */
};

/*
 * Error return codes from gethostbyname() and gethostbyaddr()
 * (left in extern int h_errno).
 */

#define	NETDB_INTERNAL	-1	/* see errno */
#define	NETDB_SUCCESS	0	/* no problem */
#define	HOST_NOT_FOUND	1 /* Authoritative Answer Host not found */
#define	TRY_AGAIN	2 /* Non-Authoritive Host not found, or SERVERFAIL */
#define	NO_RECOVERY	3 /* Non recoverable errors, FORMERR, REFUSED, NOTIMP */
#define	NO_DATA		4 /* Valid name, no data record of requested type */
#define	NO_ADDRESS	NO_DATA		/* no address, look for MX record */

/* Values for getaddrinfo() and getnameinfo() */
#define AI_PASSIVE	1	/* socket address is intended for bind() */
#define AI_CANONNAME	2	/* request for canonical name */
#define AI_NUMERICHOST	4	/* don't ever try nameservice */
/* valid flags for addrinfo */
#define AI_MASK		(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST)

#if !defined(_XOPEN_SOURCE) || (_XOPEN_SOURCE - 0) >= 500
#define	AI_ALL		0x00000100 /* IPv6 and IPv4-mapped (with AI_V4MAPPED) */
#endif
#if !defined(_XOPEN_SOURCE)
#define	AI_V4MAPPED_CFG	0x00000200 /* accept IPv4-mapped if kernel supports */
#endif
#if !defined(_XOPEN_SOURCE) || (_XOPEN_SOURCE - 0) >= 500
#define	AI_ADDRCONFIG	0x00000400 /* only if any address is assigned */
#define	AI_V4MAPPED	0x00000800 /* accept IPv4-mapped IPv6 address */
/* special recommended flags for getipnodebyname */
#define	AI_DEFAULT	(AI_V4MAPPED_CFG | AI_ADDRCONFIG)
#endif


#define NI_NUMERICHOST	1	/* return the host address, not the name */
#define NI_NUMERICSERV	2	/* return the service address, not the name */
#define NI_NOFQDN	4	/* return a short name if in the local domain */
#define NI_NAMEREQD	8	/* fail if either host or service name is unknown */
#define NI_DGRAM	16	/* look up datagram service instead of stream */
#define NI_WITHSCOPEID	32	/* KAME hack: attach scopeid to host portion */

#define NI_MAXHOST	MAXHOSTNAMELEN	/* max host name returned by getnameinfo */
#define NI_MAXSERV	32	/* max serv. name length returned by getnameinfo */

/*
 * Scope delimit character (KAME hack)
 */
#define SCOPE_DELIMITER '%'

#define EAI_BADFLAGS	-1	/* invalid value for ai_flags */
#define EAI_NONAME	-2	/* name or service is not known */
#define EAI_AGAIN	-3	/* temporary failure in name resolution */
#define EAI_FAIL	-4	/* non-recoverable failure in name resolution */
/* #define EAI_NODATA	-5	(obsoleted) */
#define EAI_FAMILY	-6	/* ai_family not supported */
#define EAI_SOCKTYPE	-7	/* ai_socktype not supported */
#define EAI_SERVICE	-8	/* service not supported for ai_socktype */
/* #define EAI_ADDRFAMILY	(obsoleted) */
#define EAI_MEMORY	-10	/* memory allocation failure */
#define EAI_SYSTEM	-11	/* system error (code indicated in errno) */
#define EAI_BADHINTS	-12	/* invalid value for hints */
#define EAI_PROTOCOL	-13	/* resolved protocol is unknown */

struct addrinfo {
	int ai_flags;		/* input flags */
	int ai_family;		/* protocol family for socket */
	int ai_socktype;	/* socket type */
	int ai_protocol;	/* protocol for socket */
	int ai_addrlen;		/* length of socket-address */
	struct sockaddr *ai_addr; /* socket-address for socket */
	char *ai_canonname;	/* canonical name for service location (iff req) */
	struct addrinfo *ai_next; /* pointer to next in list */
};

__BEGIN_DECLS
void		endhostent __P((void));
void		endnetent __P((void));
void		endprotoent __P((void));
void		endservent __P((void));
#if !defined(_XOPEN_SOURCE) || (_XOPEN_SOURCE - 0) >= 500
void		freehostent __P((struct hostent *));
#endif
struct hostent	*gethostbyaddr __P((const char *, int, int));
struct hostent	*gethostbyname __P((const char *));
struct hostent	*gethostbyname2 __P((const char *, int));
struct hostent	*gethostent __P((void));
#if !defined(_XOPEN_SOURCE) || (_XOPEN_SOURCE - 0) >= 500
struct hostent	*getipnodebyaddr __P((const void *, size_t, int, int *));
struct hostent	*getipnodebyname __P((const char *, int, int, int *));
#endif
struct netent	*getnetbyaddr __P((in_addr_t, int));
struct netent	*getnetbyname __P((const char *));
struct netent	*getnetent __P((void));
struct protoent	*getprotobyname __P((const char *));
struct protoent	*getprotobynumber __P((int));
struct protoent	*getprotoent __P((void));
struct servent	*getservbyname __P((const char *, const char *));
struct servent	*getservbyport __P((int, const char *));
struct servent	*getservent __P((void));
void		herror __P((const char *));
const char	*hstrerror __P((int));
void		sethostent __P((int));
/* void		sethostfile __P((const char *)); */
void		setnetent __P((int));
void		setprotoent __P((int));
void		setservent __P((int));

int		getaddrinfo __P((const char *, const char *,
		    const struct addrinfo *, struct addrinfo **));
void		freeaddrinfo __P((struct addrinfo *));
int		getnameinfo __P((const struct sockaddr *, socklen_t,
		    char *, size_t, char *, size_t,
		    int));
char		*gai_strerror __P((int));
int		net_addrcmp __P((struct sockaddr *, struct sockaddr *));
__END_DECLS

/* This is nec'y to make this include file properly replace the sun version. */
#ifdef sun
#ifdef __GNU_LIBRARY__
#include <rpc/netdb.h>
#else
struct rpcent {
	char	*r_name;	/* name of server for this rpc program */
	char	**r_aliases;	/* alias list */
	int	r_number;	/* rpc program number */
};
struct rpcent	*getrpcbyname(), *getrpcbynumber(), *getrpcent();
#endif /* __GNU_LIBRARY__ */
#endif /* sun */

#endif /* !_NETDB_H_ */
