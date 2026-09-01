#include "preview/PreviewXdfPrivate.h"

#include "preview/PreviewParsing.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace vicon_lsl::preview_xdf_detail {
namespace {

std::string xmlUnescape(std::string value) {
    const std::pair<const char*, const char*> replacements[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"},
    };
    for (const auto& replacement : replacements) {
        std::size_t pos = 0;
        while ((pos = value.find(replacement.first, pos)) != std::string::npos) {
            value.replace(pos, std::strlen(replacement.first), replacement.second);
            pos += std::strlen(replacement.second);
        }
    }
    return value;
}

std::optional<std::string> xmlTagValue(const std::string& xml, const std::string& tag) {
    const std::string open = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    const std::size_t start = xml.find(open);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t value_start = start + open.size();
    const std::size_t end = xml.find(close, value_start);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    return xmlUnescape(xml.substr(value_start, end - value_start));
}

int parseIntTag(const std::string& xml, const std::string& tag, int default_value) {
    const auto value = xmlTagValue(xml, tag);
    if (!value) {
        return default_value;
    }
    try {
        return std::stoi(*value);
    } catch (...) {
        return default_value;
    }
}

double parseDoubleTag(const std::string& xml, const std::string& tag, double default_value) {
    const auto value = xmlTagValue(xml, tag);
    if (!value) {
        return default_value;
    }
    try {
        return std::stod(*value);
    } catch (...) {
        return default_value;
    }
}

std::vector<std::string> parseChannelLabels(const std::string& xml, int channel_count) {
    std::vector<std::string> labels;
    std::size_t cursor = 0;
    while ((cursor = xml.find("<label>", cursor)) != std::string::npos) {
        cursor += 7;
        const std::size_t end = xml.find("</label>", cursor);
        if (end == std::string::npos) {
            break;
        }
        std::string label = xmlUnescape(xml.substr(cursor, end - cursor));
        labels.push_back(label.empty() ? "ch_" + std::to_string(labels.size())
                                       : std::move(label));
        cursor = end + 8;
    }

    if (channel_count <= 0) {
        channel_count = static_cast<int>(labels.size());
    }
    if (static_cast<int>(labels.size()) != channel_count) {
        labels.clear();
        for (int index = 0; index < channel_count; ++index) {
            labels.push_back("ch_" + std::to_string(index));
        }
    }
    return labels;
}

PreviewStreamRole inferRole(const XdfStreamData& stream) {
    PreviewStreamSchema schema;
    schema.name = stream.name;
    schema.type = stream.type;
    schema.channel_labels = stream.channel_labels;
    return inferPreviewStreamRole(schema);
}

} // namespace

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void parseStreamHeaderMetadata(XdfStreamData& stream, const std::string& xml) {
    stream.name = xmlTagValue(xml, "name").value_or(
        "stream_" + std::to_string(stream.stream_id));
    stream.type = xmlTagValue(xml, "type").value_or("");
    stream.source_id = xmlTagValue(xml, "source_id").value_or("");
    stream.hostname = xmlTagValue(xml, "hostname").value_or("");
    stream.session_id = xmlTagValue(xml, "session_id").value_or("");
    stream.channel_count = parseIntTag(xml, "channel_count", 0);
    stream.nominal_srate = parseDoubleTag(xml, "nominal_srate", 0.0);
    stream.channel_format = xmlTagValue(xml, "channel_format").value_or("double64");
    stream.coordinate_frame = xmlTagValue(xml, "coordinate_frame").value_or("");
    stream.channel_labels = parseChannelLabels(xml, stream.channel_count);
    if (stream.channel_count <= 0) {
        stream.channel_count = static_cast<int>(stream.channel_labels.size());
    }
    stream.numeric = lowerAscii(stream.channel_format) != "string";
    stream.role = inferRole(stream);
}

void finalizeStreamMetadata(XdfStreamData& stream) {
    if (stream.channel_count <= 0 && !stream.samples.empty()) {
        stream.channel_count = static_cast<int>(stream.samples.front().size());
    }
    if (stream.channel_labels.empty()) {
        for (int index = 0; index < stream.channel_count; ++index) {
            stream.channel_labels.push_back("ch_" + std::to_string(index));
        }
    }
    stream.role = inferRole(stream);
}

} // namespace vicon_lsl::preview_xdf_detail
