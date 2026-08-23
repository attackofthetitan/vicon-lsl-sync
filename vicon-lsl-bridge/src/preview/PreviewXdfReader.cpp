#include "preview/PreviewXdfReader.h"

#include "preview/PreviewXdfPrivate.h"

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
    std::size_t remaining() const { return data_.size() - position_; }
    bool eof() const { return position_ >= data_.size(); }

    std::uint8_t peekU8() const {
        require(1);
        return data_[position_];
    }

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
    const std::string normalized = preview_xdf_detail::lowerAscii(
        format.empty() ? "double64" : format);
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
        const double timestamp = preview_xdf_detail::resolveSampleTimestamp(
            encoded_timestamp,
            previous_timestamp,
            stream.nominal_srate);
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
    preview_xdf_detail::appendClockOffset(stream, collection_time, offset);
}

} // namespace

XdfLoadResult loadXdfNumericStreams(const std::string& path) {
    BinaryReader reader(readFile(path));
    reader.expectBytes("XDF:", 4);

    std::map<std::uint32_t, XdfStreamData> streams_by_id;
    std::map<std::uint32_t, std::optional<double>> previous_timestamps;
    bool truncated_tail_ignored = false;
    while (!reader.eof()) {
        const std::uint8_t varlen_width = reader.peekU8();
        if (varlen_width != 1 && varlen_width != 4 && varlen_width != 8) {
            throw std::runtime_error("Unsupported XDF variable-length integer width");
        }
        if (reader.remaining() < 1 + static_cast<std::size_t>(varlen_width)) {
            truncated_tail_ignored = true;
            break;
        }
        const std::uint64_t chunk_length = reader.readVarlenInt();
        const std::size_t chunk_start = reader.position();
        if (chunk_length > reader.size() - chunk_start) {
            truncated_tail_ignored = true;
            break;
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
                preview_xdf_detail::parseStreamHeaderMetadata(
                    stream,
                    reader.readString(chunk_end - reader.position()));
            } else if (tag == XdfChunkTag::Samples) {
                parseSamplesChunk(reader, chunk_end, stream, previous_timestamps[stream_id]);
            } else if (tag == XdfChunkTag::ClockOffset) {
                parseClockOffsetChunk(reader, chunk_end, stream);
            }
        }

        reader.seek(chunk_end);
    }

    XdfLoadResult result;
    result.truncated_tail_ignored = truncated_tail_ignored;
    for (auto& item : streams_by_id) {
        XdfStreamData& stream = item.second;
        preview_xdf_detail::finalizeStreamMetadata(stream);
        stream.repaired_timestamp_count =
            preview_xdf_detail::correctAndRepairTimestamps(stream);
        result.streams.push_back(std::move(stream));
    }
    return result;
}

} // namespace vicon_lsl
