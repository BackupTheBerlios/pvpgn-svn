/*
 * Copyright (C) 2002
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
#define D2GSTRANS_INTERNAL_ACCESS
#include "common/setup_before.h"
#include <stdio.h>
#ifdef HAVE_STDDEF_H
# include <stddef.h>
#else
# ifndef NULL
#  define NULL ((void *)0)
# endif
#endif
#ifdef STDC_HEADERS
# include <stdlib.h>
#else
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif
#include "compat/strrchr.h"
#include <errno.h>
#include "compat/strerror.h"
#include "common/eventlog.h"
#include "common/list.h"
#include "common/addr.h"
#include "common/util.h"
#include "d2gstrans.h"
#include "common/setup_after.h"

static t_list * d2gstrans_head=NULL;

extern int d2gstrans_load(char const * filename)
{
    FILE *        fp;
    unsigned int  line;
    unsigned int  pos;
    char *        buff;
    char *        temp;
    char const *  server;
    char const *  output;
    char const *  exclude;
    t_d2gstrans * entry;
    
    if (!filename)
    {
        eventlog(eventlog_level_error,__FUNCTION__,"got NULL filename");
        return -1;
    }
    
    if (!(d2gstrans_head = list_create()))
    {
        eventlog(eventlog_level_error,__FUNCTION__,"could not create list");
        return -1;
    }
    if (!(fp = fopen(filename,"r")))
    {
        eventlog(eventlog_level_error,__FUNCTION__,"could not open file \"%s\" for reading (fopen: %s)",filename,strerror(errno));
	list_destroy(d2gstrans_head);
	d2gstrans_head = NULL;
        return -1;
    }
    
    for (line=1; (buff = file_get_line(fp)); line++)
    {
        for (pos=0; buff[pos]=='\t' || buff[pos]==' '; pos++);
        if (buff[pos]=='\0' || buff[pos]=='#')
        {
            free(buff);
            continue;
        }
        if ((temp = strrchr(buff,'#')))
        {
	    unsigned int len;
	    unsigned int endpos;
	    
            *temp = '\0';
	    len = strlen(buff)+1;
            for (endpos=len-1; buff[endpos]=='\t' || buff[endpos]==' '; endpos--);
            buff[endpos+1] = '\0';
        }
	if (!(server = strtok(buff," \t"))) /* strtok modifies the string it is passed */
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"missing server on line %u of file \"%s\"",line,filename);
	    free(buff);
	    continue;
	}
	if (!(output = strtok(NULL," \t")))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"missing output on line %u of file \"%s\"",line,filename);
	    free(buff);
	    continue;
	}
	if (!(exclude = strtok(NULL," \t")))
	    exclude = "0.0.0.0/0"; /* no excluded network address */

	if (!(entry = malloc(sizeof(t_d2gstrans))))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"could not allocate memory for entry");
	    free(buff);
	    continue;
	}
	if (!(entry->server = addr_create_str(server,0,4000)))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"could not allocate memory for server address");
	    free(entry);
	    free(buff);
	    continue;
	}
	if (!(entry->output = addr_create_str(output,0,4000)))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"could not allocate memory for output address");
	    addr_destroy(entry->server);
	    free(entry);
	    free(buff);
	    continue;
	}
	if (!(entry->exclude = netaddr_create_str(exclude)))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"could not allocate memory for exclude address");
	    addr_destroy(entry->output);
	    addr_destroy(entry->server);
	    free(entry);
	    free(buff);
	    continue;
	}
	
	free(buff);

	if (list_append_data(d2gstrans_head,entry)<0)
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"could not append item");
	    netaddr_destroy(entry->exclude);
	    addr_destroy(entry->output);
	    addr_destroy(entry->server);
	    free(entry);
	}
    }
    fclose(fp);
    eventlog(eventlog_level_info,__FUNCTION__,"d2gstrans file loaded");
    return 0;
}


extern int d2gstrans_unload(void)
{
    t_elem *      curr;
    t_d2gstrans * entry;
    
    if (d2gstrans_head)
    {
	LIST_TRAVERSE(d2gstrans_head,curr)
	{
	    if (!(entry = elem_get_data(curr)))
		eventlog(eventlog_level_error,__FUNCTION__,"found NULL entry in list");
	    else
	    {
		netaddr_destroy(entry->exclude);
		addr_destroy(entry->output);
		addr_destroy(entry->server);
		free(entry);
	    }
	    list_remove_elem(d2gstrans_head,curr);
	}
	list_destroy(d2gstrans_head);
	d2gstrans_head = NULL;
    }
    
    return 0;
}

extern int d2gstrans_reload(char const * filename)
{
    d2gstrans_unload();
    if(d2gstrans_load(filename)<0) return -1;
    return 0;
}


extern void d2gstrans_net(unsigned int clientaddr, unsigned int *gsaddr)
{
    t_elem const * curr;
    t_d2gstrans *  entry;
    char           temp1[32];
    char           temp2[32];
    char           temp3[32];
    
    eventlog(eventlog_level_debug,__FUNCTION__,"checking gameserver %s for client %s ...",
	     addr_num_to_ip_str(*gsaddr),	// gameserver ip
	     addr_num_to_ip_str(clientaddr));	// client ip

    if (d2gstrans_head)
    {
	LIST_TRAVERSE_CONST(d2gstrans_head,curr)
	{
	    if (!(entry = elem_get_data(curr)))
	    {
		eventlog(eventlog_level_error,"__FUNCTION__","found NULL entry in list");
		continue;
	    }
	    eventlog(eventlog_level_debug,__FUNCTION__,"against entry gameserver %s output %s clientex %s",
		     addr_get_addr_str(entry->server,temp1,sizeof(temp1)),	// gameserver
		     addr_get_addr_str(entry->output,temp2,sizeof(temp2)),	// output
		     netaddr_get_addr_str(entry->exclude,temp3,sizeof(temp3)));	// clientex
	    if (addr_get_ip(entry->server)!=*gsaddr)
	    {
		eventlog(eventlog_level_debug,"__FUNCTION__","entry not for right gameserver");
		continue;
	    }
	    if (netaddr_contains_addr_num(entry->exclude,clientaddr)==1)
	    {
		eventlog(eventlog_level_debug,__FUNCTION__,"client is in the excluded network");
		continue;
	    }
	    *gsaddr = addr_get_ip(entry->output);
	    return;
	}
    }
}
