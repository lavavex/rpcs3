#pragma once

#include <string>

namespace ps5
{
	// Installs PS3UPDAT.PUP into dev_flash, as main_window::HandlePupInstallation does without Qt.
	// Returns the installed version, or an empty string on failure (logged).
	std::string install_firmware(const std::string& pup_path);
}
