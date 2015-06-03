/*
 * \brief  Registry of VMM-local memory regions
 * \author Norman Feske
 * \date   2013-09-02
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#ifndef _VMM_MEMORY_H_
#define _VMM_MEMORY_H_

/*
 * Work-around for a naming conflict between the enum definition of PAGE_SIZE
 * in 'os/attached_ram_dataspace.h' and the VirtualBox #define with the same
 * name.
 */
#define BACKUP_PAGESIZE PAGE_SIZE
#undef  PAGE_SIZE

/* Genode includes */
#include <base/env.h>
#include <base/lock.h>
#include <util/list.h>
#include <os/attached_io_mem_dataspace.h>

#define PAGE_SIZE BACKUP_PAGESIZE

/* VirtualBox includes */
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/pdmdev.h>


class Vmm_memory
{
	struct Region;

	typedef Genode::Ram_session            Ram_session;
	typedef Genode::Rm_session             Rm_session;
	typedef Genode::size_t                 size_t;
	typedef Genode::addr_t                 addr_t;
	typedef Genode::Lock                   Lock;
	typedef Genode::Attached_io_mem_dataspace Attached_io_mem_dataspace;
	typedef Genode::List<Region>           Region_list;

	private:

		struct Region : Region_list::Element, Attached_io_mem_dataspace
		{
			PPDMDEVINS           pDevIns;
			unsigned const       iRegion;
			RTGCPHYS             vm_phys; 
			PFNPGMR3PHYSHANDLER  pfnHandlerR3;
			void                *pvUserR3;
			PGMPHYSHANDLERTYPE   enmType;
			size_t				 region_size;

			addr_t _phys_base()
			{
				struct Region_info
				{
					addr_t base;
					size_t size;
				};
				static unsigned counter;
				static Region_info regions[] = {
					/* RAM phys 0x0 */
					{ 0x800000000, 0x10000000 },
					/* pcbios phys 0xe1000 */
					{ 0x810000000, 0x1000 },
					/* pcbios phys 0xf0000*/
					{ 0x820000000, 0x10000 },
					/* pcbios 0xffff0000 */
					{ 0x830000000, 0x10000 },
					/* VMMDev */
					{ 0x840000000, 0x400000 },
					/* VMMDev */
					{ 0x850000000, 0x4000 },
					/* vga */
					{ 0x860000000, 0xc00000 },
					/* vga phys 0xc0000 */
					{ 0x870000000, 0x9000 },
					/* acpi phys e0000 */
					{ 0x880000000, 0x1000 },
					{ 0x0, 0x0 },
				};

				Region_info cur_region = regions[counter++];

				if (cur_region.size == 0)
				{
					PERR("Region size is zero!!!");
					return 0;
				}
				return cur_region.base;
			}

			size_t size() const { return region_size; }

			Region(Ram_session &ram, size_t size, PPDMDEVINS pDevIns,
			           unsigned iRegion)
			:
				Attached_io_mem_dataspace(_phys_base(), size),
				pDevIns(pDevIns),
				iRegion(iRegion),
				vm_phys(0), pfnHandlerR3(0), pvUserR3(0),
				region_size(size)
			{
				PDBG("Virt address=0x%p, size 0x%zx, dev=%s ",
						local_addr<char>(), size,
		               pDevIns && pDevIns->pReg ? pDevIns->pReg->szName : 0);
			}
		};

		Lock        _lock;
		Region_list _regions;

		/**
		 * Backing store
		 */
		Genode::Ram_session &_ram;

		Region *_lookup_unsynchronized(PPDMDEVINS pDevIns, unsigned iRegion)
		{
			for (Region *r = _regions.first(); r; r = r->next())
				if (r->pDevIns == pDevIns && r->iRegion == iRegion)
					return r;
			return 0;
		}

		Region *_lookup_unsynchronized(RTGCPHYS vm_phys, size_t size)
		{
			for (Region *r = _regions.first(); r; r = r->next())
				if (r->vm_phys && r->vm_phys <= vm_phys
				    && vm_phys - r->vm_phys < r->size()
				    && r->size() - (vm_phys - r->vm_phys) >= size)
					return r;

			return 0;
		}

