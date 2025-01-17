/*
 * m80.h
 *
 * Copyright (C) 2001,2003 Matt Ownby
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

#ifndef M80_H
#define M80_H

#include <stdint.h>

void m80_set_opcode_base(uint8_t *address);
void m80_reset();
uint32_t m80_execute(uint32_t cycles_to_execute);
/*__inline__*/ void m80_exec_cb();
void m80_set_nmi_line(uint8_t);
void m80_set_irq_line(uint8_t);
void m80_activate_nmi();
void m80_activate_irq();
uint32_t m80_get_pc();
void m80_set_pc(uint32_t);
uint16_t m80_get_sp();
void m80_set_sp(uint16_t);
uint16_t m80_get_reg(int index);
void m80_set_reg(int index, uint16_t val);
void m80_set_irq_callback(int32_t (*callback)(int));
void m80_set_nmi_callback(int32_t (*callback)());
uint32_t m80_get_cycles_executed();
uint32_t m80_get_context(void *context);
void m80_set_context(void *context);
unsigned int m80_dasm( char *buffer, unsigned pc );
const char *m80_info(void *context, int regnum);

typedef enum
{
	M80_PC, M80_SP, M80_AF, M80_AFPRIME, M80_HL, M80_HLPRIME, M80_DE, M80_DEPRIME,
	M80_BC, M80_BCPRIME, M80_IX, M80_IY, M80_RI, M80_REG_COUNT
} m80_regnames;

// These definitions mirror those found in mamewrap.h
// But I don't want to #include mamewrap.h in this file because I don't
// want all the daphne z80 games to depend on mamewrap.h
#define CLEAR_LINE 0
#define ASSERT_LINE 1

#endif
