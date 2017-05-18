/*
 * \brief   Implementation of "SHMStream Version 2 IPC Interface"
 *          https://git.codelabs.ch/?p=muen.git;a=tree;f=doc/shmstream
 * \date    2017-05-14
 * \author  Adrian-Ken Rueegsegger
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__SPEC__X86_64__SHMSTREAM__MUCHANNEL_H_
#define _INCLUDE__SPEC__X86_64__SHMSTREAM__MUCHANNEL_H_

#include <base/log.h>
#include <base/attached_io_mem_dataspace.h>
#include <cpu/memory_barrier.h>

namespace Muchannel
{
	enum {
		NULL_EPOCH  = 0,
		SHMSTREAM20 = 0x487312b6b79a9b6dULL,
	};

	struct Limit_exceeded {};
	struct Limit { Genode::uint64_t value; };
	struct Header;
	template <typename T> struct Channel;
	template <typename T> class Reader;
	template <typename T> class Writer;
}

struct Muchannel::Header {
	Genode::uint64_t transport;
	Genode::uint64_t epoch;
	Genode::uint64_t protocol;
	Genode::uint64_t size;
	Genode::uint64_t elements;
	Genode::uint64_t __reserved;
	Genode::uint64_t wsc;
	Genode::uint64_t wc;
} __attribute__((packed, aligned (8)));

template <typename T>
struct Muchannel::Channel {
	struct Header hdr;

	template <typename ... ARGS>
	void construct_at(Genode::uint64_t i, Limit limit, ARGS&&...args)
	{
		T *data_ptr = reinterpret_cast<T*>(this + 1);
		if (i > limit.value)
			throw Limit_exceeded();
		Genode::construct_at<T>(data_ptr + i, args...);
	}

	T const & element_at(Genode::uint64_t i, Limit limit)
	{
		T *data_ptr = reinterpret_cast<T*>(this + 1);
		if (i > limit.value)
			throw Limit_exceeded();
		return *(data_ptr + i);
	}
};

Genode::uint64_t atomic64_read (volatile Genode::uint64_t const *val)
{
	return __atomic_load_n(val, __ATOMIC_RELAXED);
}

void atomic64_set (Genode::uint64_t * const val, Genode::uint64_t value)
{
	__atomic_store_n(val, value, __ATOMIC_RELAXED);
}


template <typename T>
class Muchannel::Reader
{
	public:
		struct Incompatible_interface {};
		struct Epoch_changed {};
		struct Overrun_detected {};

	private:
		enum Result {
			INACTIVE,
			INCOMPATIBLE_INTERFACE,
			EPOCH_CHANGED,
			NO_DATA,
			OVERRUN_DETECTED,
			SUCCESS
		};

		Genode::uint64_t   _epoch;
		Genode::uint64_t   _protocol;
		Genode::uint64_t   _size;
		Genode::uint64_t   _elements;
		Genode::uint64_t   _rc;
		struct Channel<T> &_channel;

		bool is_active() { return  atomic64_read(&_channel.hdr.epoch) != NULL_EPOCH; }
		bool has_epoch_changed() { return _epoch != atomic64_read (&_channel.hdr.epoch); }

		void synchronize()
		{
			if (SHMSTREAM20 == atomic64_read(&_channel.hdr.transport) &&
				_protocol == atomic64_read(&_channel.hdr.protocol))
			{
				_epoch = atomic64_read(&_channel.hdr.epoch);
				_size = atomic64_read(&_channel.hdr.size);
				/* TODO: check size */
				_elements = atomic64_read(&_channel.hdr.elements);
				_rc = 0;

				Genode::log ("Muchannel reader: Epoch Changed.");
				throw Epoch_changed();
			} else {
				Genode::log ("Muchannel reader: Incompatible Interface.");
				throw Incompatible_interface();
			}
		}

	public:
		Reader (Genode::uint64_t protocol, void *channel_addr)
		:
			_epoch(NULL_EPOCH), _protocol(protocol), _size(0), _elements(0), _rc(0),
			_channel(*(Channel<T>*) channel_addr)
		{
			Genode::log ("Muchannel reader: listining at ", channel_addr);
		}

		template <typename FUNC>
		void for_each_element(FUNC const &func)
		{
			bool has_data = true;

			while (has_data)
			{
				if (is_active()) {
					if (has_epoch_changed())
						synchronize();

					if (_rc < atomic64_read(&_channel.hdr.wc))
					{
						T const elem(_channel.element_at(_rc % _elements,  Limit { _elements }));

						if (atomic64_read(&_channel.hdr.wsc) > _rc + _elements) {
							_rc = atomic64_read(&_channel.hdr.wc);
							Genode::log ("Muchannel reader: Overrun detected.");
							throw Overrun_detected();
						} else {
							_rc++;
							func(elem);
						}
						if (has_epoch_changed()) {
							Genode::log ("Muchannel reader: Epoch changed.");
							throw Epoch_changed();
						}
					} else {
						has_data = false;
					}
				} else {
					_epoch = NULL_EPOCH;
					has_data = false;
				}
			}
		}
};

template <typename T>
class Muchannel::Writer
{
	private:

		Genode::uint64_t   _epoch;
		Genode::uint64_t   _protocol;
		Genode::uint64_t   _size;
		Genode::uint64_t   _elements;
		struct Channel<T> &_channel;

	public:
		Writer (Genode::uint64_t protocol,
				void *channel_addr,
				Genode::uint64_t size,
				Genode::uint64_t epoch)
		:
			_epoch(epoch), _protocol(protocol), _size(size),
			_elements((_size - sizeof(Muchannel::Header)) / sizeof(T)),
			_channel(*(Channel<T>*) channel_addr)
		{
			deactivate();

			atomic64_set(&_channel.hdr.transport, SHMSTREAM20);
			atomic64_set(&_channel.hdr.protocol, _protocol);
			atomic64_set(&_channel.hdr.size, sizeof(T));
			atomic64_set(&_channel.hdr.elements, _elements);
			atomic64_set(&_channel.hdr.wsc, 0);
			atomic64_set(&_channel.hdr.wc, 0);

			atomic64_set(&_channel.hdr.epoch, epoch);
		}

		void deactivate()
		{
			atomic64_set(&_channel.hdr.epoch, NULL_EPOCH);
		}

		template <typename... ARGS>
		void write (ARGS&&... args)
		{
			Genode::uint64_t wc, pos, tmp;

			wc  = atomic64_read(&_channel.hdr.wc);
			tmp = wc;
			pos = tmp % _elements;
			wc  = wc + 1;

			atomic64_set(&_channel.hdr.wsc, wc);
			_channel.construct_at(pos, Limit { _elements }, args...);
			atomic64_set(&_channel.hdr.wc, wc);
		}
};

#endif /* _INCLUDE__SPEC__X86_64__SHMSTREAM__MUCHANNEL_H_ */
