#include "preview/PreviewXdfReader.h"

#include "preview/PreviewParsing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace vicon_lsl {
namespace {

enum class XdfChunkTag : std::uint16_t {
    FileHeader = 1,
    StreamHeader = 2,
    Samples = 3,
    ClockOffset = 4,
    Boundary = 5,
    StreamFooter = 6,
};

class BinaryReader {
public:
    explicit BinaryReader(std::vector<unsigned char> data) : data_(std::move(data)) {}

    std::size_t position() const { return position_; }
    std::size_t size() const { return data_.size(); }
    bool eof() const { return position_ >= data_.size(); }

    void seek(std::size_t position) {
        if (position > data_.size()) {
            throw std::runtime_error("XDF chunk extends beyond end of file");
        }
        position_ = position;
    }

    void expectBytes(const char* expected, std::size_t count) {
        require(count);
        if (std::memcmp(data_.data() + position_, expected, count) != 0) {
            throw std::runtime_error("Invalid XDF magic header");
        }
        position_ += count;
    }

    std::uint8_t readU8() {
        require(1);
        return data_[position_++];
    }

    std::uint16_t readU16() { return readLittle<std::uint16_t>(); }
    std::uint32_t readU32() { return readLittle<std::uint32_t>(); }
    std::uint64_t readU64() { return readLittle<std::uint64_t>(); }
    std::int8_t readI8() { return static_cast<std::int8_t>(readU8()); }
    std::int16_t readI16() { return readLittle<std::int16_t>(); }
    std::int32_t readI32() { return readLittle<std::int32_t>(); }
    std::int64_t readI64() { return readLittle<std::int64_t>(); }
    float readFloat() { return readLittle<float>(); }
    double readDouble() { return readLittle<double>(); }

    std::uint64_t readVarlenInt() {
        const std::uint8_t bytes = readU8();
        if (bytes == 1) {
            return readU8();
        }
        if (bytes == 4) {
            return readU32();
        }
        if (bytes == 8) {
            return readU64();
        }
        throw std::runtime_error("Unsupported XDF variable-length integer width");
    }

    std::string readString(std::size_t count) {
        require(count);
        std::string value(reinterpret_cast<const char*>(data_.data() + position_), count);
        position_ += count;
        return value;
    }

private:
    void require(std::size_t count) const {
        if (count > data_.size() - position_) {
            throw std::runtime_error("Unexpected end of XDF file");
        }
    }

    template <typename T>
    T readLittle() {
        require(sizeof(T));
        T value{};
        std::memcpy(&value, data_.data() + position_, sizeof(T));
        position_ += sizeof(T);
        return value;
    }

    std::vector<unsigned char> data_;
    std::size_t position_ = 0;
};

std::vector<unsigned char> readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open XDF: " + path);
    }
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    input.seekg(0, std::ios::beg);
    if (length < 0) {
        throw std::runtime_error("Failed to size XDF: " + path);
    }
    std::vector<unsigned char> data(static_cast<std::size_t>(length));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    return data;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

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
        labels.push_back(label.empty() ? "ch_" + std::to_string(labels.size()) : std::move(label));
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

void parseStreamHeader(XdfStreamData& stream, const std::string& xml) {
    stream.name = xmlTagValue(xml, "name").value_or("stream_" + std::to_string(stream.stream_id));
    stream.type = xmlTagValue(xml, "type").value_or("");
    stream.source_id = xmlTagValue(xml, "source_id").value_or("");
    stream.channel_count = parseIntTag(xml, "channel_count", 0);
    stream.nominal_srate = parseDoubleTag(xml, "nominal_srate", 0.0);
    stream.channel_format = xmlTagValue(xml, "channel_format").value_or("double64");
    stream.channel_labels = parseChannelLabels(xml, stream.channel_count);
    if (stream.channel_count <= 0) {
        stream.channel_count = static_cast<int>(stream.channel_labels.size());
    }
    stream.numeric = lower(stream.channel_format) != "string";
    stream.role = inferRole(stream);
}

