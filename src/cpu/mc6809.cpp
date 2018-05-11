/*
*  Module d'�mulation des micro-circuits Motorola MC68xx:
*    - microprocesseur MC6809E
*    - PIA MC6846
*    - PIA MC6821
*
*  Copyright (C) 1996 Sylvain Huet, 1999 Eric Botcazou.
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
*  Module     : mc68xx/mc6809.c
*  Version    : 2.7
*  Cr�� par   : Sylvain Huet 1996
*  Modifi� par: Eric Botcazou 30/11/2000
*
*  Emulateur du microprocesseur Motorola MC6809E
*
*  version 1.0: �mulation fonctionnelle
*  version 2.0: horloge interne, interface
*  version 2.1: interruption du timer
*  version 2.2: nouvelles valeurs de retour des fonctions d'�x�cution
*  version 2.3: encapsulation compl�te du module
*  version 2.4: ajout d'un masque d'�criture des registres
*  version 2.5: ajout d'une fonction trace (mode DEBUG)
*  version 2.6: nouvelles commandes externes (RESET, NMI, FIRQ)
*               correction mineure du mode index� 5-bit
*               Fetch devient FetchInstr et utilise des unsigned char
*               suppression d'un inline inutile
*  version 2.7: nouvelle interface de manipulation de l'�tat du MC6809E
*/
#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif


#include <stdio.h>
#include "mc6809.h"

#include "cpu-debug.h"	// MPO
#include "mamewrap.h"// for CPU INFO

#ifdef WIN32
#pragma warning (disable:4244)	// disable the warning about possible loss of data
#endif

// RJS TEMP
// #include "../io/conout.h"
// static char s[81] = { 0 };
// #define LOGPC(STRIN) if (pc == 64758) { sprintf(s, "PC VALUE 64758 HIT: "STRIN); printline(s); }
#define LOGPC(STRIN)

INT_MC taille[] =
{ 2,2,1,2,2,1,2,2,2,2,2,1,2,2,2,2
,0,0,1,1,1,1,3,3,1,1,2,1,2,1,2,2
,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
,2,2,2,2,2,2,2,2,1,1,1,1,2,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,2,1,1,2,2,1,2,2,2,2,2,1,2,2,2,2
,3,1,1,3,3,1,3,3,3,3,3,1,3,3,3,3
,2,2,2,3,2,2,2,1,2,2,2,2,3,2,3,1
,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
,2,2,2,3,2,2,2,1,2,2,2,2,3,1,3,1
,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3

,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,4,1,1,1,1,1,1,1,1,4,1,4,1
,1,1,1,3,1,1,1,1,1,1,1,1,3,1,3,3
,1,1,1,3,1,1,1,1,1,1,1,1,3,1,3,3
,1,1,1,4,1,1,1,1,1,1,1,1,4,1,4,4
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,4,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,4,4

,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,4,1,1,1,1,1,1,1,1,4,1,1,1
,1,1,1,3,1,1,1,1,1,1,1,1,3,1,1,1
,1,1,1,3,1,1,1,1,1,1,1,1,3,1,1,1
,1,1,1,4,1,1,1,1,1,1,1,1,4,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};


INT_MC adr[] =
{ 0,1,1,0,0,1,0,0,0,0,0,1,0,0,0,0
,6,6,1,1,1,1,2,2,1,1,3,1,3,1,1,1
,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
,4,4,4,4,3,3,3,3,1,1,1,1,3,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
,3,3,3,3,3,3,3,3,3,3,3,3,3,2,3,1
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
,3,3,3,3,3,3,3,1,3,3,3,3,3,1,3,1
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5

,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,3,1,1,1,1,1,1,1,1,3,1,3,1
,1,1,1,0,1,1,1,1,1,1,1,1,0,1,0,0
,1,1,1,4,1,1,1,1,1,1,1,1,4,1,4,4
,1,1,1,5,1,1,1,1,1,1,1,1,5,1,5,5
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,4,4
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,5,5

,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,3,1,1,1,1,1,1,1,1,3,1,1,1
,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1
,1,1,1,4,1,1,1,1,1,1,1,1,4,1,1,1
,1,1,1,5,1,1,1,1,1,1,1,1,5,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};


