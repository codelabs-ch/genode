/*
 * \brief  Genode specific VirtualBox SUPLib supplements.
 *         File used by Genode platforms not supporting hardware
 *         virtualisation features.
 * \author Alexander Boettcher
 * \date   2013-11-18
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Genode includes */
#include <base/printf.h>
#include <base/semaphore.h>
#include <os/attached_io_mem_dataspace.h>

/* VirtualBox includes */
#include "HMInternal.h" /* enable access to hm.s.* */
#include "CPUMInternal.h" /* enable access to cpum.s.* */
#include <VBox/vmm/vm.h>
#include <VBox/vmm/rem.h>
#include <VBox/err.h>

/* Genode's VirtualBox includes */
#include "sup.h"
#include "vcpu.h"
#include "vm_handler.h"
#include "vmm_memory.h"

/* Libc include */
#include <pthread.h>

static Genode::Vm_handler vm_handler;

/* VirtualBox SUPLib interface */


int SUPR3QueryVTxSupported(void) { return VINF_SUCCESS; }


int SUPR3CallVMMR0Fast(PVMR0 pVMR0, unsigned uOperation, VMCPUID idCpu)
{
	static Genode::Attached_io_mem_dataspace subject_state(0xf00000000, 0x1000);

	switch (uOperation) {
	case SUP_VMMR0_DO_HM_RUN:
		struct Subject_state *cur_state = subject_state.local_addr<struct Subject_state>();
		VM     * pVM   = reinterpret_cast<VM *>(pVMR0);
		PVMCPU   pVCpu = &pVM->aCpus[idCpu];
		PCPUMCTX pCtx  = CPUMQueryGuestCtxPtr(pVCpu);

		cur_state->Rip = pCtx->rip;
		cur_state->Rsp = pCtx->rsp;

		cur_state->Regs.Rax = pCtx->rax;
		cur_state->Regs.Rbx = pCtx->rbx;
		cur_state->Regs.Rcx = pCtx->rcx;
		cur_state->Regs.Rdx = pCtx->rdx;
		cur_state->Regs.Rbp = pCtx->rbp;
		cur_state->Regs.Rsi = pCtx->rsi;
		cur_state->Regs.Rdi = pCtx->rdi;
		cur_state->Regs.R08 = pCtx->r8;
		cur_state->Regs.R09 = pCtx->r9;
		cur_state->Regs.R10 = pCtx->r10;
		cur_state->Regs.R11 = pCtx->r11;
		cur_state->Regs.R12 = pCtx->r12;
		cur_state->Regs.R13 = pCtx->r13;
		cur_state->Regs.R14 = pCtx->r14;
		cur_state->Regs.R15 = pCtx->r15;

		cur_state->Rflags = pCtx->rflags.u;

		cur_state->Shadow_cr0 = pCtx->cr0;
		cur_state->Cr0  = pCtx->cr0;
		cur_state->Cr0 |= 1 << 5;
		cur_state->Regs.Cr2 = pCtx->cr2;
		cur_state->Cr3  = pCtx->cr3;
		cur_state->Cr4  = pCtx->cr4;
		cur_state->Cr4  |= 1 << 13;

		cur_state->Cs = pCtx->cs.Sel;
		cur_state->Ss = pCtx->ss.Sel;

		cur_state->Ia32_efer = CPUMGetGuestEFER(pVCpu);

		/*
		utcb->sysenter_cs = pCtx->SysEnter.cs;
		utcb->sysenter_sp = pCtx->SysEnter.esp;
		utcb->sysenter_ip = pCtx->SysEnter.eip;
		utcb->dr7  = pCtx->dr[7];

		utcb->idtr.limit  = pCtx->idtr.cbIdt;
		utcb->idtr.base   = pCtx->idtr.pIdt;
		utcb->gdtr.limit  = pCtx->gdtr.cbGdt;
		utcb->gdtr.base   = pCtx->gdtr.pGdt;

		TODO: For all segments, tr

		cur_state->ldtr.sel   = pCtx->ldtr.Sel;
		cur_state->ldtr.limit = pCtx->ldtr.u32Limit;
		cur_state->ldtr.base  = pCtx->ldtr.u64Base;
		cur_state->ldtr.ar    = pCtx->ldtr.Attr.u;
		*/

		vm_handler.run_vm();

#ifdef VBOX_WITH_REM
		/* XXX see VMM/VMMR0/HMVMXR0.cpp - not necessary every time ! XXX */
		REMFlushTBs(pVM);
#endif

		return VINF_EM_RAW_EMULATE_INSTR;
	}

	return VERR_INTERNAL_ERROR;
}


static Genode::Semaphore *r0_halt_sem()
{
	static Genode::Semaphore sem;
	return &sem;
}


int SUPR3CallVMMR0Ex(PVMR0 pVMR0, VMCPUID idCpu, unsigned
                     uOperation, uint64_t u64Arg, PSUPVMMR0REQHDR pReqHdr)
{
	switch(uOperation)
	{
 		case VMMR0_DO_GVMM_CREATE_VM:
			genode_VMMR0_DO_GVMM_CREATE_VM(pReqHdr);
			return VINF_SUCCESS;

		case VMMR0_DO_GVMM_SCHED_HALT:
			r0_halt_sem()->down();
			return VINF_SUCCESS;

		case VMMR0_DO_GVMM_SCHED_WAKE_UP:
			r0_halt_sem()->up();
			return VINF_SUCCESS;

		case VMMR0_DO_VMMR0_INIT:
			reinterpret_cast<VM *>(pVMR0)->hm.s.svm.fSupported = false;
			reinterpret_cast<VM *>(pVMR0)->hm.s.vmx.fSupported = true;
			return VINF_SUCCESS;

		case VMMR0_DO_GVMM_SCHED_POLL:
			/* called by 'vmR3HaltGlobal1Halt' */
			PDBG("SUPR3CallVMMR0Ex: VMMR0_DO_GVMM_SCHED_POLL");
			return VINF_SUCCESS;

		case VMMR0_DO_HM_SETUP_VM:
			return VINF_SUCCESS;

		case VMMR0_DO_GVMM_SCHED_POKE:
			vm_handler.poke();
			return VINF_SUCCESS;

		default:
			PERR("SUPR3CallVMMR0Ex: unhandled uOperation %d", uOperation);
			return VERR_GENERAL_FAILURE;
	}
}


bool create_emt_vcpu(pthread_t * thread, size_t stack_size,
                     const pthread_attr_t *attr,
                     void *(*start_routine)(void *), void *arg,
                     Genode::Cpu_session * cpu_session,
                     Genode::Affinity::Location location)
{
	/* no hardware acceleration support */
	return false;
}


/**
 * Dummies and unimplemented stuff.
 */

uint64_t genode_cpu_hz() {
	return 1000000000ULL; /* XXX fixed 1GHz return value */
}


bool Vmm_memory::revoke_from_vm(Mem_region *r)
{
	PWRN("%s unimplemented", __func__);
	return false;
}


extern "C" void pthread_yield() { PWRN("%s unimplemented", __func__); }
