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
#include <timer_session/connection.h>
#include <os/attached_io_mem_dataspace.h>
#include <rom_session/connection.h>

/* VirtualBox includes */
#include "HMInternal.h" /* enable access to hm.s.* */
#include "CPUMInternal.h" /* enable access to cpum.s.* */
#include <VBox/vmm/vm.h>
#include <VBox/vmm/rem.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/err.h>

/* Genode's VirtualBox includes */
#include "sup.h"
#include "vcpu.h"
#include "vm_handler.h"
#include "vmm_memory.h"

/* Libc include */
#include <pthread.h>

enum {
	VMCS_SEG_UNUSABLE  = 0x10000,
	BLOCKING_BY_STI    = 1U << 0,
	BLOCKING_BY_MOV_SS = 1U << 1,
};

#define GENODE_READ_SELREG_REQUIRED(REG) \
    (pCtx->REG.Sel != cur_state->REG.sel) || \
    (pCtx->REG.ValidSel != cur_state->REG.sel) || \
    (pCtx->REG.fFlags   != CPUMSELREG_FLAGS_VALID) || \
    (pCtx->REG.u32Limit != cur_state->REG.limit) || \
    (pCtx->REG.u64Base  != cur_state->REG.base) || \
    (pCtx->REG.Attr.u   != cur_state->REG.access)

#define GENODE_READ_SELREG(REG) \
	pCtx->REG.Sel      = cur_state->REG.sel; \
	pCtx->REG.ValidSel = cur_state->REG.sel; \
	pCtx->REG.fFlags   = CPUMSELREG_FLAGS_VALID; \
	pCtx->REG.u32Limit = cur_state->REG.limit; \
	pCtx->REG.u64Base  = cur_state->REG.base; \
	pCtx->REG.Attr.u   = cur_state->REG.access

#define GENODE_WRITE_SELREG(REG) \
	Assert(pCtx->REG.fFlags & CPUMSELREG_FLAGS_VALID); \
	Assert(pCtx->REG.ValidSel == pCtx->REG.Sel); \
	cur_state->REG.sel   = pCtx->REG.Sel; \
	cur_state->REG.limit = pCtx->REG.u32Limit; \
	cur_state->REG.base  = pCtx->REG.u64Base; \
	\
	/* attribute fixup according to 'VMX_WRITE_SELREG' in 'HWVMXR0.h' */ \
	if (( pCtx->REG.Sel \
	   || !CPUMIsGuestInPagedProtectedModeEx(pCtx) \
	   || (!pCtx->cs.Attr.n.u1DefBig && !CPUMIsGuestIn64BitCodeEx(pCtx))) \
	  && pCtx->REG.Attr.n.u1Present == 1) \
	{ \
		cur_state->REG.access = pCtx->REG.Attr.u | X86_SEL_TYPE_ACCESSED; \
	} else { \
		cur_state->REG.access = VMCS_SEG_UNUSABLE; \
	}

static Genode::Vm_handler vm_handler;

/* VirtualBox SUPLib interface */


/**
 * Returns the value of the register identified by reg.
 * The register mapping is specified by Intel SDM Vol. 3C, table 27-3.
 */
inline uint64_t get_reg_val (struct Subject_state *cur_state, unsigned reg)
{
	switch (reg) {
		case 0:
			return cur_state->Regs.Rax;
			break;
		case 1:
			return cur_state->Regs.Rcx;
			break;
		case 2:
			return cur_state->Regs.Rdx;
			break;
		case 3:
			return cur_state->Regs.Rbx;
			break;
		case 4:
			return cur_state->Rsp;
			break;
		case 5:
			return cur_state->Regs.Rbp;
			break;
		case 6:
			return cur_state->Regs.Rsi;
			break;
		case 7:
			return cur_state->Regs.Rdi;
			break;
		default:
			PDBG("Invalid register %u", reg);
			return 0;
	}
}


/**
 * Sets the control register identified by cr to the given value.
 */
inline bool set_cr(struct Subject_state *cur_state, unsigned cr, uint64_t value)
{
	bool res = false;
	switch (cr) {
		case 0:
			cur_state->Shadow_cr0 = value;
			cur_state->Cr0  = value | 1 << 5;
			cur_state->Cr0 &= ~(1 << 30 | 1 << 29);
			res = true;
			break;
		case 2:
			cur_state->Regs.Cr2 = value;
			res = true;
			break;
		case 3:
			cur_state->Cr3 = value;
			res = true;
			break;
		case 4:
			cur_state->Shadow_cr4 = value;
			cur_state->Cr4  = value | 1 << 13;
			res = true;
			break;
		default:
			PDBG("Invalid control register %u", cr);
			res = false;
	}

	return res;
}


/**
 * Handle control register access by evaluating the VM-exit qualification
 * according to Intel SDM Vol. 3C, table 27-3.
 */
