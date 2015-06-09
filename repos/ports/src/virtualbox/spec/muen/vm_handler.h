/*
 * \brief  Genode/Muen specific VirtualBox SUPLib supplements
 * \author Adrian-Ken Rueegsegger
 */

/*
 * Copyright (C) 201 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#ifndef _GENODE_VIRTUALBOX_VM_HANDLER__H_
#define _GENODE_VIRTUALBOX_VM_HANDLER__H_

#include <base/env.h>
#include <base/signal.h>

#include <vm_session/vm_session.h>
#include <vm_session/connection.h>

namespace Genode
{
	/**
	 * Vm execution handler.
	 */
	class Vm_handler;
}


class Genode::Vm_handler
{
	private:

		Vm_connection _vm_session;
		Signal_context_capability _sig_cap;
		Signal_receiver *_sig_rcv;
		Signal_transmitter _sig_xmit;
		Signal_context _sig_ctx;

	public:

		Vm_handler()
		{
			_sig_rcv = new (env()->heap())Signal_receiver();
			_sig_cap = _sig_rcv->manage(&_sig_ctx);
			_sig_xmit.context(_sig_cap);
			_vm_session.exception_handler(_sig_cap);
		}

		~Vm_handler() { _sig_rcv->dissolve(&_sig_ctx); }

		/**
		 * Starts execution of the Vm and blocks until the Vm returns or the
		 * execution handler gets poked.
		 */
		void run_vm()
		{
			_vm_session.run();
			_sig_rcv->wait_for_signal();
		}


		/**
		 * Pauses the Vm and sends a signal to the Vm execution thread.
		 */
		void poke()
		{
			_vm_session.pause();
			_sig_xmit.submit();
		}
};

#endif /* _GENODE_VIRTUALBOX_VM_HANDLER__H_ */
