/* This file is part of Netsukuku
 * (c) Copyright 2004 Andrea Lo Pumo aka AlpT <alpt@freaknet.org>
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Public License as published 
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This source code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Please refer to the GNU Public License for more details.
 *
 * You should have received a copy of the GNU Public License along with
 * this source code; if not, write to:
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * --
 * route.c:
 * Routing table management code.
 */

#include "includes.h"

#include "llist.c"
#include "libnetlink.h"
#include "inet.h"
#include "krnl_route.h"
#include "pkts.h"
#include "map.h"
#include "gmap.h"
#include "bmap.h"
#include "netsukuku.h"
#include "route.h"
#include "xmalloc.h"
#include "log.h"

u_char rt_find_table(ct_route *ctr, u_int dst, u_int gw)
{
	ct_route *i;
	u_char tables[MAX_ROUTE_TABLES];
	int l;
	
	memset(tables, '\0', MAX_ROUTE_TABLES);
	
	for(i=ctr; i; i=i->next) {
		if(i->ct_dst==dst) {
			if(i->ct_gw==gw)
				return i->ct_table;
			else 
				tables[i->ct_table]=1;
		}
	}

	for(l=1; l<MAX_ROUTE_TABLES; l++)
		if(!tables[l])
			return l;

	return 0xff; /*This shouldn't happen!*/
}

void krnl_update_node(void *void_node, u_char level)
{
	map_node *node, *gw_node;
	map_gnode *gnode;
	struct nexthop *nh;
	inet_prefix to;
	int i, node_pos;

	nh=0;
	node=(map_node *)void_node;
	gnode=(map_gnode *)void_node;

	if(!level) {
		nh=xmalloc(sizeof(struct nexthop)*(node->links+1));
		memset(nh, '\0', sizeof(struct nexthop)*(node->links+1));

		maptoip((u_int)me.int_map, (u_int)node, me.cur_quadg.ipstart[1], &to);
		inet_htonl(&to);

		for(i=0; i<node->links; i++) {
#ifdef QMAP_STYLE_I
			maptoip((u_int)me.int_map, (u_int)get_gw_node(node, i),
					me.cur_quadg.ipstart[1], &nh[i].gw);
#else /*QMAP_STYLE_II*/
			maptoip((u_int)me.int_map, (u_int)node->r_node[i].r_node,
					me.cur_quadg.ipstart[1], &nh[i].gw);
#endif
			inet_htonl(&nh[i].gw);
			nh[i].dev=me.cur_dev;
			nh[i].hops=255-i;
		}
		nh[node->links].dev=0;
		node_pos=pos_from_node(node, me.int_map);
	} else {
		nh=xmalloc(sizeof(struct nexthop)*2);
		memset(nh, '\0', sizeof(struct nexthop)*2);
		
		node=&gnode->g;
		node_pos=pos_from_gnode(gnode, me.ext_map[_EL(level)]);
		gnodetoip(me.ext_map, &me.cur_quadg, gnode, level, &to);
		inet_htonl(&to);
		
		gw_node=get_gw_gnode(me.int_map, me.ext_map, me.bnode_map,
				me.bmap_nodes, gnode, level, 0);
		if(!gw_node)
			goto finish;
		
		maptoip((u_int)me.int_map, (u_int)gw_node, 
				me.cur_quadg.ipstart[1], &nh[0].gw);
		inet_htonl(&nh[0].gw);
		nh[0].dev=me.cur_dev;

		nh[1].dev=0;
	}
	
	if(node->flags & MAP_VOID) {
		/*Ok, let's delete it*/
		if(route_del(0, to, nh, me.cur_dev, 0))
			error("WARNING: Cannot delete the route entry for the ",
					"%d %cnode!", node_pos, !level ? ' ' : 'g');
	} else
		if(route_replace(0, to, nh, me.cur_dev, 0))
			error("WARNING: Cannot update the route entry for the "
					"%d %cnode!", node_pos, !level ? ' ' : 'g');
finish:
	if(nh)
		xfree(nh);
}

/* 
 * rt_full_update: It updates _ALL_ the possible routes it can get from _ALL_
 * the maps. If `check_update_flag' is not 0, it will update only the routes of
 * the nodes with the MAP_UPDATE flag set. Note that the MAP_VOID nodes aren't
 * considered.
 */
