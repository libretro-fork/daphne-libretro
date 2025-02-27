/*
 * rgb2yuv.h
 *
 * Copyright (C) 2001 Matt Ownby
 *
 * This file is part of DAPHNE, a laserdisc arcade game emulator
 *
 * DAPHNE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * DAPHNE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef RGB2YUV_H
#define RGB2YUV_H

// the C version of this routine
void rgb2yuv();
extern unsigned int rgb2yuv_input[3];	// 8-bit
extern unsigned int rgb2yuv_result_y;	// 8-bit
extern unsigned int rgb2yuv_result_u;	// 8-bit
extern unsigned int rgb2yuv_result_v;	// 8-bit

/////////////////////////////

#endif