inline bool handle_cr(struct Subject_state *cur_state)
{
	uint64_t qual = cur_state->Exit_qualification;
	unsigned cr  =  qual & 0xf;
	unsigned acc = (qual & 0x30) >> 4;
	unsigned reg = (qual & 0xf00) >> 8;
	bool res;

	switch (acc) {
		case 0: // MOV to CR
			res = set_cr(cur_state, cr, get_reg_val(cur_state, reg));
			break;
		default:
			PDBG("Invalid control register access");
			return false;
	}

	if (res)
		cur_state->Rip += cur_state->Instruction_len;

	return res;
}


inline bool has_pending_irq(PVMCPU pVCpu)
{
	if (!TRPMHasTrap(pVCpu) &&
		!VMCPU_FF_IS_PENDING(pVCpu, (VMCPU_FF_INTERRUPT_APIC |
		                             VMCPU_FF_INTERRUPT_PIC)))
		return false;

	return true;
}


inline void inject_irq(PVMCPU pVCpu)
{
	int rc;

	if (!TRPMHasTrap(pVCpu)) {
		bool res = VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NMI);
		Assert(!res);

		if (VMCPU_FF_IS_PENDING(pVCpu, (VMCPU_FF_INTERRUPT_APIC |
						VMCPU_FF_INTERRUPT_PIC))) {

			uint8_t irq;
			rc = PDMGetInterrupt(pVCpu, &irq);
			Assert(RT_SUCCESS(rc));

			rc = TRPMAssertTrap(pVCpu, irq, TRPM_HARDWARE_INT);
			Assert(RT_SUCCESS(rc));
		}
	}

	Assert(TRPMHasTrap(pVCpu));

	uint8_t   u8Vector;
	TRPMEVENT enmType;
	RTGCUINT  u32ErrorCode;

	/* If a new event is pending, then dispatch it now. */
	rc = TRPMQueryTrapAll(pVCpu, &u8Vector, &enmType, 0, 0, 0);
	AssertRC(rc);
	Assert(enmType == TRPM_HARDWARE_INT);
	Assert(u8Vector != X86_XCPT_NMI);

	/* Clear the pending trap. */
	rc = TRPMResetTrap(pVCpu);
	AssertRC(rc);

	switch (u8Vector) {
		case 8: // Non-remapped RTC
			asm volatile ("vmcall" : : "a" (8) : "memory");
			break;
		case 48: // Timer
			asm volatile ("vmcall" : : "a" (2) : "memory");
			break;
		case 49: // Kbd
			asm volatile ("vmcall" : : "a" (3) : "memory");
			break;
		case 56: // RTC
			asm volatile ("vmcall" : : "a" (9) : "memory");
			break;
		case 60: // Ptr
			asm volatile ("vmcall" : : "a" (6) : "memory");
			break;
		case 63: // Ata
			asm volatile ("vmcall" : : "a" (4) : "memory");
			break;
		case 239: // LOC (Local timer interrupts)
			asm volatile ("vmcall" : : "a" (5) : "memory");
			break;
		case 246: // Work
			asm volatile ("vmcall" : : "a" (7) : "memory");
			break;
		default:
			PDBG("No event to inject interrupt %u", u8Vector);
	}
}


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

		if (has_pending_irq(pVCpu))
			inject_irq(pVCpu);

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

		cur_state->Sysenter_cs  = pCtx->SysEnter.cs;
		cur_state->Sysenter_eip = pCtx->SysEnter.eip;
		cur_state->Sysenter_esp = pCtx->SysEnter.esp;

		set_cr(cur_state, 0, pCtx->cr0);
		set_cr(cur_state, 2, pCtx->cr2);
		set_cr(cur_state, 3, pCtx->cr3);
		set_cr(cur_state, 4, pCtx->cr4);

		GENODE_WRITE_SELREG(cs);
		GENODE_WRITE_SELREG(ss);
		GENODE_WRITE_SELREG(ds);
		GENODE_WRITE_SELREG(es);
		GENODE_WRITE_SELREG(fs);
		GENODE_WRITE_SELREG(gs);

		if (pCtx->ldtr.Sel == 0) {
			cur_state->ldtr.sel    = 0;
			cur_state->ldtr.limit  = 0;
			cur_state->ldtr.base   = 0;
			cur_state->ldtr.access = 0x82;
		} else {
			cur_state->ldtr.sel    = pCtx->ldtr.Sel;
			cur_state->ldtr.limit  = pCtx->ldtr.u32Limit;
			cur_state->ldtr.base   = pCtx->ldtr.u64Base;
			cur_state->ldtr.access = pCtx->ldtr.Attr.u;
		}
		Assert(pCtx->tr.Attr.u & X86_SEL_TYPE_SYS_TSS_BUSY_MASK);
		{
			cur_state->tr.sel    = pCtx->tr.Sel;
			cur_state->tr.limit  = pCtx->tr.u32Limit;
			cur_state->tr.base   = pCtx->tr.u64Base;
			cur_state->tr.access = pCtx->tr.Attr.u;
		}

		cur_state->idtr.limit = pCtx->idtr.cbIdt;
		cur_state->idtr.base  = pCtx->idtr.pIdt;
		cur_state->gdtr.limit = pCtx->gdtr.cbGdt;
		cur_state->gdtr.base  = pCtx->gdtr.pGdt;

		cur_state->Ia32_efer = CPUMGetGuestEFER(pVCpu);

		/*
		utcb->dr7  = pCtx->dr[7];
		*/

		VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC);

