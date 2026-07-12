#include "ViconFrameMapper.h"
#include "TestSupport.h"

#include <cmath>
#include <map>
#include <string>

namespace {

struct FakeSubject {
    std::string name;
    std::vector<std::string> markers;
    std::vector<std::string> segments;
};

struct FakeClient {
    std::vector<FakeSubject> subjects;
    std::string failed_discovery_operation;
    std::map<std::string, vicon_lsl::MarkerTranslationRead> marker_reads;
    std::map<std::string, vicon_lsl::SegmentTranslationRead> segment_translation_reads;
    std::map<std::string, vicon_lsl::SegmentRotationRead> segment_rotation_reads;

    static std::string key(const std::string& subject, const std::string& object) {
        return subject + "/" + object;
    }

    vicon_lsl::CountRead readSubjectCount() const {
        if (failed_discovery_operation == "GetSubjectCount") {
            return {vicon_lsl::ViconReadStatus::SdkError,
                    0,
                    "NoFrame (10)",
                    "Failed to get subject count"};
        }
        return {vicon_lsl::ViconReadStatus::Ok,
                static_cast<unsigned int>(subjects.size()),
                "Success",
                ""};
    }

    vicon_lsl::NameRead readSubjectName(unsigned int index) const {
        if (failed_discovery_operation == "GetSubjectName") {
            return {vicon_lsl::ViconReadStatus::SdkError,
                    "",
                    "InvalidIndex (11)",
                    "Failed to get subject name"};
        }
        return {vicon_lsl::ViconReadStatus::Ok, subjects[index].name, "Success", ""};
    }

    vicon_lsl::CountRead readMarkerCount(const std::string& subject) const {
        if (failed_discovery_operation == "GetMarkerCount") {
            return {vicon_lsl::ViconReadStatus::SdkError,
                    0,
                    "InvalidSubjectName (13)",
                    "Failed to get marker count"};
        }
        return {vicon_lsl::ViconReadStatus::Ok, getMarkerCount(subject), "Success", ""};
    }

    vicon_lsl::NameRead readMarkerName(const std::string& subject,
                                       unsigned int index) const {
        if (failed_discovery_operation == "GetMarkerName") {
            return {vicon_lsl::ViconReadStatus::SdkError,
                    "",
                    "InvalidIndex (11)",
                    "Failed to get marker name"};
        }
        return {vicon_lsl::ViconReadStatus::Ok,
                getMarkerName(subject, index),
                "Success",
                ""};
    }

    vicon_lsl::CountRead readSegmentCount(const std::string& subject) const {
        if (failed_discovery_operation == "GetSegmentCount") {
            return {vicon_lsl::ViconReadStatus::SdkError,
                    0,
                    "InvalidSubjectName (13)",
                    "Failed to get segment count"};
        }
        return {vicon_lsl::ViconReadStatus::Ok, getSegmentCount(subject), "Success", ""};
    }

    vicon_lsl::NameRead readSegmentName(const std::string& subject,
                                        unsigned int index) const {
        if (failed_discovery_operation == "GetSegmentName") {
            return {vicon_lsl::ViconReadStatus::SdkError,
                    "",
                    "InvalidIndex (11)",
                    "Failed to get segment name"};
        }
        return {vicon_lsl::ViconReadStatus::Ok,
                getSegmentName(subject, index),
                "Success",
                ""};
    }

    unsigned int getSubjectCount() const {
        return static_cast<unsigned int>(subjects.size());
    }

    std::string getSubjectName(unsigned int index) const {
        return subjects[index].name;
    }

    unsigned int getMarkerCount(const std::string& subject) const {
        for (const auto& entry : subjects) {
            if (entry.name == subject) return static_cast<unsigned int>(entry.markers.size());
        }
        return 0;
    }

    std::string getMarkerName(const std::string& subject, unsigned int index) const {
        for (const auto& entry : subjects) {
            if (entry.name == subject) return entry.markers[index];
        }
        return {};
    }

