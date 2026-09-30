#include "preview/PreviewXdfReader.h"

#include "preview/PreviewParsing.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
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

// Reads numbers stored lowest byte first, and throws instead of reading past the end.
class BinaryReader {
public:
    explicit BinaryReader(const std::string& path) : input_(path, std::ios::binary) {
        if (!input_) {
            throw std::runtime_error("Failed to open XDF: " + path);
        }
        input_.seekg(0, std::ios::end);
        const std::streamoff end = input_.tellg();
        if (end < 0) {
            throw std::runtime_error("Failed to size XDF: " + path);
        }
        size_ = static_cast<std::uint64_t>(end);
        input_.seekg(0, std::ios::beg);
    }

    std::uint64_t position() const { return position_; }
    std::uint64_t size() const { return size_; }
    std::uint64_t remaining() const { return size_ - position_; }
    bool eof() const { return position_ >= size_; }

    std::uint8_t peekU8() {
        require(1);
        const int value = input_.peek();
        if (value == std::char_traits<char>::eof()) {
            throw std::runtime_error("Unexpected end of XDF file");
        }
        return static_cast<std::uint8_t>(value);
    }

    void seek(std::uint64_t position) {
        if (position > size_ || position > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)())) {
            throw std::runtime_error("XDF chunk extends beyond end of file");
        }
        input_.clear();
        input_.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        if (!input_) {
            throw std::runtime_error("Failed to seek within XDF file");
        }
        position_ = position;
    }

    void skip(std::uint64_t count) {
        require(count);
        seek(position_ + count);
    }

    void expectBytes(const char* expected, std::size_t count) {
        if (readString(count) != std::string(expected, count)) {
            throw std::runtime_error("Invalid XDF magic header");
        }
    }

    std::uint8_t readU8() { return readLittle<std::uint8_t>(); }
    std::uint16_t readU16() { return readLittle<std::uint16_t>(); }
    std::uint32_t readU32() { return readLittle<std::uint32_t>(); }
    std::uint64_t readU64() { return readLittle<std::uint64_t>(); }
    std::int8_t readI8() { return readLittle<std::int8_t>(); }
    std::int16_t readI16() { return readLittle<std::int16_t>(); }
    std::int32_t readI32() { return readLittle<std::int32_t>(); }
    std::int64_t readI64() { return readLittle<std::int64_t>(); }
    float readFloat() { return readLittle<float>(); }
    double readDouble() { return readLittle<double>(); }

    // XDF stores sizes as a width byte (1, 4, or 8) followed by the value.
    std::uint64_t readVarlenInt() {
        const std::uint8_t bytes = readU8();
        if (bytes == 1) return readU8();
        if (bytes == 4) return readU32();
        if (bytes == 8) return readU64();
        throw std::runtime_error("Unsupported XDF variable-length integer width");
    }

    std::string readString(std::size_t count) {
        require(count);
        std::string value(count, '\0');
        if (count > 0) readBytes(value.data(), count);
        return value;
    }

private:
    void require(std::uint64_t count) const {
        if (count > size_ - position_) {
            throw std::runtime_error("Unexpected end of XDF file");
        }
    }

    void readBytes(void* destination, std::size_t count) {
        require(count);
        input_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(count));
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
    std::uint64_t size_ = 0;
    std::uint64_t position_ = 0;
};

// Stream header XML

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

// The text of the first <tag>...</tag> in the XML.
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
    try {
        return value ? std::stoi(*value) : default_value;
    } catch (...) {
        return default_value;
    }
}

double parseDoubleTag(const std::string& xml, const std::string& tag, double default_value) {
    const auto value = xmlTagValue(xml, tag);
    try {
        return value ? std::stod(*value) : default_value;
    } catch (...) {
        return default_value;
    }
}

// Uses the <label> values when there is one per channel, or else names the
// channels ch_0, ch_1, and so on.
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

    PreviewStreamSchema schema;
    schema.name = stream.name;
    schema.type = stream.type;
    schema.channel_labels = stream.channel_labels;
    stream.role = inferPreviewStreamRole(schema);
}

// Sample times

