#include "stdafx.h"
#include "ps5_pad_handler.h"
#include "Emu/Io/pad_config.h"

LOG_CHANNEL(ps5_pad_log, "PS5 Pad");

ps5_pad_handler::ps5_pad_handler() : PadHandlerBase(pad_handler::scepad)
{
	button_list =
	{
		{ None,     ""         },
		{ Cross,    "Cross"    },
		{ Circle,   "Circle"   },
		{ Square,   "Square"   },
		{ Triangle, "Triangle" },
		{ Left,     "Left"     },
		{ Right,    "Right"    },
		{ Up,       "Up"       },
		{ Down,     "Down"     },
		{ L1,       "L1"       },
		{ R1,       "R1"       },
		{ L3,       "L3"       },
		{ R3,       "R3"       },
		{ Options,  "Options"  },
		{ Touchpad, "Touchpad" },
		{ L2,       "L2"       },
		{ R2,       "R2"       },
		{ LSXNeg,   "LS X-"    },
		{ LSXPos,   "LS X+"    },
		{ LSYPos,   "LS Y+"    },
		{ LSYNeg,   "LS Y-"    },
		{ RSXNeg,   "RS X-"    },
		{ RSXPos,   "RS X+"    },
		{ RSYPos,   "RS Y+"    },
		{ RSYNeg,   "RS Y-"    },
	};

	init_configs();

	// The DualSense's sticks and triggers are 0..255, as DS4/DualSense over HID
	thumb_max = 255;
	trigger_min = 0;
	trigger_max = 255;

	b_has_config = true;
	b_has_deadzones = true;
	b_has_rumble = true;
	b_has_led = true;
	b_has_rgb = true;

	m_trigger_threshold = trigger_max / 2;
	m_thumb_threshold = thumb_max / 2;
}

ps5_pad_handler::~ps5_pad_handler()
{
	for (auto& [name, dev] : m_devices)
	{
		if (dev && dev->handle >= 0)
		{
			scePadClose(dev->handle);
			dev->handle = -1;
		}
	}
}

std::string ps5_pad_handler::device_name(u32 index)
{
	return fmt::format("DualSense %u", index + 1);
}

void ps5_pad_handler::init_config(cfg_pad* cfg)
{
	if (!cfg) return;

	cfg->ls_left.def  = ::at32(button_list, LSXNeg);
	cfg->ls_down.def  = ::at32(button_list, LSYNeg);
	cfg->ls_right.def = ::at32(button_list, LSXPos);
	cfg->ls_up.def    = ::at32(button_list, LSYPos);
	cfg->rs_left.def  = ::at32(button_list, RSXNeg);
	cfg->rs_down.def  = ::at32(button_list, RSYNeg);
	cfg->rs_right.def = ::at32(button_list, RSXPos);
	cfg->rs_up.def    = ::at32(button_list, RSYPos);
	cfg->start.def    = ::at32(button_list, Options);
	// The DualSense has no Select: the touchpad click stands in for it
	cfg->select.def   = ::at32(button_list, Touchpad);
	// The PS button belongs to the system: Options + touchpad opens RPCS3's home menu
	cfg->ps.def       = cfg_pad::make_button_string(button_list, {{Options, Touchpad}});
	cfg->square.def   = ::at32(button_list, Square);
	cfg->cross.def    = ::at32(button_list, Cross);
	cfg->circle.def   = ::at32(button_list, Circle);
	cfg->triangle.def = ::at32(button_list, Triangle);
	cfg->left.def     = ::at32(button_list, Left);
	cfg->down.def     = ::at32(button_list, Down);
	cfg->right.def    = ::at32(button_list, Right);
	cfg->up.def       = ::at32(button_list, Up);
	cfg->r1.def       = ::at32(button_list, R1);
	cfg->r2.def       = ::at32(button_list, R2);
	cfg->r3.def       = ::at32(button_list, R3);
	cfg->l1.def       = ::at32(button_list, L1);
	cfg->l2.def       = ::at32(button_list, L2);
	cfg->l3.def       = ::at32(button_list, L3);

	cfg->pressure_intensity_button.def = ::at32(button_list, None);
	cfg->analog_limiter_button.def = ::at32(button_list, None);
	cfg->orientation_reset_button.def = ::at32(button_list, None);

	cfg->lstick_anti_deadzone.def = static_cast<u32>(0.13 * thumb_max); // 13%
	cfg->rstick_anti_deadzone.def = static_cast<u32>(0.13 * thumb_max); // 13%
	cfg->lstickdeadzone.def    = 40; // between 0 and 255
	cfg->rstickdeadzone.def    = 40; // between 0 and 255
	cfg->ltriggerthreshold.def = 0;  // between 0 and 255
	cfg->rtriggerthreshold.def = 0;  // between 0 and 255

	cfg->colorR.def = 0;
	cfg->colorG.def = 0;
	cfg->colorB.def = 20;

	cfg->from_default();
}