INT_MC cpu_cycles[] =
{ 6,2,0,6,6,0,6,6,6,6,6,0,6,6,3,6
,0,0,2,4,0,0,5,9,0,2,3,0,3,2,8,6
,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
,4,4,4,4,5,5,5,5,0,5,3,3,20,11,0,7
,2,0,0,2,2,0,2,2,2,2,2,0,2,2,0,2
,2,0,0,2,2,0,2,2,2,2,2,0,2,2,0,2
,6,0,0,6,6,0,6,6,6,6,6,0,6,6,3,6
,7,0,0,7,7,0,7,7,7,7,7,0,7,7,4,7
,2,2,2,4,2,2,2,0,2,2,2,2,4,7,3,0
,4,4,4,6,4,4,4,4,4,4,4,4,6,7,5,5
,4,4,4,6,4,4,4,4,4,4,4,4,6,7,5,5
,5,5,5,7,5,5,5,5,5,5,5,5,7,8,6,6
,2,2,2,4,2,2,2,0,2,2,2,2,3,0,3,0
,4,4,4,6,4,4,4,4,4,4,4,4,5,5,5,5
,4,4,4,6,4,4,4,4,4,4,4,4,5,5,5,5
,5,5,5,7,5,5,5,5,5,5,5,5,6,6,6,6

,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,8
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,5,0,0,0,0,0,0,0,0,5,0,4,0
,0,0,0,7,0,0,0,0,0,0,0,0,7,0,6,6
,0,0,0,7,0,0,0,0,0,0,0,0,7,0,6,6
,0,0,0,8,0,0,0,0,0,0,0,0,8,0,7,7
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,6
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,6
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,7

,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,8
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,5,0,0,0,0,0,0,0,0,5,0,0,0
,0,0,0,7,0,0,0,0,0,0,0,0,7,0,0,0
,0,0,0,7,0,0,0,0,0,0,0,0,7,0,0,0
,0,0,0,8,0,0,0,0,0,0,0,0,8,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void(*FetchInstr)(INT_MC, UCHAR_MC[]);
static INT_MC(*LoadByte)(INT_MC);
static INT_MC(*LoadWord)(INT_MC);
static void(*StoreByte)(INT_MC, INT_MC);
static void(*StoreWord)(INT_MC, INT_MC);

static INT_MC(*TrapCallback)(struct MC6809_REGS *);
static void(*TimerCallback)(void *);
static void *timer_data;

static UCHAR_MC fetch_buffer[MC6809_FETCH_BUFFER_SIZE];

/* le caract�re 8-bit du MC6809 impose l'utilisation de char
pour la manipulation des opcodes qui sont des octets sign�s */
static CHAR_MC *op;
static INT_MC ad;
static INT_MC *regist[4], *exreg[16];
static INT_MC illegal_instruction_flag;

/* variables d'�tat du MC6809 */
static mc6809_clock_t cpu_clock, cpu_timer;
static INT_MC pc, xr, yr, ur, sr, ar, br, dp;
static INT_MC res, m1, m2, sign, ovfl, h1, h2, ccrest;


/*************************************************/
/*** gestion du registre d'�tat (CC) du MC6809 ***/
/*************************************************/

static INT_MC getcc()
{
	return  ((((h1 & 15) + (h2 & 15)) & 16) << 1)
		| ((sign & 0x80) >> 4)
		| ((((res & 0xff) == 0) & 1) << 2)
		| ((((~(m1^m2))&(m1^ovfl)) & 0x80) >> 6)
		| ((res & 0x100) >> 8)
		| ccrest;
}


static void setcc(INT_MC i)
{
	m1 = m2 = 0;
	res = ((i & 1) << 8) | (4 - (i & 4));
	ovfl = (i & 2) << 6;
	sign = (i & 8) << 4;
	h1 = h2 = (i & 32) >> 2;
	ccrest = i & 0xd0;
}


/***********************************/
/*** modes d'adressage du MC6809 ***/
/***********************************/

static INT_MC direc()
{
	return (dp << 8) + ((*op) & 255);
}

static INT_MC inher()
{
	return -1;
}

static INT_MC immedc()
{
	return (pc - 1) & 0xffff;
}

static INT_MC immedl()
{
	return (pc - 2) & 0xffff;
}

static INT_MC indxp()
{
	INT_MC    *x = regist[((*op) & 0x60) >> 5];
	INT_MC    k;
	k = *x;
	*x = ((*x) + 1) & 0xffff;
	cpu_clock += 2;
	return k;
}

static INT_MC indxpp()
{
	INT_MC    *x = regist[((*op) & 0x60) >> 5];
	INT_MC    k;
	k = *x;
	*x = ((*x) + 2) & 0xffff;
	cpu_clock += 3;
	return k;
}

static INT_MC indmx()
{
	INT_MC    *x = regist[((*op) & 0x60) >> 5];
	*x = ((*x) - 1) & 0xffff;
	cpu_clock += 2;
	return *x;
}

static INT_MC indmmx()
{
	INT_MC    *x = regist[((*op) & 0x60) >> 5];
	*x = ((*x) - 2) & 0xffff;
	cpu_clock += 3;
	return *x;
}

static INT_MC indx()
{
	return *(regist[((*op) & 0x60) >> 5]);
}

static INT_MC indax()
{
	CHAR_MC    a = ar;
	cpu_clock++;
	return ((*(regist[((*op) & 0x60) >> 5])) + a) & 0xffff;
}

static INT_MC indbx()
{
	CHAR_MC    b = br;
	cpu_clock++;
	return ((*(regist[((*op) & 0x60) >> 5])) + b) & 0xffff;
}

static INT_MC inder()
{
	return 0;
}

static INT_MC ind1x()
{
	CHAR_MC    del = op[1];
	pc++; pc &= 0xffff;
	LOGPC("in ind1x");
	cpu_clock++;
	return ((*(regist[((*op) & 0x60) >> 5])) + del) & 0xffff;
}

static INT_MC ind2x()
{
	INT_MC     del = ((op[1] & 255) << 8) + (op[2] & 255);
	pc += 2; pc &= 0xffff;
	LOGPC("in ind2x");
	cpu_clock += 4;
	return ((*(regist[((*op) & 0x60) >> 5])) + del) & 0xffff;
}

static INT_MC inddx()
{
	INT_MC     del = (ar << 8) + br;
	cpu_clock += 4;
	return ((*(regist[((*op) & 0x60) >> 5])) + del) & 0xffff;
}

static INT_MC ind1p()
{
	CHAR_MC    del = op[1];
	pc++; pc &= 0xffff;
	LOGPC("in ind1p");
	cpu_clock++;
	return (pc + del) & 0xffff;
}

static INT_MC ind2p()
{
	INT_MC     del = ((op[1] & 255) << 8) + (op[2] & 255);
	pc += 2; pc &= 0xffff;
	LOGPC("in ind2p");
	cpu_clock += 5;
	return (pc + del) & 0xffff;
}

static INT_MC indad()
{
	pc += 2; pc &= 0xffff;
	LOGPC("in indad");
	cpu_clock += 2;
	return ((op[1] & 255) << 8) + (op[2] & 255);
}

static INT_MC(*indmod[])() =
{ indxp,indxpp,indmx,indmmx,indx,indbx,indax,inder
,ind1x,ind2x,inder,inddx,ind1p,ind2p,inder,indad
};


static INT_MC indir()
{
	INT_MC k;

	if ((*op) & 0x80)
	{
		k = (*indmod[(*op) & 0xf])();

		if ((*op) & 0x10)
		{
			cpu_clock += 3;  /* p�nalit� de 3 cycles pour le mode indirect */
			return LoadWord(k);
		} else
			return k; /* non indirect */
	}

	/* 5-bit offset */
	cpu_clock++;

	if ((*op) & 0x10)
	{
		if ((*op) & 0xf)
			k = (*(regist[((*op) & 0x60) >> 5])) - ((-*op) & 0xf);
		else
			k = (*(regist[((*op) & 0x60) >> 5])) - 16;
	} else
		k = (*(regist[((*op) & 0x60) >> 5])) + ((*op) & 0xf);

	return k & 0xffff;
}

static INT_MC etend()
{
	return (((*op) & 255) << 8) + (op[1] & 255);
}

static INT_MC(*adresc[])() =
{ direc,inher,inher,immedc,indir,etend,inher };

static INT_MC(*adresl[])() =
{ direc,inher,inher,immedl,indir,etend,inher };


/******************************/
/*** instructions du MC6809 ***/
/******************************/

static void what()
{
	illegal_instruction_flag = 1;
}

static void negm()             /* H?NxZxVxCx */
{
	INT_MC    k;
	INT_MC     val;
	val = LoadByte(k = (*adresc[ad])());
	m1 = val; m2 = -val;                /* bit V */
	StoreByte(k, (val = -val) & 255);
	ovfl = res = sign = val;
}

static void comm()             /* NxZxV0C1 */
{
	INT_MC    k;
	INT_MC     val;
	val = LoadByte(k = (*adresc[ad])());
	m1 = ~m2;
	StoreByte(k, val = (~val) & 255);
	sign = val;
	res = sign | 0x100; /* bit C a 1 */
}

static void lsrm()             /* N0ZxCx */
{
	INT_MC    k;
	INT_MC     val;
	val = LoadByte(k = (*adresc[ad])());
	res = (val & 1) << 8; /* bit C */
	StoreByte(k, val >>= 1);
	sign = 0;
	res |= val;
}

static void rorm()             /* NxZxCx */
{
	INT_MC    k;
	INT_MC     val, i;
	i = val = LoadByte(k = (*adresc[ad])());
	StoreByte(k, val = (val | (res & 0x100)) >> 1);
	sign = val;
	res = ((i & 1) << 8) | sign;
}

static void asrm()             /* H?NxZxCx */
{
	INT_MC    k;
	INT_MC     val;
	val = LoadByte(k = (*adresc[ad])());
	res = (val & 1) << 8;
	StoreByte(k, val = (val >> 1) | (val & 0x80));
	sign = val;
	res |= sign;
}

static void aslm()             /* H?NxZxVxCx */
{
	INT_MC    k;
	INT_MC     val;
	val = LoadByte(k = (*adresc[ad])());
	m1 = m2 = val;
	StoreByte(k, val <<= 1);
	ovfl = sign = res = val;
}

static void rolm()             /* NxZxVxCx */
{
	INT_MC    k;
	INT_MC     val, i;
	i = val = LoadByte(k = (*adresc[ad])());
	m1 = m2 = val;
	StoreByte(k, val = (val << 1) | ((res & 0x100) >> 8));
	ovfl = sign = res = val;
}

static void decm()             /* NxZxVx */
{
	INT_MC    k;
	INT_MC     val;
	val = LoadByte(k = (*adresc[ad])());
	m1 = val; m2 = 0x80;
	StoreByte(k, --val);
	ovfl = sign = val & 255;
	res = (res & 0x100) | sign;
}

static void incm()             /* NxZxVx */
{
	INT_MC    k;
	INT_MC     val;
	val = LoadByte(k = (*adresc[ad])());
	m1 = val; m2 = 0;
	StoreByte(k, ++val);
	ovfl = sign = val & 255;
	res = (res & 0x100) | sign;
}

static void tstm()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ~m2;
	sign = val;
	res = (res & 0x100) | sign;
}