// A missing time is rebuilt from the previous time and the expected rate.
double resolveSampleTimestamp(const std::optional<double>& encoded_timestamp,
                              const std::optional<double>& previous_timestamp,
                              double nominal_srate) {
    double timestamp = 0.0;
    if (encoded_timestamp) {
        timestamp = *encoded_timestamp;
    } else {
        if (!previous_timestamp || !std::isfinite(nominal_srate) || nominal_srate <= 0.0) {
            throw std::runtime_error(
                "Cannot restore an XDF sample time without an earlier time and a positive expected rate");
        }
        timestamp = *previous_timestamp + 1.0 / nominal_srate;
    }
    if (!std::isfinite(timestamp)) {
        throw std::runtime_error("XDF sample timestamp is not finite");
    }
    return timestamp;
}

struct ClockOffsetFit {
    long double stream_center = 0.0L;
    long double offset_center = 0.0L;
    long double slope = 0.0L;
};

// Fits a straight line through the clock offsets after centring both values on
// their averages, because XDF times are large and rounding would lose the drift.
std::optional<ClockOffsetFit> fitClockOffsets(const std::vector<XdfClockOffset>& offsets) {
    if (offsets.empty()) {
        return std::nullopt;
    }

    long double stream_center = 0.0L;
    long double offset_center = 0.0L;
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        const long double weight = 1.0L / static_cast<long double>(index + 1);
        stream_center +=
            (static_cast<long double>(offsets[index].stream_time) - stream_center) * weight;
        offset_center +=
            (static_cast<long double>(offsets[index].offset) - offset_center) * weight;
    }

    long double covariance = 0.0L;
    long double variance = 0.0L;
    for (const auto& measurement : offsets) {
        const long double stream_delta =
            static_cast<long double>(measurement.stream_time) - stream_center;
        const long double offset_delta =
            static_cast<long double>(measurement.offset) - offset_center;
        covariance += stream_delta * offset_delta;
        variance += stream_delta * stream_delta;
    }

    const long double slope = variance > 0.0L ? covariance / variance : 0.0L;
    if (!std::isfinite(covariance) || !std::isfinite(variance) ||
        !std::isfinite(stream_center) || !std::isfinite(offset_center) ||
        !std::isfinite(slope)) {
        throw std::runtime_error("XDF clock-offset fit is not finite");
    }
    return ClockOffsetFit{stream_center, offset_center, slope};
}

// Applies the clock offsets, moves any time that does not increase to just after
// the previous one (shifting later times too so gaps keep their size), and
// returns how many times it changed.
std::size_t correctAndRepairTimestamps(XdfStreamData& stream) {
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

    if (const auto fit = fitClockOffsets(stream.clock_offsets)) {
        for (double& timestamp : stream.timestamps) {
            const long double centered_timestamp =
                static_cast<long double>(timestamp) - fit->stream_center;
            const long double correction =
                fit->offset_center + fit->slope * centered_timestamp;
            timestamp = static_cast<double>(static_cast<long double>(timestamp) + correction);
            if (!std::isfinite(timestamp)) {
                throw std::runtime_error("Corrected XDF timestamp is not finite");
            }
        }
    }

    std::size_t repaired_count = 0;
    long double accumulated_shift = 0.0L;
    std::optional<double> previous;
    for (double& timestamp : stream.timestamps) {
        const long double shifted = static_cast<long double>(timestamp) + accumulated_shift;
        double repaired = static_cast<double>(shifted);
        if (!std::isfinite(shifted) || !std::isfinite(repaired)) {
            throw std::runtime_error("Corrected XDF timestamp is not finite");
        }
        if (previous && repaired <= *previous) {
            repaired = std::nextafter(*previous, std::numeric_limits<double>::infinity());
            if (!std::isfinite(repaired)) {
                throw std::runtime_error("Could not fix an XDF timestamp that moved backward");
            }
            accumulated_shift += static_cast<long double>(repaired) - shifted;
            ++repaired_count;
        }
        timestamp = repaired;
        previous = repaired;
    }
    return repaired_count;
}

// Sample chunks

std::optional<double> readTimestamp(BinaryReader& reader) {
    const std::uint8_t bytes = reader.readU8();
    if (bytes == 0) return std::nullopt;
    if (bytes == 4) return reader.readFloat();
    if (bytes == 8) return reader.readDouble();
    throw std::runtime_error("Unsupported XDF timestamp width");
}