	public:

		Vmm_memory(Ram_session &ram) : _ram(ram) { }

		/**
		 * \throw  Ram_session::Alloc_failed
		 * \throw  Rm_session::Attach_failed
		 */
		void *alloc(size_t cb, PPDMDEVINS pDevIns, unsigned iRegion)
		{
			Lock::Guard guard(_lock);

			try {
				Region *r = new (Genode::env()->heap())
				                Region(_ram, cb, pDevIns, iRegion);
				_regions.insert(r);

				return r->local_addr<void>();

			} catch (Ram_session::Alloc_failed) {
				PERR("Vmm_memory::alloc(0x%zx): RAM allocation failed", cb);
				throw;
			} catch (Rm_session::Attach_failed) {
				PERR("Vmm_memory::alloc(0x%zx): RM attach failed", cb);
				throw;
			}

			return 0;
		}

		void *alloc_rom(size_t cb, PPDMDEVINS pDevIns)
		{
			return alloc(cb, pDevIns, ~0U);
		}

		void *alloc_ram(size_t cb)
		{
			return alloc(cb, 0, ~0U);
		}

		bool add_handler(RTGCPHYS vm_phys, size_t size,
		                 PFNPGMR3PHYSHANDLER pfnHandlerR3, void *pvUserR3,
		                 PGMPHYSHANDLERTYPE enmType = PGMPHYSHANDLERTYPE_PHYSICAL_ALL)
		{
			Lock::Guard guard(_lock);

			Region *r = _lookup_unsynchronized(vm_phys, size);

			if (!r) return false;

			r->enmType      = enmType;
			r->pfnHandlerR3 = pfnHandlerR3;
			r->pvUserR3     = pvUserR3;

			return true;
		}

		void * lookup(RTGCPHYS vm_phys, size_t size,
		              PFNPGMR3PHYSHANDLER *ppfnHandlerR3 = 0,
		              void **ppvUserR3 = 0,
		              PGMPHYSHANDLERTYPE *enmType = 0)
		{
			Lock::Guard guard(_lock);

			Region *r = _lookup_unsynchronized(vm_phys, size);

			if (!r) return 0;

			if (enmType)       *enmType       = r->enmType;
			if (ppfnHandlerR3) *ppfnHandlerR3 = r->pfnHandlerR3;
			if (ppvUserR3)     *ppvUserR3     = r->pvUserR3;

			return reinterpret_cast<void *>(r->local_addr<uint8_t>() +
			                                (vm_phys - r->vm_phys));
		}

		bool lookup_range(RTGCPHYS &vm_phys, size_t &size)
		{
			Lock::Guard guard(_lock);

			Region *r = _lookup_unsynchronized(vm_phys, size);

			if (!r)
				return false;

			vm_phys = r->vm_phys;
			size    = r->size();

			return true;
		}

		size_t map_to_vm(PPDMDEVINS pDevIns, RTGCPHYS GCPhys,
		                 unsigned iRegion = ~0U)
		{
			Lock::Guard guard(_lock);

			Region *r = _lookup_unsynchronized(pDevIns, iRegion);

			if (r) r->vm_phys = GCPhys;

			return r ? r->size() : 0;
		}

		bool unmap_from_vm(RTGCPHYS GCPhys, size_t size, bool invalidate = false)
		{
			Lock::Guard guard(_lock);

			Region *r = _lookup_unsynchronized(GCPhys, size);
			if (!r) return false;

			bool result = revoke_from_vm(r);

			if (invalidate)
				r->vm_phys = 0ULL;

			return result;
		}

		/**
		 * Platform specific implemented.
		 */
		bool revoke_from_vm(Region *r);

		/**
		 * Revoke all memory (RAM or ROM) from VM
		 */
		void revoke_all()
		{
			Lock::Guard guard(_lock);

			for (Region *r = _regions.first(); r; r = r->next())
			{
				bool ok = revoke_from_vm(r);
				Assert(ok);
			}
		}
};


/**
 * Return pointer to singleton instance
 */
Vmm_memory *vmm_memory();


#endif /* _VMM_MEMORY_H_ */
