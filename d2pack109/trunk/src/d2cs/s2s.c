/*
 * Copyright (C) 2000,2001	Onlyer	(onlyer@263.net)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include "common/setup_before.h"
#include "setup.h"

#ifdef HAVE_STDDEF_H
# include <stddef.h>
#else
# ifndef NULL
#  define NULL ((void *)0)
# endif
#endif
#include <ctype.h>
#ifdef HAVE_STRING_H
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
# ifdef HAVE_MEMORY_H
#  include <memory.h>
# endif
#endif
#include "compat/memset.h"
#include "compat/strdup.h"
#ifdef STDC_HEADERS
# include <stdlib.h>
#else
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif
#include <errno.h>
#include "compat/strerror.h"
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#include "compat/psock.h"
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#include "compat/netinet_in.h"
#include <ctype.h>

#include "compat/psock.h"
#include "compat/strtoul.h"
#include "connection.h"
#include "bnetd.h"
#include "net.h"
#include "s2s.h"
#include "common/eventlog.h"
#include "common/setup_after.h"

extern int s2s_check(void)
{
	bnetd_check();
	return 0;
}

extern int s2s_init(void)
{
	bnetd_init();
	return 0;
}

extern t_connection * s2s_create(char const * server, unsigned short def_port, t_conn_class class)
{
	struct sockaddr_in	addr, laddr;
	psock_t_socklen		laddr_len;
	unsigned int		ip;
	unsigned short		port;
	int			sock, connected;
	t_connection		* c;
	char			* p, * tserver;

	ASSERT(server,NULL);
	if (!(tserver=strdup(server))) {
		return NULL;
	}
	p=strchr(tserver,':');
	if (p) {
		port=(unsigned short)strtoul(p+1,NULL,10);
		*p='\0';
	} else {
		port=def_port;
	}
	if ((sock=net_socket(PSOCK_SOCK_STREAM))<0) {
		eventlog(eventlog_level_error,__FUNCTION__,"error creating s2s socket");
		free(tserver);
		return NULL;
	}
	memset(&addr,0,sizeof(addr));
	addr.sin_family = PSOCK_AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr= net_inet_addr(tserver);
	free(tserver);
	eventlog(eventlog_level_info,__FUNCTION__,"try make s2s connection to %s",server);
	if (psock_connect(sock,(struct sockaddr *)&addr,sizeof(addr))<0) {
		if (psock_errno()!=PSOCK_EWOULDBLOCK && psock_errno() != PSOCK_EINPROGRESS) {
			eventlog(eventlog_level_error,__FUNCTION__,"error connecting to %s (psock_connect: %s)",server,strerror(psock_errno()));
			psock_close(sock);
			return NULL;
		}
		connected=0;
		eventlog(eventlog_level_info,__FUNCTION__,"connection to s2s server %s is in progress",server);
	} else {
		connected=1;
		eventlog(eventlog_level_info,__FUNCTION__,"connected to s2s server %s",server);
	}
	laddr_len=sizeof(laddr);
	memset(&laddr,0,sizeof(laddr));
	ip=port=0;
	if (psock_getsockname(sock,(struct sockaddr *)&laddr,&laddr_len)<0) {
		eventlog(eventlog_level_error,__FUNCTION__,"unable to get local socket info");
	} else {
		if (laddr.sin_family != PSOCK_AF_INET) {
			eventlog(eventlog_level_error,__FUNCTION__,"got bad socket family %d",laddr.sin_family);
		} else {
			ip=ntohl(laddr.sin_addr.s_addr);
			port=ntohs(laddr.sin_port);
		}
	}
	if (!(c=d2cs_conn_create(sock,ip,port, ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port)))) {
		eventlog(eventlog_level_error,__FUNCTION__,"error create s2s connection");
		psock_close(sock);
		return NULL;
	}
	if (connected) {
		d2cs_conn_set_state(c,conn_state_init);
	} else {
		d2cs_conn_set_state(c,conn_state_connecting);
	}
	d2cs_conn_set_class(c,class);
	return c;
}

extern int s2s_destroy(t_connection * c)
{
	ASSERT(c,-1);
	switch (d2cs_conn_get_class(c)) {
		case conn_class_bnetd:
			bnetd_destroy(c);
			break;
		default:
			eventlog(eventlog_level_error,__FUNCTION__,"got bad s2s connection class %d",d2cs_conn_get_class(c));
			return -1;
	}
	return 0;
}
