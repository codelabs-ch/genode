/*
 * \brief  Timer driver for core
 * \author Reto Buerki
 * \date   2015-04-14
 */

/*
 * Copyright (C) 2015 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _CORE__INCLUDE__SPEC__X86_64__MUEN__TIMER_H_
#define _CORE__INCLUDE__SPEC__X86_64__MUEN__TIMER_H_

/* base includes */
#include <base/printf.h>
#include <muen/sinfo.h>

/* core includes */
#include <board.h>

namespace Genode
{
	/**
	 * Timer driver for core on Muen
	 */
	class Timer;
}

class Genode::Timer
{
	private:

		enum {
			TIMER_DISABLED = ~0ULL,
		};

		uint64_t _tics_per_ms;

		struct Subject_timed_event
		{
			uint64_t tsc_trigger;
			uint8_t  event_nr :5;
		} __attribute__((packed));

		struct Subject_timed_event * _event_page = 0;
		struct Subject_timed_event * _guest_event_page = 0;

		inline uint64_t rdtsc()
		{
			uint32_t lo, hi;
			asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
			return (uint64_t)hi << 32 | lo;
		}

		class Invalid_region {};

	public:

		Timer() : _tics_per_ms(Sinfo::get_tsc_khz())
		{
			struct Sinfo::Memregion_info region;
			if (!Sinfo::get_memregion_info("timed_event", &region)) {
				PERR("muen-timer: Unable to retrieve timed event region");
				throw Invalid_region();
			}

			_event_page = (Subject_timed_event *)region.address;
			_event_page->event_nr = Board::TIMER_EVENT_KERNEL;
			PINF("muen-timer: Page @0x%llx, frequency %llu kHz, event %u",
			     region.address, _tics_per_ms, _event_page->event_nr);

			if (Sinfo::get_memregion_info("monitor_timed_event", &region)) {
				PINF("muen-timer: Found guest timed event page @0x%llx"
					 " -> enabling preemption", region.address);
				_guest_event_page = (Subject_timed_event *)region.address;
				_guest_event_page->event_nr = Board::TIMER_EVENT_PREEMPT;
			}

		}

		static unsigned interrupt_id(int)
		{
			return Board::TIMER_VECTOR_KERNEL;
		}

		inline void start_one_shot(uint32_t const tics, unsigned)
		{
			const uint64_t t = rdtsc() + tics;
			_event_page->tsc_trigger = t;

			if (_guest_event_page)
				_guest_event_page->tsc_trigger = t;
		}

		uint32_t ms_to_tics(unsigned const ms)
		{
			return ms * _tics_per_ms;
		}

		unsigned value(unsigned)
		{
			const uint64_t now = rdtsc();
			if (_event_page->tsc_trigger != TIMER_DISABLED
			    && _event_page->tsc_trigger > now) {
				return _event_page->tsc_trigger - now;
			}
			return 0;
		}

		/*
		 * Dummies
		 */
		static void disable_pit(void) { }
};

namespace Kernel { class Timer : public Genode::Timer { }; }

#endif /* _CORE__INCLUDE__SPEC__X86_64__MUEN__TIMER_H_ */
