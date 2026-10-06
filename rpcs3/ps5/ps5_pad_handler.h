#pragma once

#include "Emu/Io/PadHandler.h"
#include "ps5_sce.h"

#include <map>

// A DualSense through ScePad, one per signed-in user (players 1-4)
class ps5_pad_device : public PadDevice
{
public:
	s32 user = -1;
	s32 handle = -1;
	bool connected = false;
	ps5::pad_sample last{};
	u8 large_motor = 0;
	u8 small_motor = 0;
};

class ps5_pad_handler final : public PadHandlerBase
{
	enum key_codes : u32
	{
		None = 0,
		Cross, Circle, Square, Triangle,
		Left, Right, Up, Down,
		L1, R1, L3, R3,
		Options, Touchpad,
		L2, R2,
		LSXNeg, LSXPos, LSYNeg, LSYPos,
		RSXNeg, RSXPos, RSYNeg, RSYPos,
	};

public:
	ps5_pad_handler();
	~ps5_pad_handler() override;

	bool Init() override;
	void process() override;
	void init_config(cfg_pad* cfg) override;
	std::vector<pad_list_entry> list_devices() override;
	void SetPadData(const std::string& padId, u8 player_id, u8 large_motor, u8 small_motor, s32 r, s32 g, s32 b, bool player_led, bool battery_led, u32 battery_led_brightness) override;

	// "DualSense 1".."DualSense 4": player N is the Nth signed-in user's controller
	static std::string device_name(u32 index);

private:
	std::map<std::string, std::shared_ptr<ps5_pad_device>> m_devices;
	steady_clock::time_point m_last_scan{};

	void scan_users();

	std::shared_ptr<PadDevice> get_device(const std::string& device) override;
	PadHandlerBase::connection update_connection(const std::shared_ptr<PadDevice>& device) override;
	void apply_pad_data(const pad_ensemble& binding) override;
	bool get_is_left_trigger(const std::shared_ptr<PadDevice>& device, u32 keyCode) override;
	bool get_is_right_trigger(const std::shared_ptr<PadDevice>& device, u32 keyCode) override;
	bool get_is_left_stick(const std::shared_ptr<PadDevice>& device, u32 keyCode) override;
	bool get_is_right_stick(const std::shared_ptr<PadDevice>& device, u32 keyCode) override;
	std::unordered_map<u32, u16> get_button_values(const std::shared_ptr<PadDevice>& device) override;
	pad_preview_values get_preview_values(const std::unordered_map<u32, u16>& data, const std::vector<std::string>& buttons) override;
};