std::optional<double> readTimestamp(BinaryReader& reader) {
    const std::uint8_t bytes = reader.readU8();
    if (bytes == 0) {
        return std::nullopt;
    }
    if (bytes == 4) {
        return reader.readFloat();
    }
    if (bytes == 8) {
        return reader.readDouble();
    }
    throw std::runtime_error("Unsupported XDF timestamp width");
}

void skipStringValue(BinaryReader& reader) {
    const std::uint64_t length = reader.readVarlenInt();
    reader.readString(static_cast<std::size_t>(length));
}

double readNumericValue(BinaryReader& reader, const std::string& format) {
    const std::string normalized = lower(format.empty() ? "double64" : format);
    if (normalized == "double64") return reader.readDouble();
    if (normalized == "float32") return reader.readFloat();
    if (normalized == "int8") return reader.readI8();
    if (normalized == "int16") return reader.readI16();
    if (normalized == "int32") return reader.readI32();
    if (normalized == "int64") return static_cast<double>(reader.readI64());
    throw std::runtime_error("Unsupported XDF channel format: " + format);
}

void parseSamplesChunk(BinaryReader& reader,
                       std::size_t chunk_end,
                       XdfStreamData& stream,
                       std::optional<double>& previous_timestamp) {
    const std::uint64_t count = reader.readVarlenInt();
    if (stream.channel_count < 0) {
        throw std::runtime_error("Invalid XDF channel count");
    }

    for (std::uint64_t sample_index = 0; sample_index < count; ++sample_index) {
        const std::optional<double> encoded_timestamp = readTimestamp(reader);
        double timestamp = 0.0;
        if (encoded_timestamp) {
            timestamp = *encoded_timestamp;
        } else {
            if (!previous_timestamp || !std::isfinite(stream.nominal_srate) ||
                stream.nominal_srate <= 0.0) {
                throw std::runtime_error(
                    "Cannot reconstruct implicit XDF timestamp without a preceding timestamp and positive nominal_srate");
            }
            timestamp = *previous_timestamp + 1.0 / stream.nominal_srate;
        }
        if (!std::isfinite(timestamp)) {
            throw std::runtime_error("XDF sample timestamp is not finite");
        }
        previous_timestamp = timestamp;
        ++stream.sample_count;
        if (stream.numeric) {
            std::vector<double> sample;
            sample.reserve(static_cast<std::size_t>(stream.channel_count));
            for (int channel = 0; channel < stream.channel_count; ++channel) {
                sample.push_back(readNumericValue(reader, stream.channel_format));
            }
            stream.timestamps.push_back(timestamp);
            stream.samples.push_back(std::move(sample));
        } else {
            for (int channel = 0; channel < stream.channel_count; ++channel) {
                skipStringValue(reader);
            }
        }
    }

    if (reader.position() > chunk_end) {
        throw std::runtime_error("XDF samples chunk over-read");
    }
}

void parseClockOffsetChunk(BinaryReader& reader,
                           std::size_t chunk_end,
                           XdfStreamData& stream) {
    if (reader.position() > chunk_end || chunk_end - reader.position() != 2 * sizeof(double)) {
        throw std::runtime_error("Invalid XDF clock-offset chunk size");
    }
    const double collection_time = reader.readDouble();
    const double offset = reader.readDouble();
    const XdfClockOffset measurement{collection_time - offset, offset};
    if (!std::isfinite(collection_time) || !std::isfinite(measurement.stream_time) ||
        !std::isfinite(measurement.offset)) {
        throw std::runtime_error("XDF clock-offset measurement is not finite");
    }
    stream.clock_offsets.push_back(measurement);
}

double interpolatedClockOffset(const std::vector<XdfClockOffset>& offsets, double timestamp) {
    if (offsets.empty()) return 0.0;
    if (timestamp <= offsets.front().stream_time) return offsets.front().offset;
    if (timestamp >= offsets.back().stream_time) return offsets.back().offset;

    const auto upper = std::upper_bound(
        offsets.begin(), offsets.end(), timestamp,
        [](double value, const XdfClockOffset& measurement) {
            return value < measurement.stream_time;
        });
    const XdfClockOffset& right = *upper;
    const XdfClockOffset& left = *(upper - 1);
    const double fraction = (timestamp - left.stream_time) /
                            (right.stream_time - left.stream_time);
    return left.offset + fraction * (right.offset - left.offset);
}

