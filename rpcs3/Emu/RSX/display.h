#pragma once

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
// nothing
#elif defined(HAVE_X11)
// Cannot include Xlib.h before Qt
// and we don't need all of Xlib anyway
using Display = struct _XDisplay;
using Window  = unsigned long;
#endif

#ifdef HAVE_WAYLAND
#include <wayland-client.h>
#endif

#ifdef __PROSPERO__
// The PS5 has no windows: RADV presents to VideoOut through VK_KHR_display.
// The handle carries the output size the title asks for.
struct ps5_display_t
{
	unsigned width = 1920;
	unsigned height = 1080;
};
#endif

#ifdef _WIN32
using display_handle_t = HWND;
#elif defined(__APPLE__)
using display_handle_t = void*; // NSView
#else
#include <variant>
using display_handle_t = std::variant<
#if defined(HAVE_X11) && defined(HAVE_WAYLAND)
	std::pair<Display*, Window>, std::pair<wl_display*, wl_surface*>
#elif defined(HAVE_X11)
	std::pair<Display*, Window>
#elif defined(HAVE_WAYLAND)
	std::pair<wl_display*, wl_surface*>
#elif defined(ANDROID)
	struct ANativeWindow*
#elif defined(__PROSPERO__)
	struct ps5_display_t
#endif
>;
#endif

using draw_context_t = void*;
