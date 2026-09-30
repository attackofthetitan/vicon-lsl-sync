#include "LabRecorderClientTestSupport.h"

#include "gui/RecordingVerifier.h"

#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace labrecorder_client_tests {
namespace {

template <typename T>
void writeLittle(std::ostream& output, T value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void writeVarlen(std::ostream& output, std::uint64_t value) {
    if (value < 256) {
        output.put(1);
        output.put(static_cast<char>(value));
    } else if (value <= 0xffffffffu) {
        output.put(4);
        writeLittle(output, static_cast<std::uint32_t>(value));
    } else {
        output.put(8);
        writeLittle(output, value);
    }
}

void writeChunk(std::ostream& output,
                std::uint16_t tag,
                std::uint32_t stream_id,
                const std::string& content) {
    writeVarlen(output, sizeof(tag) + sizeof(stream_id) + content.size());
    writeLittle(output, tag);
    writeLittle(output, stream_id);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void writeVerifierStream(std::ostream& output,
                         std::uint32_t stream_id,
                         const std::vector<double>& timestamps,
                         const std::string& name,
                         const std::string& source_id) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\"?><info>"
        << "<name>" << name << "</name>"
        << "<type>Markers</type>"
        << "<channel_count>2</channel_count>"
        << "<nominal_srate>100</nominal_srate>"
        << "<channel_format>double64</channel_format>"
        << "<source_id>" << source_id << "</source_id>"
        << "<hostname>capture</hostname>"
        << "<session_id>session-1</session_id>"
        << "<desc><acquisition><coordinate_frame>vicon</coordinate_frame>"
        << "</acquisition><channels>"
        << "<channel><label>x</label></channel>"
        << "<channel><label>y</label></channel>"
        << "</channels></desc></info>";
    writeChunk(output, 2, stream_id, xml.str());

    std::ostringstream samples;
    writeVarlen(samples, timestamps.size());
    for (std::size_t index = 0; index < timestamps.size(); ++index) {
        samples.put(8);
        writeLittle(samples, timestamps[index]);
        writeLittle(samples, static_cast<double>(index));
        writeLittle(samples, static_cast<double>(index + 1));
    }
    writeChunk(output, 3, stream_id, samples.str());
}

void writeVerifierXdf(const QString& path,
                      const std::vector<double>& timestamps,
                      const std::string& name = "Markers",
                      const std::string& source_id = "markers-source") {
    std::ofstream output(path.toStdString(), std::ios::binary);
    output << "XDF:";
    writeVerifierStream(output, 1, timestamps, name, source_id);
}

vicon_lsl::gui::RecordingVerificationReport runVerifier(
    const vicon_lsl::gui::RecordingVerificationRequest& request) {
    vicon_lsl::gui::RecordingVerificationReport report;
    vicon_lsl::gui::RecordingVerifier verifier(request);
    QObject::connect(
        &verifier,
        &vicon_lsl::gui::RecordingVerifier::verificationFinished,
        [&report](const vicon_lsl::gui::RecordingVerificationReport& result) {
            report = result;
        });
    verifier.start();
    expect(waitUntil([&report]() {
               return report.state != RecordingVerificationState::NotRun;
           }, 5000),
           "background verifier produces a bounded result");
    expect(waitUntil([&verifier]() { return !verifier.isRunning(); }, 1000),
           "background verifier thread stops after its result");
    return report;
}

vicon_lsl::gui::RecordingVerificationRequest verificationRequest(
    const QString& path) {
    vicon_lsl::gui::RecordingVerificationRequest request;
    request.path = path;
    vicon_lsl::gui::StreamBinding expected;
    expected.role = "markers";
    expected.name = "Markers";
    expected.source_id = "markers-source";
    expected.required = true;
    expected.expected_channels = 2;
    expected.expected_nominal_rate = 100.0;
    expected.expected_coordinate_frame = "vicon";
    request.expected_streams = {expected};
    vicon_lsl::gui::StreamIdentity selected;
    selected.role = "markers";
    selected.name = "Markers";
    selected.source_id = "markers-source";
    selected.hostname = "capture";
    selected.channel_count = 2;
    selected.nominal_rate = 100.0;
    selected.coordinate_frame = "vicon";
    selected.selected = true;
    selected.required = true;
    request.setup_check_inventory = {selected};
    return request;
}

} // namespace

void testRecordingVerifierOutcomes() {
    QTemporaryDir directory;
    expect(directory.isValid(), "creates recording verification fixture root");
    if (!directory.isValid()) return;

    const QString healthy_path = directory.filePath("healthy.xdf");
    writeVerifierXdf(healthy_path, {10.00, 10.01, 10.02, 10.03, 10.04});
    auto healthy_request = verificationRequest(healthy_path);
    const QDateTime injected_time =
        QDateTime::fromString("2026-08-24T12:34:56.789Z", Qt::ISODateWithMs);
    healthy_request.now_utc = [injected_time]() { return injected_time; };
    const auto healthy = runVerifier(healthy_request);
    expect(healthy.state == RecordingVerificationState::Verified &&
               !healthy.hasErrors() && !healthy.hasWarnings(),
           "healthy recording is marked Verified");
    expect(healthy.streams.size() == 1 &&
               healthy.streams.front().sample_count == 5 &&
               healthy.streams.front().channel_count == 2 &&
               healthy.streams.front().source_id == "markers-source" &&
               healthy.streams.front().coordinate_frame == "vicon" &&
               healthy.streams.front().effective_rate > 99.0,
           "verification preserves exact counts, identity, schema, time range, and rate");
    expect(healthy.started_at == injected_time &&
               healthy.completed_at == injected_time,
           "verification report timestamps use the injected GUI clock");

    const QString warning_path = directory.filePath("warning.xdf");
    writeVerifierXdf(warning_path, {20.0, 20.01, 21.0});
    const auto warning = runVerifier(verificationRequest(warning_path));
    expect(warning.state == RecordingVerificationState::VerifiedWithWarnings &&
               !warning.hasErrors() && warning.hasWarnings(),
           "large gaps and low effective rate produce Verified with warnings");
    bool found_gap = false;
    for (const auto& finding : warning.findings) {
        if (finding.id == "sample-gaps") found_gap = true;
    }
    expect(found_gap, "warning report identifies the stream gap explicitly");

    // The check keeps only a few samples, but every time that moved back counts.
    std::vector<double> regressing_times;
    for (int index = 0; index < 200; ++index) {
        const bool moves_back = index > 0 && index % 20 == 0;
        regressing_times.push_back(moves_back ? regressing_times.back() - 0.001
                                              : 30.0 + index * 0.01);
    }
    const QString regressing_path = directory.filePath("regressing.xdf");
    writeVerifierXdf(regressing_path, regressing_times);
    const auto regressing = runVerifier(verificationRequest(regressing_path));
    expect(regressing.streams.size() == 1 &&
               regressing.streams.front().repaired_timestamp_count == 9 &&
               regressing.hasWarnings(),
           "the file check counts every timestamp that moved back");

    // Markers stop after 0.02 s while another stream runs on for 5 s, as when
    // the bridge replaces its streams during a recording.
    const QString early_path = directory.filePath("ended-early.xdf");
    {
        std::ofstream output(early_path.toStdString(), std::ios::binary);
        output << "XDF:";
        writeVerifierStream(output, 1, {40.0, 40.01, 40.02}, "Markers", "markers-source");
        std::vector<double> later_times;
        for (int index = 0; index <= 500; ++index) later_times.push_back(40.0 + index * 0.01);
        writeVerifierStream(output, 2, later_times, "Other", "other-source");
    }
    const auto early = runVerifier(verificationRequest(early_path));
    int ended_early = 0;
    for (const auto& finding : early.findings) {
        if (finding.id == "stream-ended-early") ++ended_early;
    }
    expect(ended_early == 1, "a stream that stops seconds before the others is flagged");

    auto missing_request = verificationRequest(healthy_path);
    missing_request.expected_streams.front().source_id = "missing-source";
    missing_request.expected_streams.front().name = "Missing";
    const auto missing = runVerifier(missing_request);
    expect(missing.state == RecordingVerificationState::NeedsAttention &&
               missing.hasErrors(),
           "missing required identity marks the recording Needs attention");

    const QString malformed_path = directory.filePath("malformed.xdf");
    {
        std::ofstream malformed(malformed_path.toStdString(), std::ios::binary);
        malformed << "not an XDF";
    }
    const auto malformed = runVerifier(verificationRequest(malformed_path));
    expect(malformed.state == RecordingVerificationState::NeedsAttention &&
               malformed.hasErrors(),
           "malformed recording returns a retained failure report");
    expect(QFileInfo::exists(malformed_path),
           "verification failure never deletes or rewrites the recording");
}

} // namespace labrecorder_client_tests