void rt_full_update(int check_update_flag)
{
	u_short i, l;

	/* Update int_map */
	for(i=0, l=0; i<MAXGROUPNODE; i++) {
		if(me.int_map[i].flags & MAP_VOID || me.int_map[i].flags & MAP_ME)
			continue;

		if(check_update_flag && 
				!((me.int_map[i].flags & MAP_UPDATE) && 
					!(me.int_map[i].flags & MAP_RNODE)))
			continue;

		krnl_update_node(&me.int_map[i], l);
		me.int_map[i].flags&=~MAP_UPDATE;
	}

	/* Update ext_maps */
	for(l=1; l<me.cur_quadg.levels; l++)
		for(i=0; i<MAXGROUPNODE; i++) {
			if(me.ext_map[_EL(l)][i].g.flags & MAP_VOID || 
					me.ext_map[_EL(l)][i].flags & GMAP_VOID)
				continue;

			if(check_update_flag && 
					!(me.ext_map[_EL(l)][i].g.flags & MAP_UPDATE))
				continue;

			krnl_update_node(&me.ext_map[_EL(l)][i].g, l);
			me.ext_map[_EL(l)][i].g.flags&=~MAP_UPDATE;
		}

	route_flush_cache(my_family);
}

int rt_exec_gw(char *dev, inet_prefix to, inet_prefix gw, 
		int (*route_function)(int type, inet_prefix to, struct nexthop *nhops, 
			char *dev, u_char table) )
{
	struct nexthop nh[2];
	
	inet_htonl(&to);

	memset(nh, '\0', sizeof(struct nexthop)*2);	
	memcpy(&nh[0].gw, &gw, sizeof(inet_prefix));
	inet_htonl(&nh[0].gw);
	nh[0].dev=dev;
	nh[1].dev=0;

	return route_function(0, to, nh, dev, 0);
}

int rt_add_gw(char *dev, inet_prefix to, inet_prefix gw)
{
	return rt_exec_gw(dev, to, gw, route_add);
}

int rt_del_gw(char *dev, inet_prefix to, inet_prefix gw)
{
	return rt_exec_gw(dev, to, gw, route_del);
}

int rt_change_gw(char *dev, inet_prefix to, inet_prefix gw)
{
	return rt_exec_gw(dev, to, gw, route_change);
}

int rt_replace_gw(char *dev, inet_prefix to, inet_prefix gw)
{
	return rt_exec_gw(dev, to, gw, route_replace);
}

int rt_replace_def_gw(char *dev, inet_prefix gw)
{
	inet_prefix to;
	
	if(inet_setip_anyaddr(&to, my_family)) {
		error("rt_add_def_gw(): Cannot use INADRR_ANY for the %d family", to.family);
		return -1;
	}
	to.len=0;
	to.bits=0;

	return rt_replace_gw(dev, to, gw);
}

/* 
 * rt_del_loopback_net:
 * We remove the loopback net, leaving only the 127.0.0.1 ip for loopback.
 *  ip route del local 127.0.0.0/8  proto kernel  scope host src 127.0.0.1
 *  ip route del broadcast 127.255.255.255  proto kernel scope link  src 127.0.0.1
 *  ip route del broadcast 127.0.0.0  proto kernel  scope link src 127.0.0.1
 */
int rt_del_loopback_net(void)
{
	inet_prefix to;
	char lo_dev[]="lo";
	u_int idata[4];

	memset(idata, 0, sizeof(int)*4);
	if(my_family!=AF_INET) 
		return 0;

	/*
	 * ip route del broadcast 127.0.0.0  proto kernel  scope link      \
	 * src 127.0.0.1
	 */
	idata[0]=LOOPBACK_NET;
	inet_setip(&to, idata, my_family);
	route_del(RTN_BROADCAST, to, 0, 0, RT_TABLE_LOCAL);
	
	/*
	 * ip route del local 127.0.0.0/8  proto kernel  scope host 	   \
	 * src 127.0.0.1
	 */
	to.bits=8;
	route_del(RTN_LOCAL, to, 0, lo_dev, RT_TABLE_LOCAL);

	/* 
	 * ip route del broadcast 127.255.255.255  proto kernel scope link \
	 * src 127.0.0.1 
	 */
	idata[0]=LOOPBACK_BCAST;
	inet_setip(&to, idata, my_family);
	route_del(RTN_BROADCAST, to, 0, lo_dev, RT_TABLE_LOCAL);

	return 0;
}
