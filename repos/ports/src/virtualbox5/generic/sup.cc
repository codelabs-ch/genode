/*
 * \brief  VirtualBox SUPLib supplements
 * \author Norman Feske
 * \date   2013-08-20
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Genode includes */
#include <base/attached_ram_dataspace.h>
#include <trace/timestamp.h>

/* Genode/Virtualbox includes */
#include "sup.h"
#include "vmm.h"

/* VirtualBox includes */
#include <iprt/ldr.h>
#include <iprt/semaphore.h>
#include <VBox/err.h>


SUPR3DECL(SUPPAGINGMODE) SUPR3GetPagingMode(void)
{
	return sizeof(void *) == 4 ? SUPPAGINGMODE_32_BIT : SUPPAGINGMODE_AMD64_NX;
}


int SUPR3Term(bool) { return VINF_SUCCESS; }


int SUPR3HardenedLdrLoadAppPriv(const char *pszFilename, PRTLDRMOD phLdrMod,
                               uint32_t fFlags, PRTERRINFO pErrInfo)
{
	return RTLdrLoad(pszFilename, phLdrMod);
}


SUPR3DECL(int) SUPR3PageFreeEx(void *pvPages, size_t cPages)
{
	Genode::log(__func__, " pvPages=", pvPages, " pages=", cPages);
	return VINF_SUCCESS;
}


int SUPR3QueryMicrocodeRev(uint32_t *puMicrocodeRev)
{
	return E_FAIL;
}

uint32_t SUPSemEventMultiGetResolution(PSUPDRVSESSION)
{
	return 100000*10; /* called by 'vmR3HaltGlobal1Init' */
}


int SUPSemEventCreate(PSUPDRVSESSION pSession, PSUPSEMEVENT phEvent)
{
	return RTSemEventCreate((PRTSEMEVENT)phEvent);
}


int SUPSemEventClose(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent)
{
	Assert (hEvent);

	return RTSemEventDestroy((RTSEMEVENT)hEvent);
}


int SUPSemEventSignal(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent)
{
	Assert (hEvent);

	return RTSemEventSignal((RTSEMEVENT)hEvent);
}


int SUPSemEventWaitNoResume(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent,
                            uint32_t cMillies)
{
	Assert (hEvent);

	return RTSemEventWaitNoResume((RTSEMEVENT)hEvent, cMillies);
}


int SUPSemEventMultiCreate(PSUPDRVSESSION, PSUPSEMEVENTMULTI phEventMulti)
{
    RTSEMEVENTMULTI sem;

    /*
     * Input validation.
     */
    AssertPtrReturn(phEventMulti, VERR_INVALID_POINTER);

    /*
     * Create the event semaphore object.
     */
	int rc = RTSemEventMultiCreate(&sem);

	static_assert(sizeof(sem) == sizeof(*phEventMulti), "oi");
	*phEventMulti = reinterpret_cast<SUPSEMEVENTMULTI>(sem);
	return rc;
}


int SUPSemEventMultiWaitNoResume(PSUPDRVSESSION, SUPSEMEVENTMULTI event,
                                 uint32_t ms)
{
	RTSEMEVENTMULTI const rtevent = reinterpret_cast<RTSEMEVENTMULTI>(event);
	return RTSemEventMultiWait(rtevent, ms);
}

int SUPSemEventMultiSignal(PSUPDRVSESSION, SUPSEMEVENTMULTI event) {
	return RTSemEventMultiSignal(reinterpret_cast<RTSEMEVENTMULTI>(event)); }

int SUPSemEventMultiReset(PSUPDRVSESSION, SUPSEMEVENTMULTI event) {
	return RTSemEventMultiReset(reinterpret_cast<RTSEMEVENTMULTI>(event)); }

int SUPSemEventMultiClose(PSUPDRVSESSION, SUPSEMEVENTMULTI event) {
	return RTSemEventMultiDestroy(reinterpret_cast<RTSEMEVENTMULTI>(event)); }