void skipStringValue(BinaryReader& reader) {
    const std::uint64_t length = reader.readVarlenInt();
    reader.skip(length);
}

double readNumericValue(BinaryReader& reader, const std::string& format) {
    const std::string normalized = lowerAscii(format.empty() ? "double64" : format);
    if (normalized == "double64") return reader.readDouble();
    if (normalized == "float32") return reader.readFloat();
    if (normalized == "int8") return reader.readI8();
    if (normalized == "int16") return reader.readI16();
    if (normalized == "int32") return reader.readI32();
    if (normalized == "int64") return static_cast<double>(reader.readI64());
    throw std::runtime_error("Unsupported XDF channel format: " + format);
}

void compactStoredSamples(XdfStreamData& stream, bool preserve_last = false) {
    keepEveryOther(stream.samples, preserve_last);
    keepEveryOther(stream.timestamps, preserve_last);
    if (stream.stored_sample_stride <=
        (std::numeric_limits<std::size_t>::max)() / 2) {
        stream.stored_sample_stride *= 2;
    }
}

// How many samples one stream may keep before it drops every other one.
std::size_t maximumStoredSamples(const XdfStreamData& stream,
                                 const PreviewLoadOptions& options) {
    if (stream.channel_count <= 0) return 2;
    const auto channels = static_cast<std::size_t>(stream.channel_count);
    const std::size_t by_values = options.maximum_stored_values_per_stream / channels;
    const std::size_t bytes_per_sample =
        sizeof(std::vector<double>) + sizeof(double) + channels * sizeof(double);
    const std::size_t by_memory = options.maximum_memory_bytes > 0
        ? options.maximum_memory_bytes / bytes_per_sample
        : (std::numeric_limits<std::size_t>::max)();
    return (std::max)(std::size_t{2},
                      (std::min)({options.maximum_preview_frames, by_values, by_memory}));
}

void parseSamplesChunk(BinaryReader& reader,
                       std::uint64_t chunk_end,
                       XdfStreamData& stream,
                       std::optional<double>& previous_timestamp,
                       const PreviewLoadOptions& options) {
    const std::uint64_t count = reader.readVarlenInt();
    if (stream.channel_count <= 0 || stream.channel_count > options.maximum_channels) {
        throw std::runtime_error("Invalid or excessive XDF channel count");
    }
    if (count > options.maximum_samples_per_stream ||
        stream.sample_count > options.maximum_samples_per_stream - count) {
        throw std::runtime_error("XDF stream exceeds the configured sample-count limit");
    }

    const std::size_t maximum_stored = maximumStoredSamples(stream, options);
    for (std::uint64_t sample_index = 0; sample_index < count; ++sample_index) {
        if (sample_index % (std::max)(
                std::size_t{1}, options.cancellation_check_sample_interval) == 0) {
            reportPreviewLoadProgress(options, PreviewLoadStage::Reading,
                                      reader.position(), reader.size(),
                                      "stream " + std::to_string(stream.stream_id));
        }
        const double timestamp = resolveSampleTimestamp(
            readTimestamp(reader), previous_timestamp, stream.nominal_srate);
        if (stream.sample_count == 0) {
            stream.start_timestamp = timestamp;
        } else {
            const double gap = timestamp - stream.end_timestamp;
            if (std::isfinite(gap) && gap > stream.maximum_sample_gap) {
                stream.maximum_sample_gap = gap;
            }
            if (stream.nominal_srate > 0.0 && gap > 2.0 / stream.nominal_srate) {
                ++stream.large_gap_count;
            }
        }
        stream.end_timestamp = timestamp;
        previous_timestamp = timestamp;
        const std::size_t source_index = stream.sample_count++;

        if (!stream.numeric) {
            for (int channel = 0; channel < stream.channel_count; ++channel) {
                skipStringValue(reader);
            }
            continue;
        }

        if (stream.samples.size() >= maximum_stored) compactStoredSamples(stream);
        std::vector<double> sample;
        sample.reserve(static_cast<std::size_t>(stream.channel_count));
        for (int channel = 0; channel < stream.channel_count; ++channel) {
            sample.push_back(readNumericValue(reader, stream.channel_format));
        }
        // Samples between the kept ones are dropped, but the newest is held so
        // the stream's last sample is never lost.
        if (source_index % stream.stored_sample_stride == 0) {
            stream.timestamps.push_back(timestamp);
            stream.samples.push_back(std::move(sample));
            stream.have_pending_last_sample = false;
            stream.pending_last_sample.clear();
        } else {
            stream.pending_last_timestamp = timestamp;
            stream.pending_last_sample = std::move(sample);
            stream.have_pending_last_sample = true;
        }
    }

    if (reader.position() > chunk_end) {
        throw std::runtime_error("XDF samples chunk over-read");
    }
}