static void jmpm()
{
	pc = (*adresl[ad])();
	LOGPC("in jmpm");
}

static void jsrm()
{
	INT_MC k;
	k = (*adresl[ad])();
	sr = (sr - 2) & 0xffff;
	StoreWord(sr, pc);
	pc = k;
	LOGPC("in jsrm");
}

static void clrm()     /* N0Z1V0C0 */
{
	StoreByte((*adresc[ad])(), 0);
	m1 = ~m2;
	sign = res = 0;
}

static void nopm()
{
}

static void synm()
{
	/* non support� */
}

static void lbra()
{
	pc = (pc + ((*op) << 8) + (op[1] & 255)) & 0xffff;
	LOGPC("in lbra");
}

static void lbsr()
{
	sr = (sr - 2) & 0xffff;
	StoreWord(sr, pc);
	pc = (pc + ((*op) << 8) + (op[1] & 255)) & 0xffff;
	LOGPC("in lbsr");
}

static void daam()     /* NxZxV?Cx */
{
	INT_MC     i = ar + (res & 0x100);
	if (((ar & 15)>9) || ((h1 & 15) + (h2 & 15)>15)) i += 6;
	if (i>0x99) i += 0x60;
	res = sign = i;
	ar = i & 255;
}

static void orcc()
{
	setcc(getcc() | (*op));
}

static void andc()
{
	setcc(getcc()&(*op));
}

static void sexm()     /* NxZx */
{
	if (br & 0x80) ar = 0xff;
	else ar = 0;
	sign = br;
	res = (res & 0x100) | sign;
}


static void exgm()
{
	INT_MC    k, l;
	INT_MC     o1, o2;
	INT_MC    *p, *q;

	o1 = ((*op) & 0xf0) >> 4;
	o2 = (*op) & 15;
	if ((p = exreg[o1])) k = *p;
	else    if (o1) k = getcc();
	else k = (ar << 8) + br;
	if ((q = exreg[o2]))
	{
		l = *q;
		*q = k;
	} else    if (o2) {
		l = getcc();
		setcc(k);
	} else
	{
		l = (ar << 8) + br;
		ar = (k >> 8) & 255;
		br = k & 255;
	}
	if (p) *p = l;
	else    if (o1) setcc(l);
	else
	{
		ar = (l >> 8) & 255;
		br = l & 255;
	}
}

static void tfrm()
{
	INT_MC    k;
	INT_MC     o1, o2;
	INT_MC    *p, *q;

	o1 = ((*op) & 0xf0) >> 4;
	o2 = (*op) & 15;
	if ((p = exreg[o1])) k = *p;
	else    if (o1) k = getcc();
	else k = (ar << 8) + br;
	if ((q = exreg[o2])) *q = k;
	else    if (o2) setcc(k);
	else
	{
		ar = (k >> 8) & 255;
		br = k & 255;
	}
}

static void bras()
{
	pc += op[0];
	LOGPC("in bras");
}
static void brns()
{
}

static void bhis()     /* c|z=0 */
{
	if ((!(res & 0x100)) && (res & 0xff)) pc += op[0];
	LOGPC("in bhis");
}
static void blss()     /* c|z=1 */
{
	if ((res & 0x100) || (!(res & 0xff))) pc += op[0];
	LOGPC("in blss");
}

static void bccs()     /* c=0 */
{
	if (!(res & 0x100)) pc += op[0];
	LOGPC("in bccs");
}
static void blos()     /* c=1 */
{
	if (res & 0x100) pc += op[0];
	LOGPC("in blos");
}

static void bnes()     /* z=0 */
{
	if (res & 0xff) pc += op[0];
	LOGPC("in bnes");
}
static void beqs()     /* z=1 */
{
	if (!(res & 0xff)) pc += op[0];
	LOGPC("in beqs");
}

static void bvcs()     /* v=0 */
{
	if (((m1^m2) & 0x80) || (!((m1^ovfl) & 0x80))) pc += op[0];
	LOGPC("in bvcs");
}
static void bvss()     /* v=1 */
{
	if ((!((m1^m2) & 0x80)) && ((m1^ovfl) & 0x80)) pc += op[0];
	LOGPC("in bvss");
}

static void bpls()     /* n=0 */
{
	if (!(sign & 0x80)) pc += op[0];
	LOGPC("in bpls");
}
static void bmis()     /* n=1 */
{
	if (sign & 0x80) pc += op[0];
	LOGPC("in bmis");
}

static void bges()     /* n^v=0 */
{
	if (!((sign ^ ((~(m1^m2))&(m1^ovfl))) & 0x80)) pc += op[0];
	LOGPC("in bges");
}
static void blts()     /* n^v=1 */
{
	if ((sign ^ ((~(m1^m2))&(m1^ovfl))) & 0x80) pc += op[0];
	LOGPC("in blts");
}

static void bgts()     /* z|(n^v)=0 */
{
	if ((res & 0xff)
		&& (!((sign ^ ((~(m1^m2))&(m1^ovfl))) & 0x80))) pc += op[0];
	LOGPC("in bgts");
}
static void bles()     /* z|(n^v)=1 */
{
	if ((!(res & 0xff))
		|| ((sign ^ ((~(m1^m2))&(m1^ovfl))) & 0x80)) pc += op[0];
	LOGPC("in bles");
}


static void leax()     /* Zx */
{
	xr = (*adresc[ad])();
	res = (res & 0x100) | ((xr | (xr >> 8)) & 255);
}

