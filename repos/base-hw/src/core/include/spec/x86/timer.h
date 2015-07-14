/*
 * \brief  Timer driver for core
 * \author Adrian-Ken Rueegsegger
 * \author Reto Buerki
 * \date   2015-02-06
 */

/*
 * Copyright (C) 2015 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _TIMER_H_
#define _TIMER_H_

/* Genode includes */
#include <util/mmio.h>
#include <base/stdint.h>
#include <base/printf.h>

/* core includes */
#include <port_io.h>
#include <board.h>

namespace Genode
{
	/**
	 * LAPIC-based timer driver for core
	 */
	class Timer;
}

class Genode::Timer : public Mmio
{
	private:

		enum {
			/* PIT constants */
			PIT_TICK_RATE  = 1193182ul,
			PIT_SLEEP_MS   = 50,
			PIT_SLEEP_TICS = (PIT_TICK_RATE / 1000) * PIT_SLEEP_MS,
			PIT_CH0_DATA   = 0x40,
			PIT_CH2_DATA   = 0x42,
			PIT_CH2_GATE   = 0x61,
			PIT_MODE       = 0x43,

			IA32_TSC_DEADLINE = 0x6e0,
		};

		/* Timer registers */
		struct Tmr_lvt : Register<0x320, 32>
		{
			struct Vector     : Bitfield<0,  8> { };
			struct Delivery   : Bitfield<8,  3> { };
			struct Mask       : Bitfield<16, 1> { };
			struct Timer_mode : Bitfield<17, 2> { };
		};
		struct Tmr_initial : Register <0x380, 32> { };
		struct Tmr_current : Register <0x390, 32> { };

		uint64_t _tics_per_ms = 0;

		static inline uint64_t rdtsc()
		{
			uint32_t lo, hi;
			asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
			return (uint64_t)hi << 32 | lo;
		}

		static inline void wrmsr(unsigned msr, uint64_t val)
		{
			asm volatile("wrmsr" : : "a" (static_cast<uint32_t>(val)), "d" (val >> 32), "c" (msr));
		}

		static inline uint64_t rdmsr(unsigned msr)
		{
			uint32_t low, high;
			asm volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
			return static_cast<uint64_t>(static_cast<uint64_t>(high) << 32 | low);
		}

	public:

		Timer() : Mmio(Board::MMIO_LAPIC_BASE)
		{
			/* Enable LAPIC timer in TSC deadline mode */
			write<Tmr_lvt::Vector>(Board::TIMER_VECTOR_KERNEL);
			write<Tmr_lvt::Delivery>(0);
			write<Tmr_lvt::Mask>(0);
			write<Tmr_lvt::Timer_mode>(2);

			/* Calculate timer frequency */
			_tics_per_ms = 2894674;
			PINF("TIMER LAPIC: timer frequency %llu kHz", _tics_per_ms);
		}

		/**
		 * Disable PIT timer channel. This is necessary since BIOS sets up
		 * channel 0 to fire periodically.
		 */
		static void disable_pit(void)
		{
			outb(PIT_MODE, 0x30);
			outb(PIT_CH0_DATA, 0);
			outb(PIT_CH0_DATA, 0);
		}

		static unsigned interrupt_id(int)
		{
			return Board::TIMER_VECTOR_KERNEL;
		}

		inline void start_one_shot(uint64_t const tics, unsigned)
		{
			wrmsr(IA32_TSC_DEADLINE, rdtsc() + tics);
			//PDBG("TIMER MSR programmed to %llu", rdmsr(IA32_TSC_DEADLINE));
		}

		uint64_t ms_to_tics(unsigned const ms)
		{
			//PDBG("TIMER %u ms are %llu tics", ms, ms * _tics_per_ms);
			return ms * _tics_per_ms;
		}

		uint64_t value(unsigned)
		{
			uint64_t now = rdtsc();
			uint64_t deadline = rdmsr(IA32_TSC_DEADLINE);

			return deadline == 0 ? deadline : deadline - now;
		}
};

namespace Kernel { class Timer : public Genode::Timer { }; }

#endif /* _TIMER_H_ */
