#pragma once

#include "Emu/Cell/Modules/cellMsgDialog.h"

#include <array>
#include <chrono>
#include <mutex>
#include <string>

// Before the renderer is up (PPU/SPU compilation at boot) nothing can draw RPCS3's progress
// dialog: this one shows it as PS5 system notifications instead, throttled.
// In game, RPCS3's native overlays draw dialogs; this only reports.
class ps5_msg_dialog final : public MsgDialogBase
{
public:
	void Create(const std::string& msg, const std::string& title = "") override;
	void Close(bool success) override;
	void SetMsg(const std::string& msg) override;
	void ProgressBarSetMsg(u32 index, const std::string& msg) override;
	void ProgressBarReset(u32 index) override;
	void ProgressBarInc(u32 index, u32 delta) override;
	void ProgressBarSetValue(u32 index, u32 value) override;
	void ProgressBarSetLimit(u32 index, u32 limit) override;

	// A one-off notification (also used by the frontend for its own status)
	static void notify(const std::string& text);

private:
	std::mutex m_mutex;
	std::string m_msg;
	std::array<std::string, 2> m_bar_msg{};
	std::array<u32, 2> m_value{};
	std::array<u32, 2> m_limit{};
	u32 m_last_tenth = umax;
	std::string m_last_text;
	std::chrono::steady_clock::time_point m_last_sent{};

	void update(bool force);
};
