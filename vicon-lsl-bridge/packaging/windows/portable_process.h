#pragma once

#include "portable_safe_fs.h"

#include <windows.h>

#include <filesystem>

namespace vicon_lsl::portable {

bool writeLauncherScript(const std::filesystem::path& path, ScopedHandle& script_lock);
DWORD runPayload(const std::filesystem::path& script,
                 const std::filesystem::path& payload,
                 const std::filesystem::path& target,
                 bool test_mode,
                 bool extract_only);

} // namespace vicon_lsl::portable