static void leay()     /* Zx */
{
	yr = (*adresc[ad])();
	res = (res & 0x100) | ((yr | (yr >> 8)) & 255);
}

static void leas()
{
	sr = (*adresc[ad])();
}

static void leau()
{
	ur = (*adresc[ad])();
}

static void pshsr(INT_MC i)
{
	if (i & 0x80) {
		sr = (sr - 2) & 0xffff;
		StoreWord(sr, pc);
		cpu_clock += 2;
	}
	if (i & 0x40) {
		sr = (sr - 2) & 0xffff;
		StoreWord(sr, ur);
		cpu_clock += 2;
	}
	if (i & 0x20) {
		sr = (sr - 2) & 0xffff;
		StoreWord(sr, yr);
		cpu_clock += 2;
	}
	if (i & 0x10) {
		sr = (sr - 2) & 0xffff;
		StoreWord(sr, xr);
		cpu_clock += 2;
	}
	if (i & 0x8) {
		sr = (sr - 1) & 0xffff;
		StoreByte(sr, dp);
		cpu_clock++;
	}
	if (i & 0x4) {
		sr = (sr - 1) & 0xffff;
		StoreByte(sr, br);
		cpu_clock++;
	}
	if (i & 0x2) {
		sr = (sr - 1) & 0xffff;
		StoreByte(sr, ar);
		cpu_clock++;
	}
	if (i & 0x1) {
		sr = (sr - 1) & 0xffff;
		StoreByte(sr, getcc());
		cpu_clock++;
	}
}

static void pshs()
{
	pshsr(*op);
}

static void pulsr(INT_MC i)
{
	if (i & 0x1) {
		setcc(LoadByte(sr));
		sr = (sr + 1) & 0xffff;
		cpu_clock++;
	}
	if (i & 0x2) {
		ar = LoadByte(sr);
		sr = (sr + 1) & 0xffff;
		cpu_clock++;
	}
	if (i & 0x4) {
		br = LoadByte(sr);
		sr = (sr + 1) & 0xffff;
		cpu_clock++;
	}
	if (i & 0x8) {
		dp = LoadByte(sr);
		sr = (sr + 1) & 0xffff;
		cpu_clock++;
	}
	if (i & 0x10) {
		xr = LoadWord(sr);
		sr = (sr + 2) & 0xffff;
		cpu_clock += 2;
	}
	if (i & 0x20) {
		yr = LoadWord(sr);
		sr = (sr + 2) & 0xffff;
		cpu_clock += 2;
	}
	if (i & 0x40) {
		ur = LoadWord(sr);
		sr = (sr + 2) & 0xffff;
		cpu_clock += 2;
	}
	if (i & 0x80) {
		pc = LoadWord(sr);
		LOGPC("in pulsr");
		sr = (sr + 2) & 0xffff;
		cpu_clock += 2;
	}
}

static void puls()
{
	pulsr(*op);
}

static void pshu()
{
	INT_MC     i = *op;

	if (i & 0x80) {
		ur = (ur - 2) & 0xffff;
		StoreWord(ur, pc);
		cpu_clock += 2;
	}
	if (i & 0x40) {
		ur = (ur - 2) & 0xffff;
		StoreWord(ur, sr);
		cpu_clock += 2;
	}
	if (i & 0x20) {
		ur = (ur - 2) & 0xffff;
		StoreWord(ur, yr);
		cpu_clock += 2;
	}
	if (i & 0x10) {
		ur = (ur - 2) & 0xffff;
		StoreWord(ur, xr);
		cpu_clock += 2;
	}
	if (i & 0x8) {
		ur = (ur - 1) & 0xffff;
		StoreByte(ur, dp);
		cpu_clock++;
	}
	if (i & 0x4) {
		ur = (ur - 1) & 0xffff;
		StoreByte(ur, br);
		cpu_clock++;
	}
	if (i & 0x2) {
		ur = (ur - 1) & 0xffff;
		StoreByte(ur, ar);
		cpu_clock++;
	}
	if (i & 0x1) {
		ur = (ur - 1) & 0xffff;
		StoreByte(ur, getcc());
		cpu_clock++;
	}
}

static void pulu()
{
	INT_MC     i = *op;

	if (i & 0x1) {
		setcc(LoadByte(ur));
		ur = (ur + 1) & 0xffff;
		cpu_clock++;
	}
	if (i & 0x2) {
		ar = LoadByte(ur);
		ur = (ur + 1) & 0xffff;
		cpu_clock++;
	}
	if (i & 0x4) {
		br = LoadByte(ur);
		ur = (ur + 1) & 0xffff;
		cpu_clock++;
	}
	if (i & 0x8) {
		dp = LoadByte(ur);
		ur = (ur + 1) & 0xffff;
		cpu_clock++;
	}
	if (i & 0x10) {
		xr = LoadWord(ur);
		ur = (ur + 2) & 0xffff;
		cpu_clock += 2;
	}
	if (i & 0x20) {
		yr = LoadWord(ur);
		ur = (ur + 2) & 0xffff;
		cpu_clock += 2;
	}
	if (i & 0x40) {
		sr = LoadWord(ur);
		ur = (ur + 2) & 0xffff;
		cpu_clock += 2;
	}
	if (i & 0x80) {
		pc = LoadWord(ur);
		LOGPC("in pulu");
		ur = (ur + 2) & 0xffff;
		cpu_clock += 2;
	}
}

static void rtsm()
{
	pc = LoadWord(sr);
	LOGPC("in rtsmr");
	sr = (sr + 2) & 0xffff;
}

static void abxm()
{
	xr = (xr + br) & 0xffff;
}

static void rtim()
{
	pulsr(1);
	if (ccrest & 0x80) pulsr(0xfe);
	else    pulsr(0x80);
}

static void cwai()
{
	/* non support� */
}

static void mulm()             /* ZxCx */
{
	INT_MC    k;
	k = ar*br;
	ar = (k >> 8) & 255;
	br = k & 255;
	res = ((br & 0x80) << 1) | ((k | (k >> 8)) & 255);  /* c=bit7 de br */
}

static void swim()
{
	ccrest |= 0x80;
	pshsr(0xff);
	ccrest |= 0x50;
	pc = LoadWord(0xFFFA);
	LOGPC("in swim");
}

static void nega()             /* H?NxZxVxCx */
{
	m1 = ar; m2 = -ar;          /* bit V */
	ar = -ar;
	ovfl = res = sign = ar;
	ar &= 255;
}

static void coma()             /* NxZxV0C1 */
{
	m1 = ovfl;
	ar = (~ar) & 255;
	sign = ar;
	res = sign | 0x100; /* bit C a 1 */
}

static void lsra()             /* N0ZxCx */
{
	res = (ar & 1) << 8;  /* bit C */
	ar >>= 1;
	sign = 0;
	res |= ar;
}

static void rora()             /* NxZxCx */
{
	INT_MC     i;
	i = ar;
	ar = (ar | (res & 0x100)) >> 1;
	sign = ar;
	res = ((i & 1) << 8) | sign;
}

