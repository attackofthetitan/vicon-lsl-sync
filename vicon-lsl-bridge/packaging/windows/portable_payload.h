#pragma once

#include "portable_safe_fs.h"

#include <filesystem>

namespace vicon_lsl::portable {

bool extractEmbeddedZip(const std::filesystem::path& executable,
                        const std::filesystem::path& output,
                        ScopedHandle& output_lock);

} // namespace vicon_lsl::portable
