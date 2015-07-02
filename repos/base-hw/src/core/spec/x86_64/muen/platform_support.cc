/*
 * \brief   Platform implementations specific for x86_64_muen
 * \author  Reto Buerki
 * \author  Adrian-Ken Rueegsegger
 * \date    2015-04-21
 */

/*
 * Copyright (C) 2015 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* core includes */
#include <platform.h>

using namespace Genode;

Native_region * Platform::_core_only_mmio_regions(unsigned const i)
{
	static Native_region _regions[] =
	{
		/* Sinfo pages */
		{ Board::SINFO_BASE_ADDR, Board::SINFO_SIZE },
		/* Timer page */
		{ Board::TIMER_BASE_ADDR, Board::TIMER_SIZE },
	};
	return i < sizeof(_regions)/sizeof(_regions[0]) ? &_regions[i] : 0;
}

void Platform::setup_irq_mode(unsigned, unsigned, unsigned) { }

void Platform::_init_additional() { }