resume:
		vm_handler.run_vm();

		switch(cur_state->Exit_reason)
		{
			case 0x1c: // Control-register access
				if (handle_cr(cur_state)) {
					goto resume;
				}
				break;
			case 0x01: // External interrupt
			case 0x02: // Triple fault
			case 0x09: // Task switch
			case 0x0a: // CPUID
			case 0x0c: // HLT
			case 0x0e: // INVLPG
			case 0x10: // RDTSC
			case 0x1d: // MOV DR
			case 0x1e: // I/O instruction
			case 0x1f: // RDMSR
			case 0x20: // WRMSR
			case 0x28: // PAUSE
			case 0x30: // EPT violation
			case 0x34: // VMX preemption timer
			case 0x36: // WBINVD
				break;
			default:
				PDBG("VM exited with exit reason %llx", cur_state->Exit_reason);
				PDBG("-> qualification %llx", cur_state->Exit_qualification);
				PDBG("-> interrupt info %llx", cur_state->Interrupt_info);
				PDBG("-> RIP: %lx, RSP: %lx", cur_state->Rip, cur_state->Rsp);
				PDBG("-> RFLAGS: %lx, EFER: %lx", cur_state->Rflags, cur_state->Ia32_efer);
				PDBG("-> CR0: %lx, CR2: %lx, CR3: %lx, CR4: %lx", cur_state->Cr0, cur_state->Regs.Cr2, cur_state->Cr3, cur_state->Cr4);
				PDBG("-> CS: %lx, SS: %lx", cur_state->cs.sel, cur_state->ss.sel);
		}

		CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_GLOBAL_TLB_FLUSH);

		VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED);

		pCtx->rip = cur_state->Rip;
		pCtx->rsp = cur_state->Rsp;

		pCtx->rax = cur_state->Regs.Rax;
		pCtx->rbx = cur_state->Regs.Rbx;
		pCtx->rcx = cur_state->Regs.Rcx;
		pCtx->rdx = cur_state->Regs.Rdx;
		pCtx->rbp = cur_state->Regs.Rbp;
		pCtx->rsi = cur_state->Regs.Rsi;
		pCtx->rdi = cur_state->Regs.Rdi;
		pCtx->r8  = cur_state->Regs.R08;
		pCtx->r9  = cur_state->Regs.R09;
		pCtx->r10 = cur_state->Regs.R10;
		pCtx->r11 = cur_state->Regs.R11;
		pCtx->r12 = cur_state->Regs.R12;
		pCtx->r13 = cur_state->Regs.R13;
		pCtx->r14 = cur_state->Regs.R14;
		pCtx->r15 = cur_state->Regs.R15;

		pCtx->rflags.u = cur_state->Rflags;

		if (pCtx->SysEnter.cs != cur_state->Sysenter_cs)
			CPUMSetGuestMsr(pVCpu, MSR_IA32_SYSENTER_CS, cur_state->Sysenter_cs);
		if (pCtx->SysEnter.esp != cur_state->Sysenter_esp)
			CPUMSetGuestMsr(pVCpu, MSR_IA32_SYSENTER_ESP, cur_state->Sysenter_esp);
		if (pCtx->SysEnter.eip != cur_state->Sysenter_eip)
			CPUMSetGuestMsr(pVCpu, MSR_IA32_SYSENTER_EIP, cur_state->Sysenter_eip);

		if (pCtx->idtr.cbIdt != cur_state->idtr.limit ||
		    pCtx->idtr.pIdt  != cur_state->idtr.base)
			CPUMSetGuestIDTR(pVCpu, cur_state->idtr.base, cur_state->idtr.limit);
		if (pCtx->gdtr.cbGdt != cur_state->gdtr.limit ||
		    pCtx->gdtr.pGdt  != cur_state->gdtr.base)
			CPUMSetGuestGDTR(pVCpu, cur_state->gdtr.base, cur_state->gdtr.limit);

		{
			uint32_t val;
			val  = (cur_state->Shadow_cr0 & pVCpu->hm.s.vmx.u32CR0Mask);
			val |= (cur_state->Cr0 & ~pVCpu->hm.s.vmx.u32CR0Mask);
			if (pCtx->cr0 != val)
				CPUMSetGuestCR0(pVCpu, val);
		}
		if (pCtx->cr2 != cur_state->Regs.Cr2)
			CPUMSetGuestCR2(pVCpu, cur_state->Regs.Cr2);
		{
			uint32_t val;
			val  = (cur_state->Shadow_cr4 & pVCpu->hm.s.vmx.u32CR4Mask);
			val |= (cur_state->Cr4 & ~pVCpu->hm.s.vmx.u32CR4Mask);
			if (pCtx->cr4 != val)
				CPUMSetGuestCR4(pVCpu, val);
		}

		/*
		 * Guest CR3 must be handled after saving CR0 & CR4.
		 * See HMVMXR0.cpp, function hmR0VmxSaveGuestControlRegs
		 */
		if (pCtx->cr3 != cur_state->Cr3) {
			CPUMSetGuestCR3(pVCpu, cur_state->Cr3);
		}

		GENODE_READ_SELREG(cs);
		GENODE_READ_SELREG(ss);
		GENODE_READ_SELREG(ds);
		GENODE_READ_SELREG(es);
		GENODE_READ_SELREG(fs);
		GENODE_READ_SELREG(gs);

		if (GENODE_READ_SELREG_REQUIRED(ldtr)) {
			GENODE_READ_SELREG(ldtr);
			CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_LDTR);
		}
		if (GENODE_READ_SELREG_REQUIRED(tr)) {
			GENODE_READ_SELREG(tr);
			CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_TR);
		}

		CPUMSetGuestEFER(pVCpu, cur_state->Ia32_efer);

		VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_TO_R3);

		if (cur_state->Intr_state != 0) {
			Assert(cur_state->Intr_state == BLOCKING_BY_STI ||
			       cur_state->Intr_state == BLOCKING_BY_MOV_SS);
			EMSetInhibitInterruptsPC(pVCpu, pCtx->rip);
		} else
			VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INHIBIT_INTERRUPTS);

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
			{
				VM     * pVM   = reinterpret_cast<VM *>(pVMR0);
				PVMCPU   pVCpu = &pVM->aCpus[idCpu];
				pVM->hm.s.svm.fSupported = false;
				pVM->hm.s.vmx.fSupported = true;
				pVM->hm.s.vmx.fAllowUnrestricted = false;
				pVCpu->hm.s.vmx.u32CR0Mask = 0x60000020;
				pVCpu->hm.s.vmx.u32CR4Mask = 0x2000;
				return VINF_SUCCESS;
			}
		case VMMR0_DO_GVMM_SCHED_POLL:
		case VMMR0_DO_GVMM_DESTROY_VM:
		case VMMR0_DO_VMMR0_TERM:
		case VMMR0_DO_HM_SETUP_VM:
		case VMMR0_DO_HM_ENABLE:
			return VINF_SUCCESS;

		case VMMR0_DO_GVMM_SCHED_POKE:
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
                     Genode::Affinity::Location location,
                     unsigned int cpu_id)
{
	/* no hardware acceleration support */
	return false;
}