void correctAndValidateTimestamps(XdfStreamData& stream) {
    std::stable_sort(stream.clock_offsets.begin(), stream.clock_offsets.end(),
                     [](const XdfClockOffset& left, const XdfClockOffset& right) {
                         return left.stream_time < right.stream_time;
                     });
    if (std::adjacent_find(
            stream.clock_offsets.begin(), stream.clock_offsets.end(),
            [](const XdfClockOffset& left, const XdfClockOffset& right) {
                return left.stream_time == right.stream_time;
            }) != stream.clock_offsets.end()) {
        throw std::runtime_error("XDF clock-offset measurement times are not unique");
    }

    for (double& timestamp : stream.timestamps) {
        timestamp += interpolatedClockOffset(stream.clock_offsets, timestamp);
        if (!std::isfinite(timestamp)) {
            throw std::runtime_error("Corrected XDF timestamp is not finite");
        }
    }
    if (std::adjacent_find(stream.timestamps.begin(), stream.timestamps.end(),
                           [](double left, double right) { return right <= left; }) !=
        stream.timestamps.end()) {
        throw std::runtime_error("Corrected XDF timestamps are not strictly increasing");
    }
}

} // namespace

XdfLoadResult loadXdfNumericStreams(const std::string& path) {
    BinaryReader reader(readFile(path));
    reader.expectBytes("XDF:", 4);

    std::map<std::uint32_t, XdfStreamData> streams_by_id;
    std::map<std::uint32_t, std::optional<double>> previous_timestamps;
    while (!reader.eof()) {
        const std::uint64_t chunk_length = reader.readVarlenInt();
        const std::size_t chunk_start = reader.position();
        if (chunk_length > reader.size() - chunk_start) {
            throw std::runtime_error("Invalid XDF chunk length");
        }
        const std::size_t chunk_end = chunk_start + static_cast<std::size_t>(chunk_length);
        if (chunk_end - reader.position() < sizeof(std::uint16_t)) {
            throw std::runtime_error("XDF chunk is too short for a tag");
        }

        const auto tag = static_cast<XdfChunkTag>(reader.readU16());
        if (tag == XdfChunkTag::StreamHeader || tag == XdfChunkTag::Samples ||
            tag == XdfChunkTag::ClockOffset || tag == XdfChunkTag::StreamFooter) {
            if (chunk_end - reader.position() < sizeof(std::uint32_t)) {
                throw std::runtime_error("XDF stream chunk is too short for a stream ID");
            }
            const std::uint32_t stream_id = reader.readU32();
            XdfStreamData& stream = streams_by_id[stream_id];
            stream.stream_id = stream_id;

            if (tag == XdfChunkTag::StreamHeader) {
                parseStreamHeader(stream, reader.readString(chunk_end - reader.position()));
            } else if (tag == XdfChunkTag::Samples) {
                parseSamplesChunk(reader, chunk_end, stream, previous_timestamps[stream_id]);
            } else if (tag == XdfChunkTag::ClockOffset) {
                parseClockOffsetChunk(reader, chunk_end, stream);
            }
        }

        reader.seek(chunk_end);
    }

    XdfLoadResult result;
    for (auto& item : streams_by_id) {
        XdfStreamData& stream = item.second;
        if (stream.channel_count <= 0 && !stream.samples.empty()) {
            stream.channel_count = static_cast<int>(stream.samples.front().size());
        }
        if (stream.channel_labels.empty()) {
            for (int index = 0; index < stream.channel_count; ++index) {
                stream.channel_labels.push_back("ch_" + std::to_string(index));
            }
        }
        stream.role = inferRole(stream);
        correctAndValidateTimestamps(stream);
        result.streams.push_back(std::move(stream));
    }
    return result;
}

} // namespace vicon_lsl
