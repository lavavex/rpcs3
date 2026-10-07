// RPCS3 as a native PS5 title.
//
// No Qt: this is the frontend. It sets RPCS3's folders under /data/rpcs3, fills the emulator
// callbacks (headless_application and main_application without Qt), installs the firmware
// from /data/rpcs3/PS3UPDAT.PUP when it is missing, and boots what /data/rpcs3/boot.txt names.
// The main thread then serves RPCS3's call_from_main_thread queue until emulation stops.

#include "stdafx.h"

#include "ps5_audio_backend.h"
#include "ps5_firmware.h"
#include "ps5_frame.h"
#include "ps5_pad_handler.h"
#include "ps5_sce.h"

#include "rpcs3_version.h"
#include "Emu/emu_callbacks.h"
#include "Emu/System.h"
#include "Emu/system_config.h"
#include "Emu/system_utils.hpp"
#include "Emu/localized_string_id.h"
#include "Emu/RSX/Overlays/overlay_utils.h"
#include "Emu/IdManager.h"
#include "Emu/VFS.h"
#include "Emu/Audio/Null/NullAudioBackend.h"
#include "Emu/Audio/Null/null_enumerator.h"
#include "Emu/Io/Null/NullKeyboardHandler.h"
#include "Emu/Io/Null/NullMouseHandler.h"
#include "Emu/Io/Null/null_camera_handler.h"
#include "Emu/Io/Null/null_music_handler.h"
#include "Emu/Io/KeyboardHandler.h"
#include "Emu/Io/MouseHandler.h"
#include "Emu/Io/pad_config.h"
#include "Emu/RSX/Null/NullGSRender.h"
#include "Emu/RSX/VK/VKGSRender.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/Cell/Modules/cellOskDialog.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"
#include "Input/pad_thread.h"
#include "Utilities/File.h"
#include "Utilities/Thread.h"
#include "util/logs.hpp"
#include "util/sysinfo.hpp"
#include "util/video_source.h"

#include <ps5platform/klog.h>
#include <volk.h>

#include <condition_variable>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <deque>
#include <mutex>

LOG_CHANNEL(sys_log, "SYS");

// The input profiles (defined by the Qt pad settings dialog on desktop)
cfg_input_configurations g_cfg_input_configs;

// A per-boot input configuration name (the Qt frontend's --input-config); unused here
std::string g_input_config_override;

// RADV, linked into the title, answers what a loader would
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);

// Set while a game runs that should return to Big Picture Mode when it stops (System.cpp)
extern atomic_t<bool> g_big_picture_mode_active;

extern std::string g_android_executable_dir;
extern std::string g_android_config_dir;
extern std::string g_android_cache_dir;

namespace audio
{
	extern void configure_audio(bool force_reset = false);
	extern void configure_rsxaudio();
}

namespace
{
	// RPCS3's folders: /data survives updates of the title and is reachable over FTP
	constexpr std::string_view data_dir = "/data/rpcs3/";

	// The display size the swapchain asks VideoOut for
	constexpr u32 display_width = 1920;
	constexpr u32 display_height = 1080;

	// call_from_main_thread: RPCS3 posts work for the "GUI" thread; the main thread runs it
	struct main_queue
	{
		std::mutex mutex;
		std::condition_variable cv;
		std::deque<std::pair<std::function<void()>, atomic_t<u32>*>> items;
		bool quit = false;
	};

	main_queue s_main_queue;

	void post_to_main(std::function<void()> func, atomic_t<u32>* wake_up)
	{
		{
			std::lock_guard lock(s_main_queue.mutex);
			s_main_queue.items.emplace_back(std::move(func), wake_up);
		}
		s_main_queue.cv.notify_one();
	}

	// Runs what is queued for the main thread now (from the main thread)
	void run_main_queue_once()
	{
		while (true)
		{
			std::pair<std::function<void()>, atomic_t<u32>*> item;
			{
				std::lock_guard lock(s_main_queue.mutex);
				if (s_main_queue.items.empty())
				{
					return;
				}
				item = std::move(s_main_queue.items.front());
				s_main_queue.items.pop_front();
			}

			item.first();

			if (item.second)
			{
				*item.second = true;
				item.second->notify_one();
			}
		}
	}