bool ps5_pad_handler::Init()
{
	if (m_is_init)
		return true;

	if (const int res = scePadInit(); res < 0)
	{
		ps5_pad_log.error("scePadInit() failed: 0x%x", res);
		return false;
	}

	for (u32 i = 0; i < 4; i++)
	{
		auto dev = std::make_shared<ps5_pad_device>();
		m_devices.emplace(device_name(i), dev);
	}

	m_is_init = true;
	scan_users();
	return true;
}

// Player N is the Nth signed-in user. Users come and go; check about once a second.
void ps5_pad_handler::scan_users()
{
	m_last_scan = steady_clock::now();

	sce_user_list list{{-1, -1, -1, -1}};
	if (sceUserServiceGetLoginUserIdList(&list) < 0)
	{
		s32 initial = -1;
		if (sceUserServiceGetInitialUser(&initial) < 0)
			return;
		list.user_id[0] = initial;
	}

	for (u32 i = 0; i < 4; i++)
	{
		auto& dev = ::at32(m_devices, device_name(i));
		const s32 user = list.user_id[i];

		if (dev->user == user && dev->handle >= 0)
			continue;

		if (dev->handle >= 0)
		{
			ps5_pad_log.notice("Player %u: user %d signed out", i + 1, dev->user);
			scePadClose(dev->handle);
			dev->handle = -1;
			dev->connected = false;
		}

		dev->user = user;

		if (user < 0)
			continue;

		s32 handle = scePadOpen(user, 0, 0, nullptr);
		if (handle < 0)
		{
			// Already open in this process
			handle = scePadGetHandle(user, 0, 0);
		}

		if (handle < 0)
		{
			ps5_pad_log.error("Player %u: scePadOpen(user=%d) failed: 0x%x", i + 1, user, handle);
			continue;
		}

		dev->handle = handle;
		scePadSetVibrationMode(handle, ps5::PAD_VIBRATION_COMPATIBLE);
		ps5_pad_log.notice("Player %u: user %d, pad handle %d", i + 1, user, handle);
	}
}

void ps5_pad_handler::process()
{
	if (!m_is_init)
		return;

	if (steady_clock::now() - m_last_scan > std::chrono::seconds(1))
	{
		scan_users();
	}

	PadHandlerBase::process();
}

std::vector<pad_list_entry> ps5_pad_handler::list_devices()
{
	std::vector<pad_list_entry> pads;

	if (!Init())
		return pads;

	for (const auto& [name, dev] : m_devices)
	{
		pads.emplace_back(name, false);
	}

	return pads;
}

std::shared_ptr<PadDevice> ps5_pad_handler::get_device(const std::string& device)
{
	if (!Init())
		return nullptr;

	if (auto it = m_devices.find(device); it != m_devices.end())
		return it->second;

	return nullptr;
}

PadHandlerBase::connection ps5_pad_handler::update_connection(const std::shared_ptr<PadDevice>& device)
{
	auto* dev = static_cast<ps5_pad_device*>(device.get());
	if (!dev || dev->handle < 0)
		return connection::disconnected;

	// Drain the samples since the last read; the newest one counts
	ps5::pad_sample samples[16];
	const int count = scePadRead(dev->handle, samples, 16);

	if (count < 0)
	{
		dev->connected = false;
		return connection::disconnected;
	}

	if (count == 0)
		return dev->connected ? connection::no_data : connection::disconnected;

	const ps5::pad_sample& s = samples[count - 1];
	dev->connected = s.connected != 0;

	if (!dev->connected)
		return connection::disconnected;

	if (s.buttons & ps5::PAD_INTERCEPTED)
	{
		// The system menu has the pad: report nothing pressed
		dev->last = {};
		dev->last.left_x = dev->last.left_y = dev->last.right_x = dev->last.right_y = 128;
		return connection::connected;
	}

	dev->last = s;
	return connection::connected;
}

