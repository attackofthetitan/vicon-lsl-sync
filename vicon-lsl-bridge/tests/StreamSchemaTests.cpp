#include "StreamSchema.h"
#include "TestSupport.h"

#include <string>
#include <vector>

TEST_CASE("Marker schema uses discovered object names") {
    const std::vector<vicon_lsl::NamedViconItem> markers{
        {"SubjectA", "LASI"},
        {"SubjectB", "RASI"},
    };

    const auto schema = vicon_lsl::buildMarkerStreamSchema(markers, "Markers");
    REQUIRE_EQ(schema.name, std::string("Markers"));
    REQUIRE_EQ(schema.type, std::string("MoCap"));
    REQUIRE_EQ(schema.channelCount(), static_cast<std::size_t>(8));

    const std::vector<std::string> labels{
        "SubjectA:LASI:X", "SubjectA:LASI:Y", "SubjectA:LASI:Z",
        "SubjectA:LASI:Valid", "SubjectB:RASI:X", "SubjectB:RASI:Y",
        "SubjectB:RASI:Z", "SubjectB:RASI:Valid",
    };
    const std::vector<std::string> units{
        "mm", "mm", "mm", "bool", "mm", "mm", "mm", "bool",
    };
    for (std::size_t i = 0; i < labels.size(); ++i) {
        REQUIRE_EQ(schema.channels[i].label, labels[i]);
        REQUIRE_EQ(schema.channels[i].unit, units[i]);
    }
}

TEST_CASE("Segment schema uses discovered object names") {
    const std::vector<vicon_lsl::NamedViconItem> segments{{"SubjectA", "Pelvis"}};
    const auto schema = vicon_lsl::buildSegmentStreamSchema(segments, "Segments");
    REQUIRE_EQ(schema.name, std::string("Segments"));
    REQUIRE_EQ(schema.type, std::string("MoCap"));
    REQUIRE_EQ(schema.channelCount(), static_cast<std::size_t>(7));

    const std::vector<std::string> labels{
        "SubjectA:Pelvis:X", "SubjectA:Pelvis:Y", "SubjectA:Pelvis:Z",
        "SubjectA:Pelvis:QX", "SubjectA:Pelvis:QY", "SubjectA:Pelvis:QZ",
        "SubjectA:Pelvis:QW",
    };
    const std::vector<std::string> units{
        "mm", "mm", "mm", "quaternion", "quaternion", "quaternion", "quaternion",
    };
    for (std::size_t i = 0; i < labels.size(); ++i) {
        REQUIRE_EQ(schema.channels[i].label, labels[i]);
        REQUIRE_EQ(schema.channels[i].unit, units[i]);
    }
}

TEST_CASE("Sample flattening preserves marker and segment order") {
    const auto marker_sample = vicon_lsl::flattenMarkerSamples({
        {1.0, 2.0, 3.0, 1.0},
        {4.0, 5.0, 6.0, 0.0},
    });
    REQUIRE_EQ(marker_sample.size(), static_cast<std::size_t>(8));
    const std::vector<double> expected_markers{1.0, 2.0, 3.0, 1.0, 4.0, 5.0, 6.0, 0.0};
    for (std::size_t i = 0; i < expected_markers.size(); ++i) {
        REQUIRE_EQ(marker_sample[i], expected_markers[i]);
    }

    const auto segment_sample = vicon_lsl::flattenSegmentSamples({
        {1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0},
        {4.0, 5.0, 6.0, 0.1, 0.2, 0.3, 0.9},
    });
    REQUIRE_EQ(segment_sample.size(), static_cast<std::size_t>(14));
    REQUIRE_EQ(segment_sample[0], 1.0);
    REQUIRE_EQ(segment_sample[6], 1.0);
    REQUIRE_EQ(segment_sample[7], 4.0);
    REQUIRE_EQ(segment_sample[13], 0.9);
}