    unsigned int getSegmentCount(const std::string& subject) const {
        for (const auto& entry : subjects) {
            if (entry.name == subject) return static_cast<unsigned int>(entry.segments.size());
        }
        return 0;
    }

    std::string getSegmentName(const std::string& subject, unsigned int index) const {
        for (const auto& entry : subjects) {
            if (entry.name == subject) return entry.segments[index];
        }
        return {};
    }

    vicon_lsl::MarkerTranslationRead readMarkerGlobalTranslation(
        const std::string& subject, const std::string& marker) {
        return marker_reads.at(key(subject, marker));
    }

    vicon_lsl::SegmentTranslationRead readSegmentGlobalTranslation(
        const std::string& subject, const std::string& segment) {
        return segment_translation_reads.at(key(subject, segment));
    }

    vicon_lsl::SegmentRotationRead readSegmentGlobalRotationQuaternion(
        const std::string& subject, const std::string& segment) {
        return segment_rotation_reads.at(key(subject, segment));
    }
};

} // namespace

TEST_CASE("Vicon frame timestamp applies valid latency") {
    REQUIRE(std::abs(vicon_lsl::viconFrameTimestamp(100.0, 2.5, true) - 97.5) < 1e-12);
}

TEST_CASE("Vicon frame timestamp falls back for invalid latency") {
    REQUIRE(std::abs(vicon_lsl::viconFrameTimestamp(100.0, -1.0, true) - 100.0) < 1e-12);
    REQUIRE(std::abs(vicon_lsl::viconFrameTimestamp(100.0, vicon_lsl::quietNaN(), true) -
                     100.0) < 1e-12);
    REQUIRE(std::abs(vicon_lsl::viconFrameTimestamp(100.0, 2.5, false) - 100.0) < 1e-12);
}

TEST_CASE("Vicon timestamp policy clamps regressions without dropping frames") {
    vicon_lsl::ViconTimestampState state;
    double timestamp = 0.0;
    bool adjusted = false;
    REQUIRE(vicon_lsl::enforceViconTimestamp(10.0, 10.0, state, timestamp, &adjusted));
    REQUIRE(!adjusted);
    REQUIRE(std::abs(timestamp - 10.0) < 1e-12);

    REQUIRE(vicon_lsl::enforceViconTimestamp(9.0, 11.0, state, timestamp, &adjusted));
    REQUIRE(adjusted);
    REQUIRE(timestamp > 10.0);
    REQUIRE(vicon_lsl::enforceViconTimestamp(
        vicon_lsl::quietNaN(), 12.0, state, timestamp, &adjusted));
    REQUIRE(!adjusted);
    REQUIRE(timestamp > 10.0);
}