std::unordered_map<u32, u16> ps5_pad_handler::get_button_values(const std::shared_ptr<PadDevice>& device)
{
	std::unordered_map<u32, u16> values;
	const auto* dev = static_cast<ps5_pad_device*>(device.get());
	if (!dev)
		return values;

	const ps5::pad_sample& s = dev->last;
	const auto bit = [&](u32 mask) -> u16 { return (s.buttons & mask) ? 255 : 0; };

	values[Cross]    = bit(ps5::PAD_CROSS);
	values[Circle]   = bit(ps5::PAD_CIRCLE);
	values[Square]   = bit(ps5::PAD_SQUARE);
	values[Triangle] = bit(ps5::PAD_TRIANGLE);
	values[Left]     = bit(ps5::PAD_LEFT);
	values[Right]    = bit(ps5::PAD_RIGHT);
	values[Up]       = bit(ps5::PAD_UP);
	values[Down]     = bit(ps5::PAD_DOWN);
	values[L1]       = bit(ps5::PAD_L1);
	values[R1]       = bit(ps5::PAD_R1);
	values[L3]       = bit(ps5::PAD_L3);
	values[R3]       = bit(ps5::PAD_R3);
	values[Options]  = bit(ps5::PAD_OPTIONS);
	values[Touchpad] = bit(ps5::PAD_TOUCH_PAD);
	values[L2]       = s.l2;
	values[R2]       = s.r2;

	// Sticks: 0 = left/up, 255 = right/down (DS4 conventions)
	values[LSXNeg] = Clamp0To255((127.5f - s.left_x) * 2.0f);
	values[LSXPos] = Clamp0To255((s.left_x - 127.5f) * 2.0f);
	values[LSYNeg] = Clamp0To255((s.left_y - 127.5f) * 2.0f);
	values[LSYPos] = Clamp0To255((127.5f - s.left_y) * 2.0f);
	values[RSXNeg] = Clamp0To255((127.5f - s.right_x) * 2.0f);
	values[RSXPos] = Clamp0To255((s.right_x - 127.5f) * 2.0f);
	values[RSYNeg] = Clamp0To255((s.right_y - 127.5f) * 2.0f);
	values[RSYPos] = Clamp0To255((127.5f - s.right_y) * 2.0f);

	return values;
}

pad_preview_values ps5_pad_handler::get_preview_values(const std::unordered_map<u32, u16>& data, const std::vector<std::string>& /*buttons*/)
{
	return {
		::at32(data, L2),
		::at32(data, R2),
		::at32(data, LSXPos) - ::at32(data, LSXNeg),
		::at32(data, LSYPos) - ::at32(data, LSYNeg),
		::at32(data, RSXPos) - ::at32(data, RSXNeg),
		::at32(data, RSYPos) - ::at32(data, RSYNeg)
	};
}

bool ps5_pad_handler::get_is_left_trigger(const std::shared_ptr<PadDevice>& /*device*/, u32 keyCode)
{
	return keyCode == L2;
}

bool ps5_pad_handler::get_is_right_trigger(const std::shared_ptr<PadDevice>& /*device*/, u32 keyCode)
{
	return keyCode == R2;
}

bool ps5_pad_handler::get_is_left_stick(const std::shared_ptr<PadDevice>& /*device*/, u32 keyCode)
{
	return keyCode >= LSXNeg && keyCode <= LSYPos;
}

bool ps5_pad_handler::get_is_right_stick(const std::shared_ptr<PadDevice>& /*device*/, u32 keyCode)
{
	return keyCode >= RSXNeg && keyCode <= RSYPos;
}

void ps5_pad_handler::SetPadData(const std::string& padId, u8 /*player_id*/, u8 large_motor, u8 small_motor, s32 r, s32 g, s32 b, bool /*player_led*/, bool /*battery_led*/, u32 /*battery_led_brightness*/)
{
	auto it = m_devices.find(padId);
	if (it == m_devices.end() || it->second->handle < 0)
		return;

	const u8 motors[2] = { large_motor, small_motor };
	scePadSetVibration(it->second->handle, motors);

	if (r >= 0 && g >= 0 && b >= 0)
	{
		const u8 color[4] = { static_cast<u8>(r), static_cast<u8>(g), static_cast<u8>(b), 0 };
		scePadSetLightBar(it->second->handle, color);
	}
}

void ps5_pad_handler::apply_pad_data(const pad_ensemble& binding)
{
	const auto& pad = binding.pad;
	auto* dev = static_cast<ps5_pad_device*>(binding.device.get());

	if (!dev || !pad || dev->handle < 0)
		return;

	cfg_pad* config = dev->config;
	if (!config)
		return;

	// Rumble, as the generic handlers compute it
	const u8 speed_large = config->get_large_motor_speed(pad->m_vibrate_motors);
	const u8 speed_small = config->get_small_motor_speed(pad->m_vibrate_motors);

	if (speed_large != dev->large_motor || speed_small != dev->small_motor)
	{
		dev->large_motor = speed_large;
		dev->small_motor = speed_small;

		const u8 motors[2] = { speed_large, speed_small };
		scePadSetVibration(dev->handle, motors);
	}
}
