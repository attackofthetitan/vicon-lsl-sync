#pragma once

// This fixture intentionally includes only the legacy umbrella. If a symbol is
// no longer re-exported, the existing mapper test translation unit must fail to
// compile rather than silently depending on one of the focused headers.
#include "ViconFrameMapper.h"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace vicon_frame_mapper_compatibility {

using namespace vicon_lsl;

static_assert(std::is_enum_v<DiagnosticSeverity>);
static_assert(std::is_enum_v<ViconReadStatus>);
static_assert(std::is_default_constructible_v<ViconDiagnostic>);
static_assert(std::is_default_constructible_v<ViconLayout>);
static_assert(std::is_default_constructible_v<MarkerTranslationRead>);
static_assert(std::is_default_constructible_v<SegmentTranslationRead>);
static_assert(std::is_default_constructible_v<SegmentRotationRead>);
static_assert(std::is_default_constructible_v<CountRead>);
static_assert(std::is_default_constructible_v<NameRead>);
static_assert(std::is_default_constructible_v<ViconDiscoveryResult>);
static_assert(std::is_default_constructible_v<SegmentPoseRead>);
static_assert(std::is_default_constructible_v<MarkerFrameResult>);
static_assert(std::is_default_constructible_v<SegmentFrameResult>);
static_assert(std::is_default_constructible_v<ViconFrameResult>);
static_assert(std::is_default_constructible_v<DiagnosticEmission>);
static_assert(std::is_default_constructible_v<DiagnosticAggregator>);
static_assert(std::is_default_constructible_v<ViconTimestampState>);

using SeverityToString = const char* (*)(DiagnosticSeverity);
using StatusToString = const char* (*)(ViconReadStatus);
using QuietNaN = double (*)();
using FrameTimestamp = double (*)(double, double, bool);
using EnforceTimestamp =
    bool (*)(double, double, ViconTimestampState&, double&, bool*);
using InvalidMarkerSample = MarkerSample (*)();
using InvalidSegmentSample = SegmentSample (*)();
using MarkerReadValidity = bool (*)(const MarkerTranslationRead&);
using SegmentTranslationValidity = bool (*)(const SegmentTranslationRead&);
using SegmentRotationValidity = bool (*)(const SegmentRotationRead&);
using CountReadValidity = bool (*)(const CountRead&);
using NameReadValidity = bool (*)(const NameRead&);
using LayoutChanged = bool (*)(const ViconLayout&, const ViconLayout&);
using BuildStreamSourceId = std::string (*)(const std::string&,
                                            const std::string&,
                                            const std::string&);
using FormatDiagnostic = std::string (*)(const ViconDiagnostic&);
using SummarizeDiagnostics =
    std::string (*)(const std::vector<ViconDiagnostic>&);
using MarkerSampleForLsl = MarkerSample (*)(const MarkerTranslationRead&);
using SegmentSampleForLsl =
    SegmentSample (*)(const SegmentTranslationRead&, const SegmentRotationRead&);

static_assert(std::is_same_v<
              decltype(static_cast<SeverityToString>(&toString)), SeverityToString>);
static_assert(std::is_same_v<
              decltype(static_cast<StatusToString>(&toString)), StatusToString>);
static_assert(std::is_same_v<decltype(&quietNaN), QuietNaN>);
static_assert(std::is_same_v<decltype(&viconFrameTimestamp), FrameTimestamp>);
static_assert(std::is_same_v<decltype(&enforceViconTimestamp), EnforceTimestamp>);
static_assert(std::is_same_v<decltype(&invalidMarkerSample), InvalidMarkerSample>);
static_assert(std::is_same_v<decltype(&invalidSegmentSample), InvalidSegmentSample>);
static_assert(std::is_same_v<
              decltype(static_cast<MarkerReadValidity>(&isValid)), MarkerReadValidity>);
static_assert(std::is_same_v<
              decltype(static_cast<SegmentTranslationValidity>(&isValid)),
              SegmentTranslationValidity>);
static_assert(std::is_same_v<
              decltype(static_cast<SegmentRotationValidity>(&isValid)),
              SegmentRotationValidity>);
