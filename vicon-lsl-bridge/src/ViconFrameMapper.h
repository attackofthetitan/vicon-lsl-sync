#pragma once

// The Vicon frame-mapping API: layout discovery, per-frame reads, timestamp
// policy, and the diagnostics they raise. Include this to get all of it; the
// headers below can be included individually where only one part is needed.
#include "ViconDiagnostics.h"
#include "ViconFrameMapping.h"
#include "ViconFrameTypes.h"
#include "ViconTimestamp.h"
#include "detail/ViconFrameMapperTemplates.h"