TEST_CASE("Vicon layout collection preserves discovered order") {
    FakeClient client;
    client.subjects = {
        {"SubjectA", {"LASI", "RASI"}, {"Pelvis"}},
        {"SubjectB", {"Head"}, {"Skull", "Thorax"}},
    };

    const auto layout = vicon_lsl::discoverLayout(client, 0).layout;
    REQUIRE_EQ(layout.markers.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(layout.segments.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(layout.markers[0], vicon_lsl::NamedViconItem("SubjectA", "LASI"));
    REQUIRE_EQ(layout.markers[2], vicon_lsl::NamedViconItem("SubjectB", "Head"));
    REQUIRE_EQ(layout.segments[2], vicon_lsl::NamedViconItem("SubjectB", "Thorax"));
}

TEST_CASE("Vicon status-bearing discovery preserves discovered order") {
    FakeClient client;
    client.subjects = {
        {"SubjectA", {"LASI", "RASI"}, {"Pelvis"}},
        {"SubjectB", {"Head"}, {"Skull", "Thorax"}},
    };

    const auto discovery = vicon_lsl::discoverLayout(client, 12);
    REQUIRE(discovery.ok());
    REQUIRE_EQ(discovery.layout.markers.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(discovery.layout.segments.size(), static_cast<std::size_t>(3));
    REQUIRE(discovery.diagnostics.empty());
}

TEST_CASE("Vicon discovery aborts partial layouts and reports SDK failures") {
    for (const std::string operation : {
             "GetSubjectCount",
             "GetSubjectName",
             "GetMarkerCount",
             "GetMarkerName",
             "GetSegmentCount",
             "GetSegmentName",
         }) {
        FakeClient client;
        client.subjects = {{"SubjectA", {"LASI"}, {"Pelvis"}}};
        client.failed_discovery_operation = operation;

        const auto discovery = vicon_lsl::discoverLayout(client, 22);
        REQUIRE(!discovery.ok());
        REQUIRE(discovery.layout.markers.empty());
        REQUIRE(discovery.layout.segments.empty());
        REQUIRE_EQ(discovery.diagnostics.size(), static_cast<std::size_t>(1));
        REQUIRE_EQ(discovery.diagnostics.front().operation, operation);
        REQUIRE_EQ(discovery.diagnostics.front().frame_number, 22U);
        REQUIRE(discovery.diagnostics.front().sdk_result != "Success");
    }
}

TEST_CASE("Vicon layout collection handles empty layouts") {
    FakeClient client;
    const auto layout = vicon_lsl::discoverLayout(client, 0).layout;
    REQUIRE(layout.markers.empty());
    REQUIRE(layout.segments.empty());

    const auto frame = vicon_lsl::buildViconFrame(client, layout, 1);
    REQUIRE(frame.markers.empty());
    REQUIRE(frame.segments.empty());
    REQUIRE(frame.diagnostics.empty());
}

TEST_CASE("Vicon layout comparison detects changes") {
    const vicon_lsl::ViconLayout known{{{"S", "M1"}}, {{"S", "Seg1"}}};
    REQUIRE(!vicon_lsl::layoutChanged(known, known));
    REQUIRE(vicon_lsl::layoutChanged({{{"S", "M2"}}, {{"S", "Seg1"}}}, known));
    REQUIRE(vicon_lsl::layoutChanged({{{"S", "M1"}}, {{"S", "Seg2"}}}, known));
    REQUIRE(vicon_lsl::layoutChanged({{{"S", "M1"}, {"S", "M2"}}}, known));
    REQUIRE(vicon_lsl::layoutChanged({{{"S", "M1"}}, {{"S", "Seg1"}, {"S", "Seg2"}}},
                                     known));
    REQUIRE(vicon_lsl::layoutChanged({{}, {{"S", "Seg1"}}},
                                     {{{"S", "M1"}}, {{"S", "Seg1"}}}));
    REQUIRE(vicon_lsl::layoutChanged({{{"S", "M1"}}, {}},
                                     {{{"S", "M1"}}, {{"S", "Seg1"}}}));
    REQUIRE(vicon_lsl::layoutChanged({}, known));
    REQUIRE(vicon_lsl::layoutChanged({{{"S", "M2"}, {"S", "M1"}}}, {{{"S", "M1"}, {"S", "M2"}}}));
    REQUIRE(vicon_lsl::layoutChanged({{}, {{"S", "Seg2"}, {"S", "Seg1"}}},
                                     {{}, {{"S", "Seg1"}, {"S", "Seg2"}}}));
}

TEST_CASE("Vicon marker frame mapping preserves status and context before LSL conversion") {
    FakeClient client;
    client.subjects = {{"S", {"Visible", "Occluded", "Failed"}, {}}};
    client.marker_reads[FakeClient::key("S", "Visible")] =
        {vicon_lsl::ViconReadStatus::Ok, {1.0, 2.0, 3.0}, false, "Success", ""};
    client.marker_reads[FakeClient::key("S", "Occluded")] =
        {vicon_lsl::ViconReadStatus::Occluded, {4.0, 5.0, 6.0}, true, "Success",
         "Marker is occluded"};
    client.marker_reads[FakeClient::key("S", "Failed")] =
        {vicon_lsl::ViconReadStatus::SdkError, {0.0, 0.0, 0.0}, false,
         "SDK result 1", "Failed to read marker global translation"};

    const auto layout = vicon_lsl::discoverLayout(client, 0).layout;
    const auto frame = vicon_lsl::buildMarkerFrame(client, layout.markers, 7);
    REQUIRE_EQ(frame.reads.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(frame.reads[0].value.translation[0], 1.0);
    REQUIRE_EQ(frame.reads[0].value.status, vicon_lsl::ViconReadStatus::Ok);
    REQUIRE_EQ(frame.reads[1].value.translation[0], 4.0);
    REQUIRE_EQ(frame.reads[1].value.status, vicon_lsl::ViconReadStatus::Occluded);
    REQUIRE_EQ(frame.reads[1].frame_number, 7U);
    REQUIRE_EQ(frame.reads[1].subject, std::string("S"));
    REQUIRE_EQ(frame.reads[1].object_name, std::string("Occluded"));
    REQUIRE_EQ(frame.reads[1].operation, std::string("GetMarkerGlobalTranslation"));
    REQUIRE_EQ(frame.reads[2].value.status, vicon_lsl::ViconReadStatus::SdkError);
    REQUIRE_EQ(frame.diagnostics.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(frame.diagnostics[0].frame_number, 7U);
    REQUIRE_EQ(frame.diagnostics[0].operation, std::string("GetMarkerGlobalTranslation"));
    REQUIRE_EQ(frame.diagnostics[1].object_name, std::string("Failed"));

    const auto visible_sample = vicon_lsl::markerSampleForLsl(frame.reads[0].value);
    const auto occluded_sample = vicon_lsl::markerSampleForLsl(frame.reads[1].value);
    REQUIRE_EQ(visible_sample[0], 1.0);
    REQUIRE_EQ(visible_sample[3], 1.0);
    REQUIRE(std::isnan(occluded_sample[0]));
    REQUIRE_EQ(occluded_sample[3], 0.0);
}

TEST_CASE("Vicon NotConnected reads remain distinguishable from SDK errors") {
    const vicon_lsl::MarkerTranslationRead read{
        vicon_lsl::ViconReadStatus::NotConnected,
        {0.0, 0.0, 0.0},
        false,
        "NotConnected (9)",
        "Vicon client is not connected",
    };
    REQUIRE(!vicon_lsl::isValid(read));
    REQUIRE_EQ(std::string(vicon_lsl::toString(read.status)), std::string("NotConnected"));
    REQUIRE(std::isnan(vicon_lsl::markerSampleForLsl(read)[0]));
}

TEST_CASE("Vicon segment frame mapping preserves status and context before LSL conversion") {
    FakeClient client;
    client.subjects = {{"S", {}, {"Good", "BadTranslation", "BadRotation"}}};
    client.segment_translation_reads[FakeClient::key("S", "Good")] =
        {vicon_lsl::ViconReadStatus::Ok, {1.0, 2.0, 3.0}, false, "Success", ""};
    client.segment_rotation_reads[FakeClient::key("S", "Good")] =
        {vicon_lsl::ViconReadStatus::Ok, {0.0, 0.0, 0.0, 1.0}, false, "Success", ""};
    client.segment_translation_reads[FakeClient::key("S", "BadTranslation")] =
        {vicon_lsl::ViconReadStatus::SdkError, {0.0, 0.0, 0.0}, false,
         "SDK result 3", "Failed to read segment global translation"};
    client.segment_rotation_reads[FakeClient::key("S", "BadTranslation")] =
        {vicon_lsl::ViconReadStatus::Ok, {0.0, 0.0, 0.0, 1.0}, false, "Success", ""};
    client.segment_translation_reads[FakeClient::key("S", "BadRotation")] =
        {vicon_lsl::ViconReadStatus::Ok, {4.0, 5.0, 6.0}, false, "Success", ""};
    client.segment_rotation_reads[FakeClient::key("S", "BadRotation")] =
        {vicon_lsl::ViconReadStatus::SdkError, {0.0, 0.0, 0.0, 1.0}, false,
         "SDK result 2", "Failed to read segment global rotation quaternion"};

    const auto layout = vicon_lsl::discoverLayout(client, 0).layout;
    const auto frame = vicon_lsl::buildSegmentFrame(client, layout.segments, 9);
    REQUIRE_EQ(frame.reads.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(frame.reads[0].rotation.quaternion[3], 1.0);
    REQUIRE_EQ(frame.reads[1].translation.status, vicon_lsl::ViconReadStatus::SdkError);
    REQUIRE_EQ(frame.reads[2].rotation.status, vicon_lsl::ViconReadStatus::SdkError);
    REQUIRE_EQ(frame.reads[2].frame_number, 9U);
    REQUIRE_EQ(frame.reads[2].subject, std::string("S"));
    REQUIRE_EQ(frame.reads[2].object_name, std::string("BadRotation"));
    REQUIRE_EQ(frame.reads[2].rotation_operation,
               std::string("GetSegmentGlobalRotationQuaternion"));
    REQUIRE_EQ(frame.diagnostics.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(frame.diagnostics[0].operation,
               std::string("GetSegmentGlobalTranslation"));
    REQUIRE_EQ(frame.diagnostics[1].operation,
               std::string("GetSegmentGlobalRotationQuaternion"));
    REQUIRE_EQ(frame.diagnostics[1].sdk_result, std::string("SDK result 2"));

    const auto good_sample =
        vicon_lsl::segmentSampleForLsl(frame.reads[0].translation, frame.reads[0].rotation);
    const auto failed_sample =
        vicon_lsl::segmentSampleForLsl(frame.reads[2].translation, frame.reads[2].rotation);
    REQUIRE_EQ(good_sample[6], 1.0);
    REQUIRE(std::isnan(failed_sample[0]));
}

TEST_CASE("Vicon diagnostic formatting includes required context") {
    const vicon_lsl::ViconDiagnostic diagnostic{
        vicon_lsl::DiagnosticSeverity::Error,
        10,
        "Subject",
        "Marker",
        "GetMarkerGlobalTranslation",
        "SDK result 1",
        "Failed to read marker global translation",
    };

    const auto text = vicon_lsl::formatDiagnostic(diagnostic);
    REQUIRE(text.find("frame=10") != std::string::npos);
    REQUIRE(text.find("operation=GetMarkerGlobalTranslation") != std::string::npos);
    REQUIRE(text.find("subject=Subject") != std::string::npos);
    REQUIRE(text.find("object=Marker") != std::string::npos);
    REQUIRE(text.find("SDK result 1") != std::string::npos);
    REQUIRE(text.find("message=Failed to read marker global translation") != std::string::npos);

}

TEST_CASE("Vicon diagnostic aggregation logs first and periodic repeats") {
    const vicon_lsl::ViconDiagnostic diagnostic{
        vicon_lsl::DiagnosticSeverity::Warning,
        10,
        "Subject",
        "Marker",
        "GetMarkerGlobalTranslation",
        "Success",
        "Marker is occluded",
    };

    vicon_lsl::DiagnosticAggregator aggregator(3);
    const auto first = aggregator.record({diagnostic});
    REQUIRE_EQ(first.log_lines.size(), static_cast<std::size_t>(1));
    REQUIRE(first.status_message.find("frame=10") != std::string::npos);

    const auto second = aggregator.record({diagnostic});
    REQUIRE(second.log_lines.empty());
    REQUIRE(second.status_message.empty());

    const auto third = aggregator.record({diagnostic});
    REQUIRE_EQ(third.log_lines.size(), static_cast<std::size_t>(1));
    REQUIRE(third.log_lines.front().find("repeated 3 times") != std::string::npos);

    aggregator.clear();
    REQUIRE_EQ(aggregator.record({diagnostic}).log_lines.size(), static_cast<std::size_t>(1));
}
