#include <base/printf.h>

#include "idt.h"

using namespace Genode;

extern uint64_t _isr_array[];

#pragma pack(1)
class Descriptor
{
	private:
		uint16_t _limit;
		uint64_t _base;

	public:
		Descriptor(uint16_t l, uint64_t b) : _limit(l), _base (b) {};
};
#pragma pack()

__attribute__((aligned(8))) Idt::gate Idt::_table[SIZE_IDT];


void Idt::setup()
{
	uint64_t *isrs = _isr_array;

	for (unsigned vec = 0; vec < SIZE_IDT; vec++, isrs++) {
		_table[vec].offset_15_00 = *isrs & 0xffff;
		_table[vec].segment_sel  = 8;
		_table[vec].flags        = 0x8e00;
		_table[vec].offset_31_16 = (*isrs & 0xffff0000) >> 16;
		_table[vec].offset_63_32 = (*isrs & 0xffffffff0000) >> 32;
	}
}


void Idt::load()
{
	asm volatile ("lidt %0" : : "m" (Descriptor (sizeof (_table) - 1,
				  reinterpret_cast<uint64_t>(_table))));
}


class CPU_regs {

	public:
		uint64_t _cr2, _rax, _rbx, _rcx, _rdx, _rdi, _rsi, _rbp, _r08, _r09, _r10,
				 _r11, _r12, _r13, _r14, _r15;

};


class ISR_context {
	public:
		CPU_regs _gpr;

		uint64_t _vector, _error_code, _rip, _cs, _rflags, _rsp, _ss;
};


extern "C" void dump_interrupt_info(ISR_context context)
{
	PERR("unhandled exception/interrupt occurred with vector %d",
		 (int)context._vector);

	PDBG("cr2 %16llx", context._gpr._cr2);
	PDBG("rip %16llx cs %04x", context._rip, (int)context._cs);
	PDBG("rsp %16llx ss %04x", context._rsp, (int)context._ss);
	PDBG("rax %16llx rbx %16llx rcx %16llx",
		 context._gpr._rax, context._gpr._rbx, context._gpr._rcx);
	PDBG("rdx %16llx rsi %16llx rdi %16llx",
		 context._gpr._rdx, context._gpr._rsi, context._gpr._rdi);
	PDBG("rbp %16llx r08 %16llx r09 %16llx",
		 context._gpr._rbp, context._gpr._r08, context._gpr._r09);
	PDBG("r10 %16llx r11 %16llx r12 %16llx",
		 context._gpr._r10, context._gpr._r11, context._gpr._r12);
	PDBG("r13 %16llx r14 %16llx r15 %16llx",
		 context._gpr._r13, context._gpr._r14, context._gpr._r15);
	PDBG("efl %16llx", context._rflags);

	while (true) {}
}
