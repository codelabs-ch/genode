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