	void request_quit()
	{
		{
			std::lock_guard lock(s_main_queue.mutex);
			s_main_queue.quit = true;
		}
		s_main_queue.cv.notify_one();
	}

	// The title ends by asking the shell to close it: exit() is reported as a crash
	[[noreturn]] void close_title(int status)
	{
		std::fprintf(stderr, "RPCS3: exit status %d, asking the shell to close the title\n", status);
		std::fflush(nullptr);
		sceSystemServiceLoadExec("exit", nullptr);
		for (;;)
			sceKernelUsleep(100000);
	}

	// Players 1-4: the signed-in users' DualSenses, unless the user configured something else
	void ensure_input_config(std::string_view /*title_id*/)
	{
		// RPCS3 falls back to the default profile (input_configs/global/Default.yml) when a game
		// has no configuration of its own: on the PS5 it maps players 1-4 to ScePad
		const std::string default_profile = rpcs3::utils::get_input_config_dir() + g_cfg_input_configs.default_config + ".yml";

		if (fs::is_file(default_profile))
		{
			return;
		}

		sys_log.notice("No input configuration: players 1-4 use ScePad (%s)", default_profile);

		g_cfg_input.from_default();

		for (u32 i = 0; i < 4; i++)
		{
			cfg_player* player = g_cfg_input.player[i];
			player->handler.set(pad_handler::scepad);
			player->device.from_string(ps5_pad_handler::device_name(i));

			ps5_pad_handler handler;
			handler.init_config(&player->config);
		}

		g_cfg_input.save("", g_cfg_input_configs.default_config);
	}

	// RPCS3's on-screen text, in English: the Qt frontend's table (rpcs3qt/localized_emu.h),
	// generated without Qt into ps5_localized_strings.inc by ps5/tools/gen_localized.py
	std::string localized(localized_string_id id, const char* args)
	{
		static const std::unordered_map<localized_string_id, std::string_view> s_strings =
		{
#include "ps5_localized_strings.inc"
		};

		const auto it = s_strings.find(id);
		if (it == s_strings.end())
		{
			return {};
		}

		// Qt's %0 placeholder takes the one argument RPCS3 passes
		std::string text(it->second);
		if (const usz pos = text.find("%0"); pos != umax)
		{
			text.replace(pos, 2, args ? args : "");
		}
		return text;
	}