static void asra()             /* H?NxZxCx */
{
	res = (ar & 1) << 8;
	ar = (ar >> 1) | (ar & 0x80);
	sign = ar;
	res |= sign;
}

static void asla()             /* H?NxZxVxCx */
{
	m1 = m2 = ar;
	ar <<= 1;
	ovfl = sign = res = ar;
	ar &= 255;
}

static void rola()             /* NxZxVxCx */
{
	INT_MC     i;
	i = ar;
	m1 = m2 = ar;
	ar = (ar << 1) | ((res & 0x100) >> 8);
	ovfl = sign = res = ar;
	ar &= 255;
}

static void deca()             /* NxZxVx */
{
	m1 = ar; m2 = 0x80;
	ar = (ar - 1) & 255;
	ovfl = sign = ar;
	res = (res & 0x100) | sign;
}

static void inca()             /* NxZxVx */
{
	m1 = ar; m2 = 0;
	ar = (ar + 1) & 255;
	ovfl = sign = ar;
	res = (res & 0x100) | sign;
}

static void tsta()             /* NxZxV0 */
{
	m1 = ovfl;
	sign = ar;
	res = (res & 0x100) | sign;
}

static void clra()             /* N0Z1V0C0 */
{
	ar = 0;
	m1 = ovfl;
	sign = res = 0;
}

static void negb()             /* H?NxZxVxCx */
{
	m1 = br; m2 = -br;          /* bit V */
	br = -br;
	ovfl = res = sign = br;
	br &= 255;
}

static void comb()             /* NxZxV0C1 */
{
	m1 = ovfl;
	br = (~br) & 255;
	sign = br;
	res = sign | 0x100; /* bit C a 1 */
}

static void lsrb()             /* N0ZxCx */
{
	res = (br & 1) << 8;  /* bit C */
	br >>= 1;
	sign = 0;
	res |= br;
}

static void rorb()             /* NxZxCx */
{
	INT_MC     i;
	i = br;
	br = (br | (res & 0x100)) >> 1;
	sign = br;
	res = ((i & 1) << 8) | sign;
}

static void asrb()             /* H?NxZxCx */
{
	res = (br & 1) << 8;
	br = (br >> 1) | (br & 0x80);
	sign = br;
	res |= sign;
}

static void aslb()             /* H?NxZxVxCx */
{
	m1 = m2 = br;
	br <<= 1;
	ovfl = sign = res = br;
	br &= 255;
}

static void rolb()             /* NxZxVxCx */
{
	INT_MC     i;
	i = br;
	m1 = m2 = br;
	br = (br << 1) | ((res & 0x100) >> 8);
	ovfl = sign = res = br;
	br &= 255;
}

static void decb()             /* NxZxVx */
{
	m1 = br; m2 = 0x80;
	br = (br - 1) & 255;
	ovfl = sign = br;
	res = (res & 0x100) | sign;
}

static void incb()             /* NxZxVx */
{
	m1 = br; m2 = 0;
	br = (br + 1) & 255;
	ovfl = sign = br;
	res = (res & 0x100) | sign;
}

static void tstb()             /* NxZxV0 */
{
	m1 = ovfl;
	sign = br;
	res = (res & 0x100) | sign;
}

static void clrb()             /* N0Z1V0C0 */
{
	br = 0;
	m1 = ovfl;
	sign = res = 0;
}

static void suba()             /* H?NxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ar; m2 = -val;
	ar -= val;
	ovfl = res = sign = ar;
	ar &= 255;
}

static void cmpa()             /* H?NxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ar; m2 = -val;
	ovfl = res = sign = ar - val;
}

static void sbca()             /* H?NxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ar; m2 = -val;
	ar -= val + ((res & 0x100) >> 8);
	ovfl = res = sign = ar;
	ar &= 255;
}

static void subd()             /* NxZxVxCx */
{
	INT_MC    dr, val;
	val = LoadWord((*adresl[ad])());
	m1 = ar; m2 = (-val) >> 8;
	dr = (ar << 8) + br - val;
	ar = dr >> 8;
	br = dr & 255;
	ovfl = res = sign = ar;
	res |= br;
	ar &= 255;
}

static void anda()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ovfl;
	ar &= val;
	sign = ar;
	res = (res & 0x100) | sign;
}

static void bita()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ovfl;
	sign = ar&val;
	res = (res & 0x100) | sign;
}

static void ldam()             /* NxZxV0 */
{
	sign = ar = LoadByte((*adresc[ad])());
	m1 = ovfl;
	res = (res & 0x100) | sign;
}

static void stam()             /* NxZxV0 */
{
	StoreByte((*adresc[ad])(), ar);
	sign = ar;
	m1 = ovfl;
	res = (res & 0x100) | sign;
}

static void eora()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ovfl;
	ar ^= val;
	sign = ar;
	res = (res & 0x100) | sign;
}

static void adca()             /* HxNxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = h1 = ar; m2 = val;
	h2 = val + ((res & 0x100) >> 8);
	ar += h2;
	ovfl = res = sign = ar;
	ar &= 255;
}

static void oram()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ovfl;
	ar |= val;
	sign = ar;
	res = (res & 0x100) | sign;
}

static void adda()             /* HxNxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = h1 = ar; m2 = h2 = val;
	ar += val;
	ovfl = res = sign = ar;
	ar &= 255;
}

static void cmpx()             /* NxZxVxCx */
{
	INT_MC    val;
	val = LoadWord((*adresl[ad])());
	m1 = xr >> 8; m2 = (-val) >> 8;
	ovfl = res = sign = (xr - val) >> 8;
	res |= (xr - val) & 255;
}

static void bsrm()
{
	sr = (sr - 2) & 0xffff;
	StoreWord(sr, pc);
	pc = (pc + op[0]) & 0xffff;
	LOGPC("in bsrm");
}

static void ldxm()     /* NxZxV0 */
{
	xr = LoadWord((*adresl[ad])());
	m1 = ovfl;
	sign = xr >> 8;
	res = (res & 0x100) | ((sign | xr) & 255);
}

static void stxm()             /* NxZxV0 */
{
	StoreWord((*adresl[ad])(), xr);
	m1 = 0; m2 = 0x80;
	sign = xr >> 8;
	res = (res & 0x100) | ((sign | xr) & 255);
}

static void subb()             /* H?NxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = br; m2 = -val;
	br -= val;
	ovfl = res = sign = br;
	br &= 255;
}

static void cmpb()             /* H?NxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = br; m2 = -val;
	ovfl = res = sign = br - val;
}

static void sbcb()             /* H?NxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = br; m2 = -val;
	br -= val + ((res & 0x100) >> 8);
	ovfl = res = sign = br;
	br &= 255;
}

static void addd()             /* NxZxVxCx */
{
	INT_MC    dr, val;
	val = LoadWord((*adresl[ad])());
	m1 = ar; m2 = val >> 8;
	dr = (ar << 8) + br + val;
	ar = dr >> 8;
	br = dr & 255;
	ovfl = res = sign = ar;
	res |= br;
	ar &= 255;
}

