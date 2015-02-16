/*
 * \brief  Timer driver for core
 * \author Norman Feske
 * \date   2013-04-05
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _TIMER_H_
#define _TIMER_H_

#include <base/stdint.h>
#include <base/printf.h>

/**
 * Read byte from I/O port
 */
inline Genode::uint8_t inb(Genode::uint16_t port)
{
	Genode::uint8_t res;
	asm volatile ("inb %%dx, %0" :"=a"(res) :"Nd"(port));
	return res;
}


/**
 * Write byte to I/O port
 */
inline void outb(Genode::uint16_t port, Genode::uint8_t val)
{
	asm volatile ("outb %b0, %w1" : : "a" (val), "Nd" (port));
}


namespace Genode
{
	/**
	 * Timer driver for core
	 *
	 * Timer channel 0 apparently doesn't work on the RPI, so we use channel 1
	 */
	class Timer;
}

class Genode::Timer
{
	public:

		Timer()
		{
			/* Init PIT in one-shot lobyte/hibyte mode */
			outb(PIT_MODE, 0x38);
		}

		static unsigned interrupt_id(int)
		{
			PDBG("not implemented");
			return 0;
		}

		inline void start_one_shot(uint32_t const tics, unsigned)
		{
			PDBG("not implemented");
		}

		static uint32_t ms_to_tics(unsigned const ms)
		{
			return (PIT_TICK_RATE / 1000) * ms;
		}

		unsigned value(unsigned)
		{
			PDBG("not implemented");
			return 0;
		}

	private:

		enum {
			PIT_TICK_RATE = 1193182ul,

			PIT_CH0  = 0x40,
			PIT_MODE = 0x43,
		};
};

namespace Kernel { class Timer : public Genode::Timer { }; }

#endif /* _TIMER_H_ */
