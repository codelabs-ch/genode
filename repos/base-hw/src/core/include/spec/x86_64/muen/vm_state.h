/*
 * \brief   CPU context of a virtual machine
 * \author  Stefan Kalkowski
 * \date    2015-06-03
 */

/*
 * Copyright (C) 2015 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _VM_STATE_H_
#define _VM_STATE_H_

namespace Genode
{
	/**
	 * Pseudo context of a virtual machine
	 *
	 */
	struct Vm_state;

	using Cpu_state_modes = Cpu_state;
}

struct Genode::Vm_state
{
	addr_t trapno;
	addr_t errcode;
};

#endif /* _VM_STATE_H_ */