static_assert(std::is_same_v<
              decltype(static_cast<CountReadValidity>(&isValid)), CountReadValidity>);
static_assert(std::is_same_v<
              decltype(static_cast<NameReadValidity>(&isValid)), NameReadValidity>);
static_assert(std::is_same_v<decltype(&layoutChanged), LayoutChanged>);
static_assert(std::is_same_v<decltype(&buildStreamSourceId), BuildStreamSourceId>);
static_assert(std::is_same_v<decltype(&formatDiagnostic), FormatDiagnostic>);
static_assert(std::is_same_v<decltype(&diagnosticKey), FormatDiagnostic>);
static_assert(std::is_same_v<decltype(&summarizeDiagnostics), SummarizeDiagnostics>);
static_assert(std::is_same_v<decltype(&markerSampleForLsl), MarkerSampleForLsl>);
static_assert(std::is_same_v<decltype(&segmentSampleForLsl), SegmentSampleForLsl>);

using RecordDiagnostics = DiagnosticEmission (DiagnosticAggregator::*)(
    const std::vector<ViconDiagnostic>&);
using ClearDiagnostics = void (DiagnosticAggregator::*)();
static_assert(std::is_same_v<decltype(&DiagnosticAggregator::record), RecordDiagnostics>);
static_assert(std::is_same_v<decltype(&DiagnosticAggregator::clear), ClearDiagnostics>);
static_assert(std::is_same_v<
              decltype(std::declval<const ViconLayout&>() ==
                       std::declval<const ViconLayout&>()),
              bool>);
static_assert(std::is_same_v<
              decltype(std::declval<const ViconLayout&>() !=
                       std::declval<const ViconLayout&>()),
              bool>);
static_assert(std::is_same_v<
              decltype(std::declval<const ViconDiscoveryResult&>().ok()), bool>);
static_assert(std::is_same_v<
              decltype(std::declval<const DiagnosticEmission&>().shouldReportStatus()),
              bool>);

struct CompatibilityClient {
    CountRead readSubjectCount() { return {}; }
    NameRead readSubjectName(unsigned int) { return {}; }
    CountRead readMarkerCount(const std::string&) { return {}; }
    NameRead readMarkerName(const std::string&, unsigned int) { return {}; }
    CountRead readSegmentCount(const std::string&) { return {}; }
    NameRead readSegmentName(const std::string&, unsigned int) { return {}; }
    MarkerTranslationRead readMarkerGlobalTranslation(
        const std::string&, const std::string&) {
        return {};
    }
    SegmentTranslationRead readSegmentGlobalTranslation(
        const std::string&, const std::string&) {
        return {};
    }
    SegmentRotationRead readSegmentGlobalRotationQuaternion(
        const std::string&, const std::string&) {
        return {};
    }
};

using DiscoverLayout =
    ViconDiscoveryResult (*)(CompatibilityClient&, unsigned int);
using BuildMarkerFrame = MarkerFrameResult (*)(
    CompatibilityClient&, const std::vector<NamedViconItem>&, unsigned int);
using BuildSegmentFrame = SegmentFrameResult (*)(
    CompatibilityClient&, const std::vector<NamedViconItem>&, unsigned int);
using BuildViconFrame = ViconFrameResult (*)(
    CompatibilityClient&, const ViconLayout&, unsigned int);

static_assert(std::is_same_v<
              decltype(&discoverLayout<CompatibilityClient>), DiscoverLayout>);
static_assert(std::is_same_v<
              decltype(&buildMarkerFrame<CompatibilityClient>), BuildMarkerFrame>);
static_assert(std::is_same_v<
              decltype(&buildSegmentFrame<CompatibilityClient>), BuildSegmentFrame>);
static_assert(std::is_same_v<
              decltype(&buildViconFrame<CompatibilityClient>), BuildViconFrame>);

inline void instantiateMapperTemplates() {
    CompatibilityClient client;
    const ViconLayout layout;
    (void)discoverLayout(client, 0);
    (void)buildMarkerFrame(client, layout.markers, 0);
    (void)buildSegmentFrame(client, layout.segments, 0);
    (void)buildViconFrame(client, layout, 0);
}

} // namespace vicon_frame_mapper_compatibility