int SUPR3CallVMMR0(PVMR0 pVMR0, VMCPUID idCpu, unsigned uOperation,
                   void *pvArg)
{
	if (uOperation == VMMR0_DO_CALL_HYPERVISOR) {
		Genode::log(__func__, ": VMMR0_DO_CALL_HYPERVISOR - doing nothing");
		return VINF_SUCCESS;
	}
	if (uOperation == VMMR0_DO_VMMR0_TERM) {
		Genode::log(__func__, ": VMMR0_DO_VMMR0_TERM - doing nothing");
		return VINF_SUCCESS;
	}
	if (uOperation == VMMR0_DO_GVMM_DESTROY_VM) {
		Genode::log(__func__, ": VMMR0_DO_GVMM_DESTROY_VM - doing nothing");
		return VINF_SUCCESS;
	}

	AssertMsg(uOperation != VMMR0_DO_VMMR0_TERM &&
	          uOperation != VMMR0_DO_CALL_HYPERVISOR &&
	          uOperation != VMMR0_DO_GVMM_DESTROY_VM,
	          ("SUPR3CallVMMR0: unhandled uOperation %d", uOperation));
	return VERR_GENERAL_FAILURE;
}


void genode_VMMR0_DO_GVMM_CREATE_VM(PSUPVMMR0REQHDR pReqHdr)
{
	GVMMCREATEVMREQ &req = reinterpret_cast<GVMMCREATEVMREQ &>(*pReqHdr);

	size_t const cCpus = req.cCpus;

	/*
	 * Allocate and initialize VM struct
	 *
	 * The VM struct is followed by the variable-sizedA array of VMCPU
	 * objects. 'RT_UOFFSETOF' is used to determine the size including
	 * the VMCPU array.
	 *
	 * VM struct must be page-aligned, which is checked at least in
	 * PDMR3CritSectGetNop().
	 */
	size_t const cbVM = RT_UOFFSETOF(VM, aCpus[cCpus]);

	static Genode::Attached_ram_dataspace vm(genode_env().ram(),
	                                         genode_env().rm(),
	                                         cbVM);
	Assert (vm.size() >= cbVM);

	VM *pVM = vm.local_addr<VM>();
	Genode::memset(pVM, 0, cbVM);

	/*
	 * On Genode, VMMR0 and VMMR3 share a single address space. Hence, the
	 * same pVM pointer is valid as pVMR0 and pVMR3.
	 */
	pVM->enmVMState       = VMSTATE_CREATING;
	pVM->pVMR0            = (RTHCUINTPTR)pVM;
	pVM->pVMRC            = (RTGCUINTPTR)pVM;
	pVM->pSession         = req.pSession;
	pVM->cbSelf           = cbVM;
	pVM->cCpus            = cCpus;
	pVM->uCpuExecutionCap = 100;  /* expected by 'vmR3CreateU()' */
	pVM->offVMCPU         = RT_UOFFSETOF(VM, aCpus);

	for (uint32_t i = 0; i < cCpus; i++) {
		pVM->aCpus[i].pVMR0           = pVM->pVMR0;
		pVM->aCpus[i].pVMR3           = pVM;
		pVM->aCpus[i].idHostCpu       = NIL_RTCPUID;
		pVM->aCpus[i].hNativeThreadR0 = NIL_RTNATIVETHREAD;
	}

	pVM->aCpus[0].hNativeThreadR0 = RTThreadNativeSelf();

	/* out parameters of the request */
	req.pVMR0 = pVM->pVMR0;
	req.pVMR3 = pVM;
}


void genode_VMMR0_DO_GVMM_REGISTER_VMCPU(PVMR0 pVMR0, VMCPUID idCpu)
{
	PVM pVM = reinterpret_cast<PVM>(pVMR0);
	pVM->aCpus[idCpu].hNativeThreadR0 = RTThreadNativeSelf();
}


HRESULT genode_check_memory_config(ComObjPtr<Machine>,
                                   size_t const memory_vmm)
{
	/* Request max available memory */
	size_t const memory_available = genode_env().pd().avail_ram().value;

	if (memory_vmm <= memory_available)
		return S_OK;

	Genode::error("Available memory too low to start the VM - available: ",
	              memory_vmm, "MB < ", memory_available, "MB requested");
	return E_FAIL;
}