void parseClockOffsetChunk(BinaryReader& reader,
                           std::uint64_t chunk_end,
                           XdfStreamData& stream) {
    if (reader.position() > chunk_end || chunk_end - reader.position() != 2 * sizeof(double)) {
        throw std::runtime_error("Invalid XDF clock-offset chunk size");
    }
    // The time is on the source stream's clock, and adding the offset gives the
    // recorder's clock.
    const double stream_time = reader.readDouble();
    const double offset = reader.readDouble();
    if (!std::isfinite(stream_time) || !std::isfinite(offset)) {
        throw std::runtime_error("XDF clock-offset measurement is not finite");
    }
    stream.clock_offsets.push_back({stream_time, offset});
}

void retainFinalSample(XdfStreamData& stream, const PreviewLoadOptions& options) {
    if (!stream.have_pending_last_sample) return;
    const std::size_t maximum_stored = maximumStoredSamples(stream, options);
    if (stream.samples.size() >= maximum_stored && !stream.samples.empty()) {
        stream.samples.back() = std::move(stream.pending_last_sample);
        stream.timestamps.back() = stream.pending_last_timestamp;
    } else {
        stream.samples.push_back(std::move(stream.pending_last_sample));
        stream.timestamps.push_back(stream.pending_last_timestamp);
    }
    stream.pending_last_sample.clear();
    stream.have_pending_last_sample = false;
}

std::size_t estimateStreamBytes(const XdfStreamData& stream) {
    std::size_t bytes = sizeof(stream);
    bytes += stream.name.capacity() + stream.type.capacity() +
             stream.source_id.capacity() + stream.hostname.capacity() +
             stream.session_id.capacity() + stream.channel_format.capacity() +
             stream.coordinate_frame.capacity();
    bytes += stream.channel_labels.capacity() * sizeof(std::string);
    bytes += stream.timestamps.capacity() * sizeof(double);
    bytes += stream.clock_offsets.capacity() * sizeof(XdfClockOffset);
    bytes += stream.samples.capacity() * sizeof(std::vector<double>);
    for (const auto& sample : stream.samples) bytes += sample.capacity() * sizeof(double);
    for (const auto& label : stream.channel_labels) bytes += label.capacity();
    bytes += stream.pending_last_sample.capacity() * sizeof(double);
    return bytes;
}

// While all streams together are over the memory limit, halves the stream
// that uses the most memory.
void enforceIndexMemoryBudget(std::map<std::uint32_t, XdfStreamData>& streams,
                              std::size_t maximum_memory_bytes,
                              bool preserve_last) {
    if (maximum_memory_bytes == 0) return;
    for (;;) {
        std::size_t total = 0;
        auto candidate = streams.end();
        std::size_t candidate_bytes = 0;
        for (auto found = streams.begin(); found != streams.end(); ++found) {
            const std::size_t bytes = estimateStreamBytes(found->second);
            if (bytes > (std::numeric_limits<std::size_t>::max)() - total) {
                total = (std::numeric_limits<std::size_t>::max)();
            } else {
                total += bytes;
            }
            if (found->second.samples.size() > 2 && bytes > candidate_bytes) {
                candidate = found;
                candidate_bytes = bytes;
            }
        }
        if (total <= maximum_memory_bytes) return;
        if (candidate == streams.end()) {
            throw std::runtime_error(
                "The XDF preview memory limit is too small even at minimum detail");
        }
        compactStoredSamples(candidate->second, preserve_last);
    }
}

} // namespace

