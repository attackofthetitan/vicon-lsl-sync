#include "preview/PreviewXdfReader.h"

#include "preview/PreviewXdfPrivate.h"

#include <algorithm>
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

constexpr std::size_t kMaxStoredSamples = 200000;
constexpr std::size_t kMaxStoredValues = 2000000;
constexpr std::size_t kMaxHeaderBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxStreams = 4096;
constexpr int kMaxChannels = 65536;

class BinaryReader {
public:
    explicit BinaryReader(const std::string& path) : input_(path, std::ios::binary) {
        if (!input_) {
            throw std::runtime_error("Failed to open XDF: " + path);
        }
        input_.seekg(0, std::ios::end);
        const auto length = input_.tellg();
        if (length < 0) {
            throw std::runtime_error("Failed to size XDF: " + path);
        }
        size_ = static_cast<std::size_t>(length);
        input_.seekg(0, std::ios::beg);
    }

    std::size_t position() const { return position_; }
    std::size_t size() const { return size_; }
    std::size_t remaining() const { return size_ - position_; }
    bool eof() const { return position_ >= size_; }

    std::uint8_t peekU8() {
        require(1);
        return static_cast<std::uint8_t>(input_.peek());
    }

    void seek(std::size_t position) {
        if (position > size_) {
            throw std::runtime_error("XDF chunk extends beyond end of file");
        }
        input_.clear();
        input_.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        if (!input_) {
            throw std::runtime_error("Failed to seek within XDF file");
        }
        position_ = position;
    }

    void skip(std::size_t count) {
        require(count);
        seek(position_ + count);
    }

    void expectBytes(const char* expected, std::size_t count) {
        const std::string actual = readString(count);
        if (std::memcmp(actual.data(), expected, count) != 0) {
            throw std::runtime_error("Invalid XDF magic header");
        }
    }

    std::uint8_t readU8() {
        return readLittle<std::uint8_t>();
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
        std::string value(count, '\0');
        readBytes(value.data(), count);
        return value;
    }

private:
    void require(std::size_t count) const {
        if (count > size_ - position_) {
            throw std::runtime_error("Unexpected end of XDF file");
        }
    }

    void readBytes(void* destination, std::size_t count) {
        require(count);
        input_.read(static_cast<char*>(destination), static_cast<std::streamsize>(count));
        if (input_.gcount() != static_cast<std::streamsize>(count)) {
            throw std::runtime_error("Unexpected end of XDF file");
        }
        position_ += count;
    }

    template <typename T>
    T readLittle() {
        T value{};
        readBytes(&value, sizeof(T));
        return value;
    }

    std::ifstream input_;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
};

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
    reader.skip(static_cast<std::size_t>(length));
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

void thinStoredSamples(XdfStreamData& stream) {
    std::size_t output = 0;
    for (std::size_t input = 0; input < stream.samples.size(); input += 2) {
        stream.samples[output] = std::move(stream.samples[input]);
        stream.timestamps[output++] = stream.timestamps[input];
    }
    stream.samples.resize(output);
    stream.timestamps.resize(output);
    stream.stored_sample_stride *= 2;
}

void parseSamplesChunk(BinaryReader& reader,
                       std::size_t chunk_end,
                       XdfStreamData& stream,
                       std::optional<double>& previous_timestamp,
                       const std::function<bool()>& cancel) {
    const std::uint64_t count = reader.readVarlenInt();
    if (stream.channel_count < 0) {
        throw std::runtime_error("Invalid XDF channel count");
    }

    for (std::uint64_t sample_index = 0; sample_index < count; ++sample_index) {
        if ((sample_index % 1024) == 0 && cancel && cancel()) {
            throw std::runtime_error("Loading canceled");
        }
        const std::optional<double> encoded_timestamp = readTimestamp(reader);
        const double timestamp = preview_xdf_detail::resolveSampleTimestamp(
            encoded_timestamp,
            previous_timestamp,
            stream.nominal_srate);
        previous_timestamp = timestamp;
        const std::size_t source_index = stream.sample_count++;
        if (stream.numeric) {
            const bool keep = source_index % stream.stored_sample_stride == 0;
            std::vector<double> sample;
            if (keep) {
                sample.reserve(static_cast<std::size_t>(stream.channel_count));
            }
            for (int channel = 0; channel < stream.channel_count; ++channel) {
                const double value = readNumericValue(reader, stream.channel_format);
                if (keep) {
                    sample.push_back(value);
                }
            }
            if (keep) {
                stream.timestamps.push_back(timestamp);
                stream.samples.push_back(std::move(sample));
                const std::size_t channels = (std::max)(stream.channel_count, 1);
                const std::size_t limit = (std::min)(
                    kMaxStoredSamples, kMaxStoredValues / channels);
                if (stream.samples.size() > limit) {
                    thinStoredSamples(stream);
                }
            }
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

XdfLoadResult loadXdfNumericStreams(
    const std::string& path,
    const std::function<bool()>& cancel) {
    BinaryReader reader(path);
    reader.expectBytes("XDF:", 4);

    std::map<std::uint32_t, XdfStreamData> streams_by_id;
    std::map<std::uint32_t, std::optional<double>> previous_timestamps;
    bool truncated_tail_ignored = false;
    while (!reader.eof()) {
        if (cancel && cancel()) {
            throw std::runtime_error("Loading canceled");
        }
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
            auto found = streams_by_id.find(stream_id);
            if (found == streams_by_id.end()) {
                if (streams_by_id.size() >= kMaxStreams) {
                    throw std::runtime_error("XDF contains too many streams");
                }
                found = streams_by_id.emplace(stream_id, XdfStreamData{}).first;
            }
            XdfStreamData& stream = found->second;
            stream.stream_id = stream_id;

            if (tag == XdfChunkTag::StreamHeader) {
                const std::size_t header_size = chunk_end - reader.position();
                if (header_size > kMaxHeaderBytes) {
                    throw std::runtime_error("XDF stream header is too large");
                }
                preview_xdf_detail::parseStreamHeaderMetadata(
                    stream, reader.readString(header_size));
                if (stream.channel_count <= 0 || stream.channel_count > kMaxChannels) {
                    throw std::runtime_error("XDF stream has an invalid channel count");
                }
            } else if (tag == XdfChunkTag::Samples) {
                parseSamplesChunk(reader,
                                  chunk_end,
                                  stream,
                                  previous_timestamps[stream_id],
                                  cancel);
            } else if (tag == XdfChunkTag::ClockOffset) {
                parseClockOffsetChunk(reader, chunk_end, stream);
            }
        }

        reader.seek(chunk_end);
    }

    XdfLoadResult result;
    result.truncated_tail_ignored = truncated_tail_ignored;
    for (auto& item : streams_by_id) {
        if (cancel && cancel()) {
            throw std::runtime_error("Loading canceled");
        }
        XdfStreamData& stream = item.second;
        preview_xdf_detail::finalizeStreamMetadata(stream);
        stream.repaired_timestamp_count =
            preview_xdf_detail::correctAndRepairTimestamps(stream);
        result.streams.push_back(std::move(stream));
    }
    return result;
}

} // namespace vicon_lsl
