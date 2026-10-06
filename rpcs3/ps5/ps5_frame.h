#pragma once

#include "Emu/RSX/GSFrameBase.h"

// The PS5 has no windows: RSX renders to the TV through VK_KHR_display (VideoOut).
// This frame only reports the output size and passes the display handle.
class ps5_frame final : public GSFrameBase
{
public:
	ps5_frame(u32 width, u32 height) : m_width(width), m_height(height) {}

	void close() override {}
	void reset() override {}
	bool shown() override { return true; }
	void hide() override {}
	void show() override {}
	void toggle_fullscreen() override {}

	void delete_context(draw_context_t) override {}
	draw_context_t make_context() override { return nullptr; }
	void set_current(draw_context_t) override {}
	void flip(draw_context_t, bool) override {}
	int client_width() override { return static_cast<int>(m_width); }
	int client_height() override { return static_cast<int>(m_height); }
	f64 client_display_rate() override { return 60.0; }
	bool has_alpha() override { return false; }

	display_handle_t handle() const override { return ps5_display_t{m_width, m_height}; }

	bool can_consume_frame() const override { return false; }
	void present_frame(std::vector<u8>&&, u32, u32, u32, bool) const override {}
	void take_screenshot(std::vector<u8>&&, u32, u32, bool) override {}

	void update_title(double /*fps*/) override {}

private:
	u32 m_width;
	u32 m_height;
};
