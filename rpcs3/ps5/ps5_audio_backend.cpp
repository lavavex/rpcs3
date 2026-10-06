#include "stdafx.h"
#include "ps5_audio_backend.h"
#include "ps5_sce.h"

LOG_CHANNEL(ps5_audio_log, "SceAudioOut");

ps5_audio_backend::ps5_audio_backend()
{
	// sceAudioOutInit may be called more than once; anything but a failure is fine
	if (const int res = sceAudioOutInit(); res < 0 && res != static_cast<int>(0x8026000e))
	{
		ps5_audio_log.error("sceAudioOutInit() failed: 0x%x", res);
		return;
	}

	m_initialized = true;
}

ps5_audio_backend::~ps5_audio_backend()
{
	Close();
}

bool ps5_audio_backend::Open(std::string_view /*dev_id*/, AudioFreq freq, AudioSampleSize sample_size, AudioChannelCnt ch_cnt, audio_channel_layout layout)
{
	if (!m_initialized)
		return false;

	Close();
	std::lock_guard lock{m_cb_mutex};

	if (freq != AudioFreq::FREQ_48K)
	{
		ps5_audio_log.error("Only 48 kHz output is supported (requested %u Hz)", static_cast<u32>(freq));
		return false;
	}

	m_sampling_rate = freq;
	m_sample_size = sample_size;

	// The port is stereo; RPCS3 downmixes 5.1/7.1 to it
	setup_channel_layout(static_cast<u32>(ch_cnt), 2, layout, ps5_audio_log);

	const bool s16 = get_convert_to_s16();
	const u32 format = s16 ? ps5::AUDIO_OUT_S16_STEREO : ps5::AUDIO_OUT_FLOAT_STEREO;

	m_port = sceAudioOutOpen(ps5::AUDIO_OUT_USER_SYSTEM, 0, 0, grain, 48000, format);

	if (m_port < 0)
	{
		ps5_audio_log.error("sceAudioOutOpen() failed: 0x%x", m_port);
		m_port = -1;
		return false;
	}

	m_buffer.assign(grain * get_channels() * get_sample_size(), 0);
	ps5_audio_log.notice("Opened port %d: %s stereo, grain %u", m_port, s16 ? "s16" : "float", grain);

	m_stop = false;
	m_thread = std::make_unique<named_thread<std::function<void()>>>("SceAudioOut", [this]() { output_loop(); });
	return true;
}

void ps5_audio_backend::Close()
{
	if (m_thread)
	{
		m_stop = true;
		m_thread.reset(); // joins
	}

	std::lock_guard lock{m_cb_mutex};

	if (m_port >= 0)
	{
		sceAudioOutClose(m_port);
		m_port = -1;
	}

	m_playing = false;
}

void ps5_audio_backend::Play()
{
	std::lock_guard lock{m_cb_mutex};
	m_playing = true;
}

void ps5_audio_backend::Pause()
{
	std::lock_guard lock{m_cb_mutex};
	m_playing = false;
}

void ps5_audio_backend::output_loop()
{
	while (!m_stop && thread_ctrl::state() != thread_state::aborting)
	{
		u32 written = 0;
		const u32 bytes = ::size32(m_buffer);

		{
			std::unique_lock lock(m_cb_mutex, std::defer_lock);

			if (lock.try_lock_for(std::chrono::microseconds{50}) && m_write_callback && m_playing)
			{
				written = std::min(m_write_callback(bytes, m_buffer.data()), bytes);
			}
		}

		// Silence for what the callback did not fill
		std::memset(m_buffer.data() + written, 0, bytes - written);

		// Blocks until the port takes the grain
		sceAudioOutOutput(m_port, m_buffer.data());
	}

	// Flush: an output of null waits for the port to drain
	sceAudioOutOutput(m_port, nullptr);
}
