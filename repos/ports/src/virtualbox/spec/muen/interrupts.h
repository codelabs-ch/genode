/*
 * \brief  Muen subject pending interrupt handling.
 * \author Adrian-Ken Rueegsegger
 * \author Reto Buerki
 * \date   2016-04-27
 */

/*
 * Copyright (C) 2016 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */


/* Genode includes */
#include <base/stdint.h>
#include <os/attached_io_mem_dataspace.h>

static Genode::Attached_io_mem_dataspace interrupts_page(0xf00010000, 0x1000);

/**
 * Returns true if the bit corresponding to the specified IRQ is set.
 */
bool is_pending_interrupt(uint8_t irq)
{
	bool result;
	asm volatile ("bt %1, %2;"
				  "sbb %0, %0;"
				  : "=r"(result)
				  : "Ir"((uint32_t)irq), "m" (*(interrupts_page.local_addr<char>()))
				  : "memory");
	return result;
}


/**
 * Set bit corresponding to given IRQ in pending interrupts region.
 */
void set_pending_interrupt(uint8_t irq)
{
	asm volatile ("lock bts %1, %0"
				  : "+m" (*(interrupts_page.local_addr<char>()))
				  : "Ir" ((uint32_t)irq)
				  : "memory");
}


/**
 * Clear bit corresponding to given IRQ in pending interrupts region.
 */
void clear_pending_interrupt(uint8_t irq)
{
	asm volatile ("lock btr %1, %0"
				  : "+m" (*(interrupts_page.local_addr<char>()))
				  : "Ir" ((uint32_t)irq)
				  : "memory");
}
