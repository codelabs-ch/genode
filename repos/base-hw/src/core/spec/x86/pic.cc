#include <pic.h>

using namespace Genode;

enum {
	PIC1         = 0x20,      /* IO base address for master PIC */
	PIC2         = 0xa0,      /* IO base address for slave PIC  */
	PIC1_COMMAND = PIC1,
	PIC1_DATA    = (PIC1+1),
	PIC2_COMMAND = PIC2,
	PIC2_DATA    = (PIC2+1),
	PIC_EOI      = 0x20,      /* End-of-interrupt command code  */
	ICW1_INIT    = 0x10,      /* Initialization                 */
	ICW1_ICW4    = 0x01,      /* Enable ICW4                    */
	ICW4_8086    = 0x01,      /* 8086/88 (MCS-80/85) mode       */
};

Pic::Pic() { }