	void create_callbacks()
	{
		g_emu_callbacks.call_from_main_thread = [](std::function<void()> func, atomic_t<u32>* wake_up)
		{
			post_to_main(std::move(func), wake_up);
		};

		g_emu_callbacks.try_to_quit = [](bool force_quit, std::function<void()> on_exit) -> bool
		{
			// Only an explicit quit (Big Picture Mode's Exit, with auto-exit on) closes the title;
			// otherwise there is no window to stay on and Big Picture Mode comes back by itself
			if (force_quit)
			{
				if (on_exit)
				{
					on_exit();
				}

				request_quit();
				return true;
			}

			return false;
		};

		g_emu_callbacks.update_emu_settings = []()
		{
			Emu.CallFromMainThread([]()
			{
				rpcs3::utils::configure_logs(Emu.IsStopped());
				audio::configure_audio();
				audio::configure_rsxaudio();
			});
		};

		g_emu_callbacks.save_emu_settings = []()
		{
			Emu.BlockingCallFromMainThread([]()
			{
				Emulator::SaveSettings(g_cfg.to_string(), Emu.GetTitleID());
			});
		};

		g_emu_callbacks.init_kb_handler = []()
		{
			ensure(g_fxo->init<KeyboardHandlerBase, NullKeyboardHandler>(Emu.DeserialManager()));
		};

		g_emu_callbacks.init_mouse_handler = []()
		{
			ensure(g_fxo->init<MouseHandlerBase, NullMouseHandler>(Emu.DeserialManager()));
		};

		g_emu_callbacks.init_pad_handler = [](std::string_view title_id)
		{
			ensure_input_config(title_id);
			ensure(g_fxo->init<named_thread<pad_thread>>(nullptr, nullptr, title_id));

			while (!pad::g_started)
			{
				std::this_thread::sleep_for(1ms);
			}
		};

		g_emu_callbacks.get_audio = []() -> std::shared_ptr<AudioBackend>
		{
			if (g_cfg.audio.renderer == audio_renderer::null)
			{
				return std::make_shared<NullAudioBackend>();
			}

			auto result = std::make_shared<ps5_audio_backend>();

			if (!result->Initialized())
			{
				sys_log.error("SceAudioOut could not be initialized, using a Null renderer instead");
				return std::make_shared<NullAudioBackend>();
			}

			return result;
		};

		g_emu_callbacks.get_audio_enumerator = [](u64) -> std::shared_ptr<audio_device_enumerator>
		{
			return std::make_shared<null_enumerator>();
		};

		g_emu_callbacks.init_gs_render = [](utils::serial* ar)
		{
			switch (g_cfg.video.renderer.get())
			{
			case video_renderer::null:
				g_fxo->init<rsx::thread, named_thread<NullGSRender>>(ar);
				break;
			default:
				// Vulkan through RADV is the only renderer on the PS5
				g_fxo->init<rsx::thread, named_thread<VKGSRender>>(ar);
				break;
			}
		};

		g_emu_callbacks.close_gs_frame = [](){};
		g_emu_callbacks.get_gs_frame = []() -> std::unique_ptr<GSFrameBase>
		{
			return std::make_unique<ps5_frame>(display_width, display_height);
		};

		g_emu_callbacks.get_camera_handler = []() -> std::shared_ptr<camera_handler_base> { return std::make_shared<null_camera_handler>(); };
		g_emu_callbacks.get_music_handler  = []() -> std::shared_ptr<music_handler_base> { return std::make_shared<null_music_handler>(); };

		// Dialogs: RPCS3's native overlays draw them; nothing comes from the frontend
		g_emu_callbacks.get_msg_dialog                 = []() -> std::shared_ptr<MsgDialogBase> { return {}; };
		g_emu_callbacks.get_osk_dialog                 = []() -> std::shared_ptr<OskDialogBase> { return {}; };
		g_emu_callbacks.get_save_dialog                = []() -> std::unique_ptr<SaveDialogBase> { return {}; };
		g_emu_callbacks.get_trophy_notification_dialog = []() -> std::unique_ptr<TrophyNotificationBase> { return {}; };

		g_emu_callbacks.on_run    = [](bool) {};
		g_emu_callbacks.on_pause  = []() {};
		g_emu_callbacks.on_resume = []() {};
		g_emu_callbacks.on_stop   = []() { sys_log.notice("Emulation stopped"); };
		g_emu_callbacks.on_ready  = []() {};
		g_emu_callbacks.on_emulation_stop_no_response = [](std::shared_ptr<atomic_t<bool>> closed_successfully, int)
		{
			if (!closed_successfully || !*closed_successfully)
			{
				sys_log.fatal("Stopping the emulator took too long; some thread has probably deadlocked");
			}
		};

		g_emu_callbacks.on_save_state_progress = [](std::shared_ptr<atomic_t<bool>>, stx::shared_ptr<utils::serial>, stx::atomic_ptr<std::string>*, std::shared_ptr<void>) {};

		g_emu_callbacks.enable_disc_eject  = [](bool) {};
		g_emu_callbacks.enable_disc_insert = [](bool) {};
		g_emu_callbacks.on_missing_fw = []() { sys_log.error("Missing firmware: put PS3UPDAT.PUP in %s", data_dir); };
		g_emu_callbacks.handle_taskbar_progress = [](s32, s32) {};

		g_emu_callbacks.get_localized_string    = [](localized_string_id id, const char* args) -> std::string { return localized(id, args); };
		g_emu_callbacks.get_localized_u32string = [](localized_string_id id, const char* args) -> std::u32string { return utf8_to_u32string(localized(id, args)); };
		// A setting's choices by name ("Vulkan", "1280x720"): the Qt frontend's translated labels
		// are Qt-only; the config's own value names read well enough
		g_emu_callbacks.get_localized_setting   = [](const cfg::_base* node, u32 index) -> std::string
		{
			if (!node)
			{
				return {};
			}

			const std::vector<std::string> names = node->to_list();
			return index < names.size() ? names[index] : std::string();
		};

		g_emu_callbacks.play_sound = [](const std::string&, std::optional<f32>) {};
		g_emu_callbacks.add_breakpoint = [](u32) {};

		g_emu_callbacks.display_sleep_control_supported = []() { return false; };
		g_emu_callbacks.enable_display_sleep = [](bool) {};
		g_emu_callbacks.check_microphone_permissions = []() {};
		g_emu_callbacks.make_video_source = []() -> std::unique_ptr<video_source> { return nullptr; };
		g_emu_callbacks.enable_gamemode = [](bool) {};

		// No image decoding service yet (cellSearch / cellPhoto)
		g_emu_callbacks.get_image_info = [](const std::string&, std::string& sub_type, s32& width, s32& height, s32& orientation) -> bool
		{
			sub_type.clear();
			width = height = orientation = 0;
			return false;
		};
		g_emu_callbacks.get_scaled_image = [](const std::string&, s32, s32, s32&, s32&, u8*, bool) -> bool { return false; };

		// realpath is refused in a title: paths are taken as they are, if they exist
		g_emu_callbacks.resolve_path = [](std::string_view sv) -> std::string
		{
			const std::string path(sv);
			return fs::exists(path) ? path : std::string();
		};
		g_emu_callbacks.resolve_path_may_not_exist = [](std::string_view sv) { return std::string(sv); };

		g_emu_callbacks.get_font_dirs = []() { return std::vector<std::string>{}; };

		g_emu_callbacks.on_install_pkgs = [](const std::vector<std::string>& pkgs, bool from_optical_drive)
		{
			for (const std::string& pkg : pkgs)
			{
				if (!rpcs3::utils::install_pkg(pkg, from_optical_drive))
				{
					sys_log.error("Failed to install %s", pkg);
					return false;
				}
			}
			return true;
		};

		g_emu_callbacks.get_photo_path = [](std::string_view title)
		{
			const std::time_t now = std::time(nullptr);
			std::tm t{};
			localtime_r(&now, &t);
			return vfs::get(fmt::format("/dev_hdd0/photo/%04d/%02d/%02d/%s %02d-%02d-%02d.png",
				t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, vfs::escape(title, true), t.tm_hour, t.tm_min, t.tm_sec));
		};

		// RPCS3's per-game recommended settings (the database the Qt frontend downloads), packaged
		// one file per title: game_configs/<TITLE_ID>.yml, in /data/rpcs3 (to update over FTP) or /app0
		g_emu_callbacks.get_database_config = [](const std::string& title_id) -> std::string
		{
			if (title_id.empty())
			{
				return {};
			}

			for (const std::string& dir : { std::string(data_dir) + "game_configs/", std::string("/app0/game_configs/") })
			{
				if (fs::file f{dir + title_id + ".yml"})
				{
					sys_log.notice("Database config for %s: %s", title_id, dir);
					return f.to_string();
				}
			}

			sys_log.notice("No database config for %s", title_id);
			return {};
		};
	}

