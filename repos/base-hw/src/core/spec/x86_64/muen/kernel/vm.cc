/*
 * \brief   Kernel backend for virtual machines
 * \author  Stefan Kalkowski
 * \date    2015-06-03
 */

/*
 * Copyright (C) 2015 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#include <platform_pd.h>
#include <kernel/vm.h>
#include <cpu/cpu_state.h>
#include <pic.h>


Kernel::Vm::Vm(void * const state, Kernel::Signal_context * const context,
               void * const)
: Cpu_job(Cpu_priority::MIN, 0),
  _state((Genode::Vm_state * const) state),
  _context(context),
  _table(nullptr)
{
	affinity(cpu_pool()->primary_cpu());
}


Kernel::Vm::~Vm() { }


void Kernel::Vm::exception(unsigned const cpu_id)
{
	if (_state->trapno == 200) {
		pause();
		_context->submit(1);
		return;
	}

	if (_state->trapno >= Genode::Cpu_state::INTERRUPTS_START &&
		_state->trapno <= Genode::Cpu_state::INTERRUPTS_END)
	{
		pic()->irq_occurred(_state->trapno);
		_interrupt(cpu_id);
		return;
	}
	PWRN("VM: triggered unknown exception %lu with error code %lu",
		 _state->trapno, _state->errcode);
//	_stop();
}


void Kernel::Vm::proceed(unsigned const cpu_id)
{
	asm volatile ("vmcall" : : "a" (1) : "memory");
}


void Kernel::Vm::inject_irq(unsigned irq) { }
