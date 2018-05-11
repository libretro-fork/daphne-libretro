/*
 * mix.cpp
 *
 * Copyright (C) 2007 Matt Ownby
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

// mix.cpp

#include <stdint.h>

#include "sound.h"
#include "../io/mpo_mem.h"
#include "mix.h"

#ifdef DEBUG
#include <assert.h>
#endif

// if we aren't using the MMX version

struct mix_s *g_pMixBufs = NULL;
uint8_t *g_pSampleDst = 0;
unsigned int g_uBytesToMix = 0;

// A C version of mix_mmx
// mix_mmx is 2.5 times as fast on Pentium 4
// NOTE : we always want this defined, even when using MMX, for the purpose of testing (see releasetest)
void mix_c(void)
{
	unsigned int uSamplesToMix = g_uBytesToMix >> 1;
	uint8_t *stream = g_pSampleDst;
	unsigned int sample = 0, val_to_store = 0;

	for (sample = 0; sample < uSamplesToMix; sample += 2)
	{
		int mixed_sample_1 = 0, mixed_sample_2 = 0;	// left/right channels
		struct mix_s *cur = g_pMixBufs;

		// mix all sound chips
		while (cur)
		{
			mixed_sample_1 += LOAD_LIL_SINT16(((int16_t *) cur->pMixBuf) + sample);
			mixed_sample_2 += LOAD_LIL_SINT16(((int16_t *) cur->pMixBuf) + sample + 1);
			cur = cur->pNext;
		}

		DO_CLIP(mixed_sample_1);
		DO_CLIP(mixed_sample_2);

		// note: sample2 needs to be on top because this is little endian, hence LSB
		val_to_store = 
			(((uint16_t) mixed_sample_2) << 16) | ((uint16_t) mixed_sample_1);

		STORE_LIL_UINT32(stream, val_to_store);
		stream += 4;
	}
}