static void andb()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ovfl;
	br &= val;
	sign = br;
	res = (res & 0x100) | sign;
}

static void bitb()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ovfl;
	sign = br&val;
	res = (res & 0x100) | sign;
}

static void ldbm()             /* NxZxV0 */
{
	sign = br = LoadByte((*adresc[ad])());
	m1 = ovfl;
	res = (res & 0x100) | sign;
}

static void stbm()             /* NxZxV0 */
{
	StoreByte((*adresc[ad])(), br);
	sign = br;
	m1 = ovfl;
	res = (res & 0x100) | sign;
}

static void eorb()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ovfl;
	br ^= val;
	sign = br;
	res = (res & 0x100) | sign;
}

static void adcb()             /* HxNxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = h1 = br; m2 = val;
	h2 = val + ((res & 0x100) >> 8);
	br += h2;
	ovfl = res = sign = br;
	br &= 255;
}

static void orbm()             /* NxZxV0 */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = ovfl;
	br |= val;
	sign = br;
	res = (res & 0x100) | sign;
}

static void addb()             /* HxNxZxVxCx */
{
	INT_MC     val;
	val = LoadByte((*adresc[ad])());
	m1 = h1 = br; m2 = h2 = val;
	br += val;
	ovfl = res = sign = br;
	br &= 255;
}

static void lddm()             /* NxZxV0 */
{
	INT_MC    dr;
	dr = LoadWord((*adresl[ad])());
	m1 = ovfl;
	ar = (dr >> 8) & 255;
	br = dr & 255;
	sign = ar;
	res = (res & 0x100) | br | ar;
}

static void stdm()             /* NxZxV0 */
{
	StoreWord((*adresl[ad])(), (ar << 8) + br);
	m1 = ovfl;
	sign = ar;
	res = (res & 0x100) | ar | br;
}

static void ldum()     /* NxZxV0 */
{
	ur = LoadWord((*adresl[ad])());
	m1 = ovfl;
	sign = ur >> 8;
	res = (res & 0x100) | ((sign | ur) & 255);
}

static void stum()             /* NxZxV0 */
{
	StoreWord((*adresl[ad])(), ur);
	m1 = ovfl;
	sign = ur >> 8;
	res = (res & 0x100) | ((sign | ur) & 255);
}

static void lbrn()
{
}

static void lbhi()     /* c|z=0 */
{
	if ((!(res & 0x100)) && (res & 0xff))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbhi");
		cpu_clock++;
	}
}

static void lbls()     /* c|z=1 */
{
	if ((res & 0x100) || (!(res & 0xff)))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbls");
		cpu_clock++;
	}
}

static void lbcc()     /* c=0 */
{
	if (!(res & 0x100))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbcc");
		cpu_clock++;
	}
}

static void lblo()     /* c=1 */
{
	if (res & 0x100)
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lblo");
		cpu_clock++;
	}
}

static void lbne()     /* z=0 */
{
	if (res & 0xff)
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbne");
		cpu_clock++;
	}
}

static void lbeq()     /* z=1 */
{
	if (!(res & 0xff))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbeq");
		cpu_clock++;
	}
}

static void lbvc()     /* v=0 */
{
	if (((m1^m2) & 0x80) || (!((m1^ovfl) & 0x80)))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbvc");
		cpu_clock++;
	}
}

static void lbvs()     /* v=1 */
{
	if ((!((m1^m2) & 0x80)) && ((m1^ovfl) & 0x80))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbvs");
		cpu_clock++;
	}
}

static void lbpl()     /* n=0 */
{
	if (!(sign & 0x80))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbpl");
		cpu_clock++;
	}
}

static void lbmi()     /* n=1 */
{
	if (sign & 0x80)
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbmi");
		cpu_clock++;
	}
}

static void lbge()     /* n^v=0 */
{
	if (!((sign ^ ((~(m1^m2))&(m1^ovfl))) & 0x80))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbge");
		cpu_clock++;
	}
}

static void lblt()     /* n^v=1 */
{
	if ((sign ^ ((~(m1^m2))&(m1^ovfl))) & 0x80)
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lblt");
		cpu_clock++;
	}
}

static void lbgt()     /* z|(n^v)=0 */
{
	if ((res & 0xff) && (!((sign ^ ((~(m1^m2))&(m1^ovfl))) & 0x80)))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lbgt");
		cpu_clock++;
	}
}

static void lble()     /* z|(n^v)=1 */
{
	if ((!(res & 0xff)) || ((sign ^ ((~(m1^m2))&(m1^ovfl))) & 0x80))
	{
		pc = (pc + (op[0] << 8) + (op[1] & 255)) & 0xffff;
		LOGPC("in lble");
		cpu_clock++;
	}
}

static void swi2()
{
	ccrest |= 0x80;
	pshsr(0xff);
	pc = LoadWord(0xFFF4);
	LOGPC("in swi2");
}

static void swi3()
{
	ccrest |= 0x80;
	pshsr(0xff);
	pc = LoadWord(0xFFF2);
	LOGPC("in swi3");
}

static void cmpd()             /* NxZxVxCx */
{
	INT_MC    dr, val;
	val = LoadWord((*adresl[ad])());
	m1 = ar; m2 = (-val) >> 8;
	dr = (ar << 8) + br - val;
	ovfl = res = sign = dr >> 8;
	res |= (dr & 255);
}

static void cmpy()             /* NxZxVxCx */
{
	INT_MC    val;
	val = LoadWord((*adresl[ad])());
	m1 = yr >> 8; m2 = (-val) >> 8;
	ovfl = res = sign = (yr - val) >> 8;
	res |= (yr - val) & 255;
}

static void cmpu()             /* NxZxVxCx */
{
	INT_MC    val;
	val = LoadWord((*adresl[ad])());
	m1 = ur >> 8; m2 = (-val) >> 8;
	ovfl = res = sign = (ur - val) >> 8;
	res |= (ur - val) & 255;
}

static void cmps()             /* NxZxVxCx */
{
	INT_MC    val;
	val = LoadWord((*adresl[ad])());
	m1 = sr >> 8; m2 = (-val) >> 8;
	ovfl = res = sign = (sr - val) >> 8;
	res |= (sr - val) & 255;
}

static void ldym()     /* NxZxV0 */
{
	yr = LoadWord((*adresl[ad])());
	m1 = ovfl;
	sign = yr >> 8;
	res = (res & 0x100) | ((sign | yr) & 255);
}

static void stym()             /* NxZxV0 */
{
	StoreWord((*adresl[ad])(), yr);
	m1 = ovfl;
	sign = yr >> 8;
	res = (res & 0x100) | ((sign | yr) & 255);
}

static void ldsm()     /* NxZxV0 */
{
	sr = LoadWord((*adresl[ad])());
	m1 = ovfl;
	sign = sr >> 8;
	res = (res & 0x100) | ((sign | sr) & 255);
}

static void stsm()             /* NxZxV0 */
{
	StoreWord((*adresl[ad])(), sr);
	m1 = ovfl;
	sign = sr >> 8;
	res = (res & 0x100) | ((sign | sr) & 255);
}


