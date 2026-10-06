#pragma once

// Console services the PS5 frontend calls. No SDK header declares them; the
// signatures and the pad sample layout are the ones PS5_VulkanTemplate
// (ps5/src/platform.c) runs on the console.

#include <cstddef>
#include <cstdint>

extern "C"
{
	int sceSystemServiceHideSplashScreen(void);
	int sceSystemServiceLoadExec(const char* path, const char* const* argv);
	int sceUserServiceInitialize(const void* params);
	int sceUserServiceGetInitialUser(int32_t* user_id);

	struct sce_user_list
	{
		int32_t user_id[4]; // unused entries are -1
	};
	int sceUserServiceGetLoginUserIdList(sce_user_list* list);

	int scePadInit(void);
	int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void* params);
	int scePadGetHandle(int32_t user_id, int32_t port_type, int32_t index);
	int scePadRead(int32_t handle, void* samples, int32_t capacity);
	int scePadClose(int32_t handle);
	int scePadSetVibrationMode(int32_t handle, int32_t mode);
	int scePadSetVibration(int32_t handle, const void* vibration);
	int scePadSetLightBar(int32_t handle, const void* color);

	int sceAudioOutInit(void);
	int sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t grain, uint32_t rate, uint32_t format);
	int sceAudioOutOutput(int32_t port, const void* samples);
	int sceAudioOutClose(int32_t port);

	int sceKernelUsleep(uint32_t microseconds);
}

namespace ps5
{
	// One pad sample as the console writes it (120 bytes)
	struct pad_sample
	{
		uint32_t buttons;
		uint8_t left_x, left_y, right_x, right_y, l2, r2;
		uint8_t reserved[66];
		int32_t connected;
		uint64_t timestamp_us;
		uint8_t extension[16];
		uint8_t connected_count;
		uint8_t remaining[15];
	};
	static_assert(sizeof(pad_sample) == 120);
	static_assert(offsetof(pad_sample, connected) == 0x4c);

	enum pad_button : uint32_t
	{
		PAD_L3 = 0x000002,
		PAD_R3 = 0x000004,
		PAD_OPTIONS = 0x000008,
		PAD_UP = 0x000010,
		PAD_RIGHT = 0x000020,
		PAD_DOWN = 0x000040,
		PAD_LEFT = 0x000080,
		PAD_L2 = 0x000100,
		PAD_R2 = 0x000200,
		PAD_L1 = 0x000400,
		PAD_R1 = 0x000800,
		PAD_TRIANGLE = 0x001000,
		PAD_CIRCLE = 0x002000,
		PAD_CROSS = 0x004000,
		PAD_SQUARE = 0x008000,
		PAD_TOUCH_PAD = 0x100000,
		PAD_INTERCEPTED = 0x80000000u, // the system has the pad (PS button menu)
	};

	// Two-motor rumble (the other mode is the DualSense's haptics)
	constexpr int32_t PAD_VIBRATION_COMPATIBLE = 2;

	// sceAudioOutOpen
	constexpr int32_t AUDIO_OUT_USER_SYSTEM = 0xff;
	constexpr uint32_t AUDIO_OUT_S16_STEREO = 1;
	constexpr uint32_t AUDIO_OUT_FLOAT_STEREO = 4;
}
