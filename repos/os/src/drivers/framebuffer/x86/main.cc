/*
 * \brief  Dummy frame-buffer driver
 * \author Stefan Kalkowski <stefan.kalkowski@genode-labs.com>
 * \date   2015-06-03
 */

/* Genode includes */
#include <framebuffer_session/framebuffer_session.h>
#include <cap_session/connection.h>
#include <dataspace/client.h>
#include <base/printf.h>
#include <base/sleep.h>
#include <os/static_root.h>
#include <os/config.h>

namespace Framebuffer {
	using namespace Genode;
	class Session_component;
};


class Framebuffer::Session_component :
	public Genode::Rpc_object<Framebuffer::Session>
{
	private:

		Mode                 _mode;
		size_t               _size;
		Dataspace_capability _ds;

	public:

		Session_component()
		: _mode(640, 480, Mode::RGB565),
		  _size(_mode.bytes_per_pixel() * _mode.width() * _mode.height()),
		  _ds(env()->ram_session()->alloc(_size)) { }


		/**************************************
		 **  Framebuffer::session interface  **
		 **************************************/

		Dataspace_capability dataspace() override { return _ds; }
		Mode mode() const override { return _mode; }
		void mode_sigh(Genode::Signal_context_capability) override { }
		void sync_sigh(Genode::Signal_context_capability sigh) override { }
		void refresh(int x, int y, int w, int h) override { }
};


int main(int, char **)
{
	Genode::printf("Starting dummy framebuffer driver\n");

	using namespace Framebuffer;

	enum { STACK_SIZE = 4096 };
	static Cap_connection cap;
	static Rpc_entrypoint ep(&cap, STACK_SIZE, "fb_ep");

	static Session_component fb_session;
	static Static_root<Framebuffer::Session> fb_root(ep.manage(&fb_session));

	env()->parent()->announce(ep.manage(&fb_root));

	sleep_forever();
	return 0;
}