static void trap();
static void cd10();
static void cd11();

//static void (*code[])(void);
static void(*code[])(void) =
{ negm,what,trap,comm,lsrm,what,rorm,asrm,aslm,rolm,decm,what,incm,tstm,jmpm,clrm
,cd10,cd11,nopm,synm,what,what,lbra,lbsr,what,daam,orcc,what,andc,sexm,exgm,tfrm
,bras,brns,bhis,blss,bccs,blos,bnes,beqs,bvcs,bvss,bpls,bmis,bges,blts,bgts,bles
,leax,leay,leas,leau,pshs,puls,pshu,pulu,what,rtsm,abxm,rtim,cwai,mulm,what,swim
,nega,what,what,coma,lsra,what,rora,asra,asla,rola,deca,what,inca,tsta,what,clra
,negb,what,what,comb,lsrb,what,rorb,asrb,aslb,rolb,decb,what,incb,tstb,what,clrb
,negm,what,what,comm,lsrm,what,rorm,asrm,aslm,rolm,decm,what,incm,tstm,jmpm,clrm
,negm,what,what,comm,lsrm,what,rorm,asrm,aslm,rolm,decm,what,incm,tstm,jmpm,clrm
,suba,cmpa,sbca,subd,anda,bita,ldam,what,eora,adca,oram,adda,cmpx,bsrm,ldxm,what
,suba,cmpa,sbca,subd,anda,bita,ldam,stam,eora,adca,oram,adda,cmpx,jsrm,ldxm,stxm
,suba,cmpa,sbca,subd,anda,bita,ldam,stam,eora,adca,oram,adda,cmpx,jsrm,ldxm,stxm
,suba,cmpa,sbca,subd,anda,bita,ldam,stam,eora,adca,oram,adda,cmpx,jsrm,ldxm,stxm
,subb,cmpb,sbcb,addd,andb,bitb,ldbm,what,eorb,adcb,orbm,addb,lddm,what,ldum,what
,subb,cmpb,sbcb,addd,andb,bitb,ldbm,stbm,eorb,adcb,orbm,addb,lddm,stdm,ldum,stum
,subb,cmpb,sbcb,addd,andb,bitb,ldbm,stbm,eorb,adcb,orbm,addb,lddm,stdm,ldum,stum
,subb,cmpb,sbcb,addd,andb,bitb,ldbm,stbm,eorb,adcb,orbm,addb,lddm,stdm,ldum,stum

,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,lbrn,lbhi,lbls,lbcc,lblo,lbne,lbeq,lbvc,lbvs,lbpl,lbmi,lbge,lblt,lbgt,lble
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,swi2
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,cmpd,what,what,what,what,what,what,what,what,cmpy,what,ldym,what
,what,what,what,cmpd,what,what,what,what,what,what,what,what,cmpy,what,ldym,stym
,what,what,what,cmpd,what,what,what,what,what,what,what,what,cmpy,what,ldym,stym
,what,what,what,cmpd,what,what,what,what,what,what,what,what,cmpy,what,ldym,stym
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,ldsm,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,ldsm,stsm
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,ldsm,stsm
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,ldsm,stsm

,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,swi3
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,cmpu,what,what,what,what,what,what,what,what,cmps,what,what,what
,what,what,what,cmpu,what,what,what,what,what,what,what,what,cmps,what,what,what
,what,what,what,cmpu,what,what,what,what,what,what,what,what,cmps,what,what,what
,what,what,what,cmpu,what,what,what,what,what,what,what,what,cmps,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what,what
,what,what,what,cmpu,what,what,what,what,what,what,what,what,cmps,what,what,what
};

static void trap()
{
	struct MC6809_REGS regs;
	INT_MC r;

	mc6809_GetRegs(&regs);

	r = TrapCallback(&regs);

	mc6809_SetRegs(&regs, 0x1FF);

	ad = adr[r];
	cpu_clock += cpu_cycles[r];
	pc += taille[r] - 1;
	LOGPC("in trap");

	(*code[r])();
}

static void cd10()
{
	INT_MC r = ((*(op++)) & 0xFF) + 256;

	ad = adr[r];
	cpu_clock += cpu_cycles[r];
	pc += taille[r];
	LOGPC("in cd10");

	(*code[r])();
}

static void cd11()
{
	INT_MC r = ((*(op++)) & 0xFF) + 512;

	ad = adr[r];
	cpu_clock += cpu_cycles[r];
	pc += taille[r];
	LOGPC("in cd11");

	(*code[r])();
}


static void do_nmi(void)
{
	ccrest |= 0x80;  /* E � 1 */
	pshsr(0xff);
	ccrest |= 0x50;
	cpu_clock += 7;  /* 2 + 5 pour pshsr */
	pc = LoadWord(0xFFFC);
	LOGPC("in do_nmi");

	mc6809_nmi = 0;
}


static void do_irq(void)
{
	if (!(ccrest & 0x10)) /* si I � 0 */
	{
		ccrest |= 0x80;  /* E � 1 */
		pshsr(0xff);
		ccrest |= 0x10;
		cpu_clock += 7;  /* 2 + 5 pour pshsr */
		pc = LoadWord(0xFFF8);
		LOGPC("in do_irq");

		mc6809_irq = 0;
	}
}


static void do_firq(void)
{
	if (!(ccrest & 0x40)) /* si F � 0 */
	{
		ccrest &= 0x7F;  /* E � 0 */
		pshsr(0x81);
		ccrest |= 0x50;
		cpu_clock += 7;  /* 2 + 5 pour pshsr */
		pc = LoadWord(0xFFF6);
		LOGPC("in firq");

		mc6809_firq = 0;
	}
}


/************************************************/
/*** Interface publique de l'�mulateur MC6809 ***/
/************************************************/

/* broche de demande d'interruption ordinaire */
INT_MC mc6809_irq;
INT_MC mc6809_nmi;
INT_MC mc6809_firq;

#ifdef DEBUG
FILE *mc6809_ftrace = NULL;
#endif


mc6809_clock_t mc6809_clock(void)
{
	return cpu_clock;
}


void mc6809_GetRegs(struct MC6809_REGS *regs)
{
	regs->cc = getcc();
	regs->dp = dp;
	regs->ar = ar;
	regs->br = br;
	regs->xr = xr;
	regs->yr = yr;
	regs->ur = ur;
	regs->sr = sr;
	regs->pc = pc;
	regs->cpu_clock = cpu_clock;
	regs->cpu_timer = cpu_timer;
}