XdfLoadResult loadXdfNumericStreams(const std::string& path,
                                    const PreviewLoadOptions& options) {
    BinaryReader reader(path);
    if (options.maximum_file_bytes > 0 && reader.size() > options.maximum_file_bytes) {
        throw std::runtime_error("XDF exceeds the configured file-size limit");
    }
    reportPreviewLoadProgress(options, PreviewLoadStage::Reading, 0, reader.size(), "XDF header");
    reader.expectBytes("XDF:", 4);

    std::map<std::uint32_t, XdfStreamData> streams_by_id;
    std::map<std::uint32_t, std::optional<double>> previous_timestamps;
    bool truncated_tail_ignored = false;
    while (!reader.eof()) {
        reportPreviewLoadProgress(options, PreviewLoadStage::Indexing,
                                  reader.position(), reader.size(), "XDF chunks");
        // Skip a final chunk cut off by a crash, but treat other damage as an error.
        const std::uint8_t varlen_width = reader.peekU8();
        if (varlen_width != 1 && varlen_width != 4 && varlen_width != 8) {
            throw std::runtime_error("Unsupported XDF variable-length integer width");
        }
        if (reader.remaining() < 1 + static_cast<std::uint64_t>(varlen_width)) {
            truncated_tail_ignored = true;
            break;
        }
        const std::uint64_t chunk_length = reader.readVarlenInt();
        const std::uint64_t chunk_start = reader.position();
        if (chunk_length > reader.size() - chunk_start) {
            truncated_tail_ignored = true;
            break;
        }
        const std::uint64_t chunk_end = chunk_start + chunk_length;
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
                if (static_cast<int>(streams_by_id.size()) >= options.maximum_streams) {
                    throw std::runtime_error("XDF exceeds the configured stream-count limit");
                }
                found = streams_by_id.emplace(stream_id, XdfStreamData{}).first;
                found->second.stream_id = stream_id;
            }
            XdfStreamData& stream = found->second;

            if (tag == XdfChunkTag::StreamHeader) {
                const std::uint64_t header_size = chunk_end - reader.position();
                if (header_size > static_cast<std::uint64_t>(options.maximum_header_bytes) ||
                    header_size > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
                    throw std::runtime_error("XDF stream header exceeds the configured size limit");
                }
                parseStreamHeaderMetadata(
                    stream, reader.readString(static_cast<std::size_t>(header_size)));
                if (stream.channel_count <= 0 || stream.channel_count > options.maximum_channels) {
                    throw std::runtime_error("Invalid or excessive XDF channel count");
                }
                enforceIndexMemoryBudget(streams_by_id, options.maximum_memory_bytes, false);
            } else if (tag == XdfChunkTag::Samples) {
                parseSamplesChunk(reader, chunk_end, stream,
                                  previous_timestamps[stream_id], options);
                enforceIndexMemoryBudget(streams_by_id, options.maximum_memory_bytes, false);
            } else if (tag == XdfChunkTag::ClockOffset) {
                parseClockOffsetChunk(reader, chunk_end, stream);
            }
        }
        reader.seek(chunk_end);
    }

    reportPreviewLoadProgress(options, PreviewLoadStage::StreamDetails,
                              streams_by_id.size(), streams_by_id.size(), "XDF streams");
    std::size_t finalized_streams = 0;
    for (auto& item : streams_by_id) {
        reportPreviewLoadProgress(options, PreviewLoadStage::Timestamps,
                                  finalized_streams, streams_by_id.size(),
                                  "Correcting XDF timestamps");
        XdfStreamData& stream = item.second;
        retainFinalSample(stream, options);
        stream.repaired_timestamp_count = correctAndRepairTimestamps(stream);
        if (!stream.timestamps.empty()) {
            stream.start_timestamp = stream.timestamps.front();
            stream.end_timestamp = stream.timestamps.back();
        }
        ++finalized_streams;
    }
    enforceIndexMemoryBudget(streams_by_id, options.maximum_memory_bytes, true);
    XdfLoadResult result;
    result.file_size_bytes = reader.size();
    result.truncated_tail_ignored = truncated_tail_ignored;
    result.streams.reserve(streams_by_id.size());
    for (auto& item : streams_by_id) {
        result.estimated_memory_bytes += estimateStreamBytes(item.second);
        result.streams.push_back(std::move(item.second));
    }
    reportPreviewLoadProgress(options, PreviewLoadStage::Timestamps,
                              finalized_streams, streams_by_id.size(),
                              "XDF timestamps corrected");
    return result;
}

} // namespace vicon_lsl