void genode_update_tsc(void (*update_func)(void), unsigned long update_us)
{
	using namespace Genode;

	Timer::Connection timer;
	Signal_context    sig_ctx;
	Signal_receiver   sig_rec;
	Signal_context_capability sig_cap = sig_rec.manage(&sig_ctx);

	timer.sigh(sig_cap);
	timer.trigger_once(update_us);

	for (;;) {
		Signal s = sig_rec.wait_for_signal();
		update_func();

		timer.trigger_once(update_us);
	}
}


uint64_t genode_cpu_hz()
{
	static uint64_t cpu_freq = 0;
	/* TODO: Reuse muen/sinfo.h or export information as report */
	struct Sinfo {
		uint64_t foo;
		uint64_t bar;
		uint64_t tsc_khz;
	};

	if (!cpu_freq) {
		try {
			using namespace Genode;

			Rom_connection sinfo_rom("subject_info_page");

			Sinfo * const sinfo = env()->rm_session()->attach(sinfo_rom.dataspace());

			cpu_freq = sinfo->tsc_khz * 1000;

		} catch (...) {
			PERR("unable to read CPU frequency from subject info page.");
			Genode::Lock lock;
			lock.lock();
		}
	}

	return cpu_freq;
}


/**
 * Dummies and unimplemented stuff.
 */


bool Vmm_memory::revoke_from_vm(Mem_region *r)
{
//	PWRN("%s unimplemented", __func__);
	return true;
}


extern "C" void pthread_yield() { PWRN("%s unimplemented", __func__); }