void mc6809_SetRegs(const struct MC6809_REGS *regs, INT_MC flags)
{
	if (flags&MC6809_REGS_CC_FLAG)
		setcc(regs->cc);

	if (flags&MC6809_REGS_DP_FLAG)
		dp = regs->dp;

	if (flags&MC6809_REGS_AR_FLAG)
		ar = regs->ar;

	if (flags&MC6809_REGS_BR_FLAG)
		br = regs->br;

	if (flags&MC6809_REGS_XR_FLAG)
		xr = regs->xr;

	if (flags&MC6809_REGS_YR_FLAG)
		yr = regs->yr;

	if (flags&MC6809_REGS_UR_FLAG)
		ur = regs->ur;

	if (flags&MC6809_REGS_SR_FLAG)
		sr = regs->sr;

	if (flags&MC6809_REGS_PC_FLAG)
		pc = regs->pc;

	if (flags&MC6809_REGS_CPUCLOCK_FLAG)
		cpu_clock = regs->cpu_clock;

	if (flags&MC6809_REGS_CPUTIMER_FLAG)
		cpu_timer = regs->cpu_timer;
}


void mc6809_SetTimer(mc6809_clock_t time, void(*func)(void *), void *data)
{
	cpu_timer = time;
	TimerCallback = func;
	timer_data = data;
}


void mc6809_Reset(void)
{
	dp = 0;
	ccrest |= 0x50;
	pc = LoadWord(0xFFFE);
	LOGPC("in mc6809_reset");
	mc6809_irq = 0;
	mc6809_nmi = 0;
	mc6809_firq = 0;
}


void mc6809_Init(const struct MC6809_INTERFACE *interface)
{
	INT_MC i;

	regist[0] = &xr;
	regist[1] = &yr;
	regist[2] = &ur;
	regist[3] = &sr;

	for (i = 0; i<16; i++)
		exreg[i] = NULL;

	exreg[1] = &xr;
	exreg[2] = &yr;
	exreg[3] = &ur;
	exreg[4] = &sr;
	exreg[5] = &pc;
	exreg[8] = &ar;
	exreg[9] = &br;
	exreg[11] = &dp;

	cpu_clock = 0;
	cpu_timer = MC6809_TIMER_DISABLED;

	FetchInstr = interface->FetchInstr;
	LoadByte = interface->LoadByte;
	LoadWord = interface->LoadWord;
	StoreByte = interface->StoreByte;
	StoreWord = interface->StoreWord;
	TrapCallback = interface->TrapCallback;
}


/*
* mc6809_StepExec: �x�cute un nombre donn� d'instructions et retourne le
*                  nombre de cycles n�cessaires � leur �x�cution
*
* MarkB - changed to execute number of cycles instead of number of instructions
*/

//INT_MC mc6809_StepExec(INT_MC ninst)
// MATT : changed this to INT_MC to match prototype of callback
UINT_MC mc6809_StepExec(UINT_MC ncycles)
{
	mc6809_clock_t start_clock = cpu_clock;
	//    register INT_MC i;
	INT_MC r;

	//	for (i=0; i<ninst; i++)
	while (ncycles > (cpu_clock - start_clock))
	{
		// start MPO
	#ifdef CPU_DEBUG
		MAME_Debug();
	#endif // CPU_DEBUG
		// end MPO

		LOGPC("in SE 1998");
		if (cpu_clock >= cpu_timer)
			TimerCallback(timer_data);

		// the priority for the external interrupts is NMI, FIRQ, and IRQ
		LOGPC("in SE 2002");
		if (mc6809_nmi)
			do_nmi();
		else if (mc6809_firq)
			do_firq();
		else if (mc6809_irq)
			do_irq();

	#ifdef DEBUG
		if (mc6809_ftrace)
			fprintf(mc6809_ftrace, "pc: %04X\n", pc);
	#endif

		/* on remplit le buffer de fetch */
		LOGPC("at FI call 01");
		FetchInstr(pc, fetch_buffer);
		op = (CHAR_MC *)fetch_buffer;

		/* on d�code l'instruction */
		r = (*(op++)) & 0xFF;
		ad = adr[r];
		cpu_clock += cpu_cycles[r];
		pc += taille[r];
		LOGPC("in SE 2026");

		/* on �x�cute l'instruction */
		(*code[r])();
	}

	return cpu_clock - start_clock;
}

// added by MPO to make debugging easier
UINT_MC mc6809_GetPC()
{
	return (UINT_MC)pc;
}

/*
* mc6809_TimeExec: fait tourner le MC6809 jusqu'� un instant donn� et
*                  retourne le nombre d'instructions �x�cut�es
*/

INT_MC mc6809_TimeExec(mc6809_clock_t time_limit)
{
	INT_MC r, ninst = 0;

	while (cpu_clock<time_limit)
	{
		if (cpu_clock >= cpu_timer)
			TimerCallback(timer_data);

		LOGPC("in TE 2075");
		if (mc6809_irq)
			do_irq();

	#ifdef DEBUG
		if (mc6809_ftrace)
			fprintf(mc6809_ftrace, "pc: %04X\n", pc);
	#endif

		/* on remplit le buffer de fetch */
		LOGPC("at FI call 02");
		FetchInstr(pc, fetch_buffer);
		op = (CHAR_MC *)fetch_buffer;

		/* on d�code l'instruction */
		r = (*(op++)) & 0xFF;

		ad = adr[r];
		cpu_clock += cpu_cycles[r];
		pc += taille[r];
		LOGPC("in TE 2074");

		/* on �x�cute l'instruction */
		(*code[r])();
		ninst++;
	}

	return ninst;
}

// MPO added

// returns an ASCII string giving info about registers and other stuff ...
const XCHAR_MC *mc6809_info(void *unused, INT_MC regnum)
{
	static XCHAR_MC buffer[1][81];
	static INT_MC which = 0;

	buffer[which][0] = 0;	// make sure the current string is empty

							// find which register they are requesting info about
	switch (regnum)
	{
	case CPU_INFO_REG + 0: sprintf(buffer[which], "PC:%04X", pc); break;
	case CPU_INFO_REG + 1: sprintf(buffer[which], " A:%02X", ar); break;
	case CPU_INFO_REG + 2: sprintf(buffer[which], " B:%02X", br); break;
	case CPU_INFO_REG + 3: sprintf(buffer[which], " X:%02X", xr); break;
	case CPU_INFO_REG + 4: sprintf(buffer[which], " Y:%02X", yr); break;
	case CPU_INFO_REG + 5: sprintf(buffer[which], " U:%02X", yr); break;
	case CPU_INFO_REG + 6: sprintf(buffer[which], " S:%02X", yr); break;
	case CPU_INFO_REG + 7: sprintf(buffer[which], "CC:%04X", getcc()); break;

		/*
		// I'm not sure how to decode flags on this thing
		case CPU_INFO_FLAGS:
		sprintf(buffer[which], "%c%c%c%c%c%c%c%c",
		context.p_reg & 0x80 ? 'N':'.',
		context.p_reg & 0x40 ? 'V':'.',
		context.p_reg & 0x20 ? 'R':'.',
		context.p_reg & 0x10 ? 'B':'.',
		context.p_reg & 0x08 ? 'D':'.',
		context.p_reg & 0x04 ? 'I':'.',
		context.p_reg & 0x02 ? 'Z':'.',
		context.p_reg & 0x01 ? 'C':'.');
		break;
		*/
	}

	return buffer[which];
}

