#include "stdafx.h"
#include "ps5_firmware.h"

#include "Emu/System.h"
#include "Emu/VFS.h"
#include "Emu/vfs_config.h"
#include "Emu/system_utils.hpp"
#include "Crypto/unself.h"
#include "Crypto/key_vault.h"
#include "Loader/PUP.h"
#include "Loader/TAR.h"
#include "Utilities/File.h"
#include "Utilities/Thread.h"

LOG_CHANNEL(fw_log, "PS5 FW");

std::string ps5::install_firmware(const std::string& path)
{
	fs::file pup_f(path);
	if (!pup_f)
	{
		fw_log.error("Cannot open %s (%s)", path, fs::g_tls_error);
		return {};
	}

	pup_object pup(std::move(pup_f));
	if (pup.operator pup_error() != pup_error::ok)
	{
		fw_log.error("Invalid PUP file: %s", pup.get_formatted_error());
		return {};
	}

	fs::file update_files_f = pup.get_file(0x300);
	if (!update_files_f || !update_files_f.size())
	{
		fw_log.error("No installation package database in the PUP");
		return {};
	}

	tar_object update_files(update_files_f);

	// The dev_flash_* entries are TARs whose files make up dev_flash
	auto update_filenames = update_files.get_filenames();
	std::erase_if(update_filenames, [](const std::string& s) { return s.find("dev_flash_") == umax; });

	if (update_filenames.empty())
	{
		fw_log.error("No dev_flash_* packages in the PUP");
		return {};
	}

	std::string version_string;
	if (fs::file version = pup.get_file(0x100))
	{
		version_string = version.to_string();
	}
	if (const usz pos = version_string.find('\n'); pos != umax)
	{
		version_string.erase(pos);
	}
	if (version_string.empty())
	{
		fw_log.error("No version data in the PUP");
		return {};
	}

	fw_log.notice("Installing firmware %s (%u packages)", version_string, update_filenames.size());

	// tar_object::extract() writes under /dev_flash
	vfs::mount("/dev_flash", g_cfg_vfs.get_dev_flash());

	bool ok = true;
	{
		named_thread worker("Firmware Installer", [&]
		{
			for (const auto& update_filename : update_filenames)
			{
				auto update_file_stream = update_files.get_file(update_filename);

				if (update_file_stream->m_file_handler)
				{
					// Forcefully read all the data
					update_file_stream->m_file_handler->handle_file_op(*update_file_stream, 0, update_file_stream->get_size(umax), nullptr);
				}

				fs::file update_file = fs::make_stream(std::move(update_file_stream->data));

				SCEDecrypter self_dec(update_file);
				self_dec.LoadHeaders();
				self_dec.LoadMetadata(SCEPKG_ERK, SCEPKG_RIV);
				self_dec.DecryptData();

				auto dev_flash_tar_f = self_dec.MakeFile();
				if (dev_flash_tar_f.size() < 3)
				{
					fw_log.error("Package %s could not be decompressed", update_filename);
					ok = false;
					return;
				}

				tar_object dev_flash_tar(dev_flash_tar_f[2]);
				if (!dev_flash_tar.extract())
				{
					fw_log.error("Package %s could not be extracted", update_filename);
					ok = false;
					return;
				}

				fw_log.notice("Installed %s", update_filename);
			}
		});

		worker();
	}

	update_files_f.close();

	// Unmount
	Emu.Init();

	if (!ok)
	{
		return {};
	}

	fw_log.success("Installed PS3 firmware %s", version_string);
	return version_string;
}