	// The first line of /data/rpcs3/boot.txt: an EBOOT.BIN, a game folder, an ISO, or a .pkg to install
	std::string read_boot_target()
	{
		const std::string boot_file = std::string(data_dir) + "boot.txt";
		fs::file f(boot_file);
		if (!f)
		{
			return {};
		}

		std::string text = f.to_string();
		if (const usz end = text.find_first_of("\r\n"); end != umax)
		{
			text.resize(end);
		}

		return text;
	}

	// Copies a folder's files and subfolders (resources the title ships beside its eboot)
	void copy_tree(const std::string& from, const std::string& to)
	{
		fs::create_path(to);

		for (const auto& entry : fs::dir(from))
		{
			if (entry.name == "." || entry.name == "..")
			{
				continue;
			}

			if (entry.is_directory)
			{
				copy_tree(from + "/" + entry.name, to + "/" + entry.name);
			}
			else
			{
				fs::copy_file(from + "/" + entry.name, to + "/" + entry.name, false);
			}
		}
	}

	// Defaults for the PS5 the first time (a user's config.yml keeps its own choices)
	void apply_ps5_defaults()
	{
		const std::string config_path = fs::get_config_dir(true) + "config.yml";
		if (fs::is_file(config_path))
		{
			return;
		}

		g_cfg.video.renderer.set(video_renderer::vulkan);
		g_cfg.audio.renderer.set(audio_renderer::cubeb); // any non-null renderer selects SceAudioOut
		g_cfg.misc.use_native_interface.set(true);
		g_cfg.misc.autoexit.set(true);
		Emulator::SaveSettings(g_cfg.to_string(), "");
		sys_log.notice("Wrote PS5 defaults to %s", config_path);
	}
}

