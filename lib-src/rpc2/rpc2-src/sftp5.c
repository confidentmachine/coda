/* BLURB lgpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-2016 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the  terms of the  GNU  Library General Public Licence  Version 2,  as
shown in the file LICENSE. The technical and financial contributors to
Coda are listed in the file CREDITS.

                        Additional copyrights

#*/

/*
                         IBM COPYRIGHT NOTICE

                          Copyright (C) 1986
             International Business Machines Corporation
                         All Rights Reserved

This  file  contains  some  code identical to or derived from the 1986
version of the Andrew File System ("AFS"), which is owned by  the  IBM
Corporation.   This  code is provided "AS IS" and IBM does not warrant
that it is free of infringement of  any  intellectual  rights  of  any
third  party.    IBM  disclaims  liability of any kind for any damages
whatsoever resulting directly or indirectly from use of this  software
or  of  any  derivative work.  Carnegie Mellon University has obtained
permission to  modify,  distribute and sublicense this code,  which is
based on Version 2  of  AFS  and  does  not  contain  the features and
enhancements that are part of  Version 3 of  AFS.  Version 3 of AFS is
commercially   available   and  supported  by   Transarc  Corporation,
Pittsburgh, PA.

*/

/*
	-- Bit string manipulation routines
	--sftp.h contains some macros too
*/


#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <assert.h>
#include <string.h>
#include "rpc2.private.h"
#include <rpc2/se.h>
#include <rpc2/sftp.h>

/* rightmost bits are ZERO-filled */
void B_ShiftLeft(unsigned int *bMask, int bShift)
{
    /*  The bit string is made up of an integral number of integer parts. (assumption)
	Each integer part is affected by at most two other integer parts, adjacent to each other.
	In each iteration,
	    current:	points to the next integer part of the bit string.
	    first:	points to first integer part affecting current
	(32-shift) low-order bits of *first will become the high order bits of *current
	(shift) high-order bits of *(first+1) will become the low-order bits of *current.
    */
    
    unsigned int shift, *current, *first, *last;
    
    shift = bShift & 31;	/* modulo 32 */
    
    current = bMask;
    first = bMask + (bShift >> 5);
    last = bMask + BITMASKWIDTH - 1;

    while(first < last)
	{
	if(shift == 0)
		*current = *first;
	else
		*current = ((*first) << shift) | ((*(first+1)) >> (32-shift));
	current++;
	first++;
	}
    if (first == last)
	{
	*current = ((*first) << shift);
	current++;
	}
    while (current <= last)
	{
	*current++ = 0;
	}
}


/* leftmost bits are ONE-filled */
#ifdef UNUSED
void B_ShiftRight(unsigned int *bMask, int bShift)
{
    /*  The bit string is made up of an integral number of integer parts. (assumption)
	Each integer part is affected by at most two other integer parts, adjacent to each other.
	In each iteration,
	    current:	points to the next integer part of the bit string.
	    first:	points to first integer part affecting current
	(32-shift) high-order bits of *first will become the low order bits of *current
	(shift) low-order bits of *(first-1) will become the high-order bits of *current.
    */
    unsigned int shift, *current, *first;

    shift = bShift & 31;	/* modulo 32 */
    current = bMask + BITMASKWIDTH - 1;
    first = current - (bShift >> 5);

    while(first > bMask)
	{
	if(shift == 0)
		*current-- = *first;
	else
		*current-- = ((*first) >> shift)| ((*(first-1)) << (32-shift));
	first--;
	}
    if (first == bMask)
	{
	if(shift == 0)
		*current-- = *first;
	else
		*current-- = ((*first) >> shift) | (0xFFFFFFFF << (32-shift));
	}
    while (current >= bMask)
	{
	*current-- = 0xFFFFFFFF;
	}
}
#endif

void B_Assign(unsigned int *dest, unsigned int *src)
{
    memcpy(dest, src, sizeof(int)*BITMASKWIDTH);
}


void B_CopyToPacket(unsigned int *bMask, RPC2_PacketBuffer *whichPacket)
{
    assert(BITMASKWIDTH <= 2);	/* for now */
    whichPacket->Header.BitMask0 = (unsigned) bMask[0];
    whichPacket->Header.BitMask1 = (unsigned) bMask[1];
}

void B_CopyFromPacket(RPC2_PacketBuffer *whichPacket, unsigned int *bMask)
{
    assert(BITMASKWIDTH <= 2);	/* for now */
    bMask[0] = (unsigned) whichPacket->Header.BitMask0;
    bMask[1] = (unsigned) whichPacket->Header.BitMask1;
}

