/*
 * \brief  Input service test program
 * \author Christian Helmuth
 * \date   2010-06-15
 */

/*
 * Copyright (C) 2010-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <timer_session/connection.h>
#include <input_session/connection.h>
#include <base/attached_io_mem_dataspace.h>
#include <rom_session/connection.h>
#include <shmstream/muchannel.h>
#include <muen/sinfo.h>

using namespace Genode;

namespace Input
{
	enum Shmstream_protocol {
		MUEN_PROTO_INPUT = 0x9a0a8679dbc22dcbULL
	};

	enum Muen_event_type {
		MUEN_EV_RESET = 0,
		MUEN_EV_MOTION,
		MUEN_EV_WHEEL,
		MUEN_EV_PRESS,
		MUEN_EV_RELEASE,
	};

	Muen_event_type to_muen_event_type (Input::Event::Type type);

	struct Muen_input_event {
		Genode::uint32_t const event_type;
		Genode::uint32_t const keycode;       /* KEY_* value, as specified in linux/input.h */
		Genode::int32_t  const rel_x;         /* Relative pointer motion on X-Axis          */
		Genode::int32_t  const rel_y;         /* Relative pointer motion on Y-Axis          */
		Genode::uint32_t const led_state;     /* State of keyboard LEDs                     */
		Genode::uint32_t const key_count;     /* Number of key repetitions                  */

		Muen_input_event(Input::Event const &ev, int key_count)
		: event_type(to_muen_event_type(ev.type())), keycode(ev.code()),
		  rel_x(ev.rx()), rel_y(ev.ry()), led_state(0), key_count(key_count)
		{
		}

		Muen_input_event(Muen_input_event const &ev)
		: event_type(ev.event_type), keycode(ev.keycode), rel_x(ev.rel_x), rel_y(ev.rel_y), led_state(ev.led_state), key_count(ev.key_count)
		{
		}

	} __attribute__((packed));
};

static void trigger_event(unsigned Event_nr)
{
	asm volatile ("vmcall" ::"a"(Event_nr));
}

Input::Muen_event_type Input::to_muen_event_type (Input::Event::Type type)
{
	switch (type) {
		case Event::MOTION:	 return MUEN_EV_MOTION;
		case Event::WHEEL:	 return MUEN_EV_WHEEL;
		case Event::PRESS:	 return MUEN_EV_PRESS;
		case Event::RELEASE: return MUEN_EV_RELEASE;
		case Event::FOCUS:
		case Event::LEAVE:
		case Event::TOUCH:
		case Event::CHARACTER:
		case Event::INVALID:
			return MUEN_EV_RESET;
	}
	return MUEN_EV_RESET;
}

static char const * ev_type(Input::Event::Type type)
{
	switch (type) {
	case Input::Event::INVALID:   return "INVALID";
	case Input::Event::MOTION:    return "MOTION ";
	case Input::Event::PRESS:     return "PRESS  ";
	case Input::Event::RELEASE:   return "RELEASE";
	case Input::Event::WHEEL:     return "WHEEL  ";
	case Input::Event::FOCUS:     return "FOCUS  ";
	case Input::Event::LEAVE:     return "LEAVE  ";
	case Input::Event::TOUCH:     return "TOUCH  ";
	case Input::Event::CHARACTER: return "CHARACTER";
	}

	return "";
}

static char const * muen_ev_type(const Input::Muen_event_type type)
{
	switch (type) {
	case Input::MUEN_EV_RESET:     return "RESET  ";
	case Input::MUEN_EV_MOTION:    return "MOTION ";
	case Input::MUEN_EV_PRESS:     return "PRESS  ";
	case Input::MUEN_EV_RELEASE:   return "RELEASE";
	case Input::MUEN_EV_WHEEL:     return "WHEEL  ";
	}

	return "";
}

static char const * key_name(Input::Event const &ev)
{
	if (ev.type() == Input::Event::MOTION)
		return "";

	return Input::key_name(static_cast<Input::Keycode>(ev.code()));
}

struct Main
{
	class Missing_input_channel {};
	class Missing_channel_event {};

	Env					   &env;
	Input::Connection		input      { env };
	Signal_handler<Main>	input_sigh { env.ep(), *this, &Main::handle_input };
	unsigned				event_cnt  { 0 };

	Genode::Sinfo::Channel_info		  channel_info;
	Genode::Attached_io_mem_dataspace input_ds;

	Muchannel::Writer<Input::Muen_input_event> input_channel;
	Muchannel::Reader<Input::Muen_input_event> read_channel;

	Genode::Sinfo::Channel_info get_channel_info(Env &env);
	void handle_input();

	Main(Env &env)
	: env(env), channel_info(get_channel_info(env)),
	  input_ds (env, channel_info.address, channel_info.size, true),
	  input_channel(Input::MUEN_PROTO_INPUT, input_ds.local_addr<void>(),
					channel_info.size, 1),
	  read_channel(Input::MUEN_PROTO_INPUT, input_ds.local_addr<void>())
	{
		if (!channel_info.has_event)
		{
			Genode::error("input channel has no event");
			throw Missing_channel_event();
		}
		log("--- Input test ---");
		input.sigh(input_sigh);
	}
};

Genode::Sinfo::Channel_info Main::get_channel_info(Env &env)
{
	Genode::Sinfo::Channel_info chan_info;
	Genode::Rom_connection sinfo_rom(env, "subject_info_page");
	Genode::Sinfo sinfo ((addr_t)env.rm().attach (sinfo_rom.dataspace()));
	if (!sinfo.get_channel_info("input_events", &chan_info)) {
		Genode::error("unable to retrieve input events channel info");
		throw Missing_input_channel();
	}

	return chan_info;
}

void Main::handle_input()
{
	int key_cnt = 0;
	int in_cnt = 0;
	input.for_each_event([&] (Input::Event const &ev) {
		event_cnt++;

		if (ev.type() == Input::Event::PRESS)   key_cnt++;
		if (ev.type() == Input::Event::RELEASE) key_cnt--;

		log("Input event #", event_cnt,          "\t"
		    "type=",         ev_type(ev.type()), "\t"
		    "code=",         ev.code(),          "\t"
		    "rx=",           ev.rx(),            "\t"
		    "ry=",           ev.ry(),            "\t"
		    "ax=",           ev.ax(),            "\t"
		    "ay=",           ev.ay(),            "\t"
		    "key_cnt=",      key_cnt,            "\t", key_name(ev));
		input_channel.write(ev, key_cnt);
		trigger_event(channel_info.event_number);
		try {
			read_channel.for_each_element([&] (Input::Muen_input_event const &mev) {
					in_cnt++;
					log("type=", muen_ev_type(Input::Muen_event_type(mev.event_type)));
					log("In count ", in_cnt);
			});
		} catch (Muchannel::Reader<Input::Muen_input_event>::Epoch_changed) {
			;
		} catch (...) {
			Genode::log ("Error reading from channel");
			throw;
		}
	});
}


void Component::construct(Env &env) { static Main main(env); }