// RPCS3 waits with this while the GUI thread keeps handling events: here the main thread keeps
// serving the main-thread queue, so work posted to it cannot deadlock the wait
void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op)
{
	while (!wrapped_op())
	{
		if (thread_ctrl::is_main())
		{
			run_main_queue_once();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(std::max(repeat_duration_ms, 1)));
	}
}

[[noreturn]] void report_fatal_error(std::string_view text, bool /*is_html*/ = false, bool /*include_help_text*/ = true)
{
	std::string buf(text);

	if (text.find("\nThread id = "sv) == umax && !thread_ctrl::is_main())
	{
		fmt::append(buf, "\n\nThread id = %u.", thread_ctrl::get_tid());
	}

	fmt::append(buf, "\nBuild: \"%s\"", rpcs3::get_verbose_version());

	std::fprintf(stderr, "RPCS3 fatal error: %s\n", buf.c_str());
	sys_log.fatal("%s", buf);
	logs::listener::sync_all();
	close_title(1);
}

int main(int /*argc*/, char** /*argv*/)
{
	ps5_klog_capture_stderr("[RPCS3] ");

	// Files the title writes under /data stay editable over FTP (another user)
	::umask(0);

	// RPCS3 opens some resources by relative path (Icons/ui/...): they are beside the eboot
	::chdir("/app0/");
	sceSystemServiceHideSplashScreen();
	sceUserServiceInitialize(nullptr);

	// Folders RPCS3 asks for (fs::get_executable_dir, get_config_dir, get_cache_dir)
	g_android_executable_dir = "/app0/";
	g_android_config_dir = std::string(data_dir);
	g_android_cache_dir = std::string(data_dir) + "cache/";
	fs::create_path(g_android_config_dir);
	fs::create_path(g_android_cache_dir);

	ensure(thread_ctrl::is_main(), "Not main thread");

	// Initialize thread pool finalizer (on first use)
	static_cast<void>(named_thread("", [](int) {}));

	std::unique_ptr<logs::listener> log_file = logs::make_file_listener(fs::get_log_dir() + "RPCS3.log", 512 * 1024 * 1024);

	struct log_listener_shutdown_guard
	{
		~log_listener_shutdown_guard() { logs::listener::shutdown_all(); }
	} log_listener_shutdown;

	sys_log.always()("%s", rpcs3::get_verbose_version());
	sys_log.notice("Running as a native PS5 title; data in %s", data_dir);
	sys_log.notice("%s", utils::get_system_info());

	// Vulkan's global commands from RADV, before anything creates an instance
	volkInitializeCustom(vk_icdGetInstanceProcAddr);

	create_callbacks();

	// Vulkan (RADV) is the PS5's renderer: the default RPCS3 writes into a new config.yml
	Emu.SetDefaultRenderer(video_renderer::vulkan);
	Emu.SetSupportedRenderers({video_renderer::null, video_renderer::vulkan});
	// Init requires a default adapter name with Vulkan; an unknown name selects the first GPU, the only one
	Emu.SetDefaultGraphicsAdapter("PS5 GPU (RADV)");

	Emu.SetHasGui(false);
	Emu.SetHeadless(false);
	Emu.SetUsr("00000001");
	Emu.Init();

	apply_ps5_defaults();

	// RPCS3's overlays load their images from <config dir>/Icons (the relative fallback needs a
	// working directory the title cannot set): copy the ones packaged with the title once
	if (!fs::is_file(std::string(data_dir) + "Icons/ui/cross.png") && fs::is_dir("/app0/Icons"))
	{
		copy_tree("/app0/Icons", std::string(data_dir) + "Icons");
		sys_log.notice("Copied overlay icons to %sIcons", data_dir);
	}

	// Vulkan is the only real renderer on the PS5: a config that says Null (an early build wrote
	// that default) is moved to Vulkan once. Auto-exit stays on: Big Picture Mode's Exit closes
	// the title through it.
	if (g_cfg.video.renderer == video_renderer::null || !g_cfg.misc.autoexit)
	{
		if (g_cfg.video.renderer == video_renderer::null)
		{
			g_cfg.video.renderer.set(video_renderer::vulkan);
			sys_log.warning("Renderer was Null: set to Vulkan");
		}

		g_cfg.misc.autoexit.set(true);
		Emulator::SaveSettings(g_cfg.to_string(), "");
	}

	// Firmware: install it once from /data/rpcs3/PS3UPDAT.PUP
	if (std::string fw = utils::get_firmware_version(); fw.empty())
	{
		const std::string pup = std::string(data_dir) + "PS3UPDAT.PUP";

		if (fs::is_file(pup))
		{
			fw = ps5::install_firmware(pup);
		}

		sys_log.always()("Firmware: %s", fw.empty() ? "missing" : fw);
	}
	else
	{
		sys_log.always()("Firmware version: %s", fw);
	}

	// What to start: Big Picture Mode (RPCS3's controller game shelf), unless /data/rpcs3/boot.txt
	// names a game, an ISO, a folder or a .pkg to install
	const std::string target = read_boot_target();

	if (!target.empty() && (target.ends_with(".pkg") || target.ends_with(".PKG")))
	{
		sys_log.notice("Installing %s", target);
		const bool ok = rpcs3::utils::install_pkg(target, false);
		sys_log.always()("Package install %s: %s", ok ? "succeeded" : "failed", target);
	}

	if (!target.empty() && !target.ends_with(".pkg") && !target.ends_with(".PKG"))
	{
		sys_log.notice("Booting %s (boot.txt)", target);

		Emu.CallFromMainThread([target]()
		{
			// When the game stops, Big Picture Mode comes back
			g_big_picture_mode_active = true;
			Emu.SetForceBoot(true);

			if (const game_boot_result error = Emu.BootGame(target); error != game_boot_result::no_errors)
			{
				sys_log.error("Booting '%s' failed: reason: %s", target, error);
				g_big_picture_mode_active = false;
				Emu.BootBigPictureMode();
			}
		});
	}
	else
	{
		Emu.CallFromMainThread([]()
		{
			if (!Emu.BootBigPictureMode())
			{
				sys_log.fatal("Big Picture Mode did not start");
				request_quit();
			}
		});
	}

	// The main thread: run what RPCS3 posts until emulation ends
	while (true)
	{
		std::pair<std::function<void()>, atomic_t<u32>*> item;
		{
			std::unique_lock lock(s_main_queue.mutex);

			s_main_queue.cv.wait(lock, [] { return s_main_queue.quit || !s_main_queue.items.empty(); });

			if (s_main_queue.items.empty())
			{
				break; // quit, and nothing left to run
			}

			item = std::move(s_main_queue.items.front());
			s_main_queue.items.pop_front();
		}

		item.first();

		if (item.second)
		{
			*item.second = true;
			item.second->notify_one();
		}
	}

	sys_log.notice("Quitting");
	Emu.Kill();
	logs::listener::sync_all();
	close_title(0);
}
