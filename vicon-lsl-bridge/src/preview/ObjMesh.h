#pragma once

#include "preview/PreviewTypes.h"

#include <filesystem>
#include <istream>

namespace vicon_lsl {

PreviewMesh parseObjMesh(std::istream& input);
PreviewMesh loadObjMesh(const std::filesystem::path& path);

} // namespace vicon_lsl
