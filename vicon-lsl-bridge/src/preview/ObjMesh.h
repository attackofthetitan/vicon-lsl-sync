#pragma once

#include "preview/PreviewTypes.h"

#include <istream>
#include <string>

namespace vicon_lsl {

PreviewMesh parseObjMesh(std::istream& input);
PreviewMesh loadObjMesh(const std::string& path);

} // namespace vicon_lsl
