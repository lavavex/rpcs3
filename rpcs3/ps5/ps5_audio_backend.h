#pragma once

#include "Emu/Audio/AudioBackend.h"
#include "Utilities/Thread.h"

#include <vector>

// Output through SceAudioOut: a thread pulls a grain from cellAudio's write callback and
// hands it to the port; sceAudioOutOutput blocks until the port takes it, which paces it.
class ps5_audio_backend final : public AudioBackend
{
public:
	ps5_audio_backend();
	~ps5_audio_backend() override;

	std::string_view GetName() const override { return "SceAudioOut"sv; }

	bool Initialized() override { return m_initialized; }
	bool Operational() override { return m_port >= 0; }

	bool Open(std::string_view dev_id, AudioFreq freq, AudioSampleSize sample_size, AudioChannelCnt ch_cnt, audio_channel_layout layout) override;
	void Close() override;

	f64 GetCallbackFrameLen() override { return static_cast<f64>(grain) / 48000.0; }

	void Play() override;
	void Pause() override;

private:
	static constexpr u32 grain = 256; // samples per sceAudioOutOutput

	bool m_initialized = false;
	s32 m_port = -1;
	std::vector<u8> m_buffer;
	std::unique_ptr<named_thread<std::function<void()>>> m_thread;
	atomic_t<bool> m_stop{false};

	void output_loop();
};
