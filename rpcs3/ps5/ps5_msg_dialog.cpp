#include "stdafx.h"
#include "ps5_msg_dialog.h"

#include <cstring>

LOG_CHANNEL(ps5_ui_log, "PS5 UI");

extern "C"
{
	struct ps5_notify_request
	{
		char reserved[45];
		char message[3075];
	};

	int sceKernelSendNotificationRequest(int device, ps5_notify_request* request, size_t size, int blocking);
}

void ps5_msg_dialog::notify(const std::string& text)
{
	ps5_notify_request request{};
	std::snprintf(request.message, sizeof(request.message), "%s", text.c_str());
	sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

void ps5_msg_dialog::Create(const std::string& msg, const std::string& /*title*/)
{
	state = MsgDialogState::Open;
	{
		std::lock_guard lock(m_mutex);
		m_msg = msg;
	}
	update(true);
}

void ps5_msg_dialog::Close(bool success)
{
	state = MsgDialogState::Close;

	if (on_close)
	{
		on_close(success ? CELL_MSGDIALOG_BUTTON_OK : CELL_MSGDIALOG_BUTTON_ESCAPE);
	}
}

void ps5_msg_dialog::SetMsg(const std::string& msg)
{
	{
		std::lock_guard lock(m_mutex);
		m_msg = msg;
	}
	update(false);
}

void ps5_msg_dialog::ProgressBarSetMsg(u32 index, const std::string& msg)
{
	if (index >= 2) return;
	{
		std::lock_guard lock(m_mutex);
		m_bar_msg[index] = msg;
	}
	update(false);
}

void ps5_msg_dialog::ProgressBarReset(u32 index)
{
	if (index >= 2) return;
	std::lock_guard lock(m_mutex);
	m_value[index] = 0;
}

void ps5_msg_dialog::ProgressBarInc(u32 index, u32 delta)
{
	if (index >= 2) return;
	{
		std::lock_guard lock(m_mutex);
		m_value[index] += delta;
	}
	update(false);
}

void ps5_msg_dialog::ProgressBarSetValue(u32 index, u32 value)
{
	if (index >= 2) return;
	{
		std::lock_guard lock(m_mutex);
		m_value[index] = value;
	}
	update(false);
}

void ps5_msg_dialog::ProgressBarSetLimit(u32 index, u32 limit)
{
	if (index >= 2) return;
	std::lock_guard lock(m_mutex);
	m_limit[index] = limit;
}

void ps5_msg_dialog::update(bool force)
{
	std::string text;
	{
		std::lock_guard lock(m_mutex);

		// One line: the dialog's message (the first line of it), then the bar's text and percentage
		const std::string head = m_msg.substr(0, m_msg.find('\n'));
		const u32 limit = m_limit[0];
		const u32 value = std::min(m_value[0], limit);
		const u32 tenth = limit ? value * 10 / limit : 0;

		text = "RPCS3: " + head;

		if (!m_bar_msg[0].empty())
		{
			text += "\n" + m_bar_msg[0].substr(0, m_bar_msg[0].find('\n'));
		}

		if (limit)
		{
			fmt::append(text, "\n%u%%", value * 100 / limit);
		}

		// Throttle: a new message, a new tenth of the progress, or every 15 s
		const auto now = std::chrono::steady_clock::now();
		const bool new_text = head != m_last_text;
		const bool new_tenth = tenth != m_last_tenth;
		const bool stale = now - m_last_sent > std::chrono::seconds(15);

		if (!force && !new_text && !new_tenth && !stale)
		{
			return;
		}

		if (!force && now - m_last_sent < std::chrono::seconds(3))
		{
			return;
		}

		m_last_text = head;
		m_last_tenth = tenth;
		m_last_sent = now;
	}

	ps5_ui_log.notice("%s", text);
	notify(text);
}
