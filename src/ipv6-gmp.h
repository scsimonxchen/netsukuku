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
 * --
 * 128bit-gmp.c: I made this to handle the HUGE ipv6 numbers
 */

#define ZERO128		{0,0,0,0}

int sum_int(unsigned int , unsigned int *);
int sum_128(unsigned int *, unsigned int *);
int sub_int(unsigned int *, unsigned int);
int sub_128(unsigned int *, unsigned int *);
int htonl_128(unsigned int *, unsigned int *, int );
int ntohl_128(unsigned int *, unsigned int *, int );

