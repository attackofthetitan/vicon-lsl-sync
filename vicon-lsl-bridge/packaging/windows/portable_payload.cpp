#include "portable_payload.h"

#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace vicon_lsl::portable {
namespace {

constexpr std::array<char, 16> kPayloadMagic{
    'V', 'I', 'C', 'O', 'N', 'L', 'S', 'L',
    '_', 'P', 'A', 'Y', 'L', 'O', 'A', 'D',
};
constexpr std::uint64_t kFooterSize = kPayloadMagic.size() + sizeof(std::uint64_t);
constexpr char kPayloadDigestPrefix[] = "VICONLSL_PAYLOAD_SHA256=";
volatile const char kPayloadDigestMarker[] =
    "VICONLSL_PAYLOAD_SHA256="
    "00000000000000000000000000000000"
    "00000000000000000000000000000000";
constexpr std::size_t kPayloadDigestHexSize = 64;

std::uint64_t decodeLittleEndian(const std::array<unsigned char, 8>& bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }
    return value;
}

bool readAt(std::ifstream& input, std::uint64_t offset, void* data, std::size_t size) {
    if (offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()) ||
        size > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)())) {
        return false;
    }
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        return false;
    }
    input.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(input.gcount()) == size;
}

std::uint16_t readWord(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8);
}

std::uint32_t readDword(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

// The PE security directory stores a file offset (rather than an RVA) to the
// Authenticode certificate table. A signed self-extracting executable has its
// ZIP/footer before that table, so this is the logical end of our payload.
std::uint64_t logicalFileEnd(std::ifstream& input, std::uint64_t file_size) {
    std::array<unsigned char, 64> dos_header{};
    if (file_size < dos_header.size() || !readAt(input, 0, dos_header.data(), dos_header.size()) ||
        readWord(dos_header.data()) != 0x5a4d) {
        return file_size;
    }

    const std::uint32_t pe_offset = readDword(dos_header.data() + 0x3c);
    if (pe_offset > file_size || file_size - pe_offset < 24) {
        return file_size;
    }

    std::array<unsigned char, 24> nt_headers{};
    if (!readAt(input, pe_offset, nt_headers.data(), nt_headers.size()) ||
        readDword(nt_headers.data()) != 0x00004550) {
        return file_size;
    }

    const std::uint16_t optional_size = readWord(nt_headers.data() + 20);
    const std::uint64_t optional_offset = static_cast<std::uint64_t>(pe_offset) + 24;
    if (optional_size < 2 || optional_offset > file_size ||
        optional_size > file_size - optional_offset) {
        return file_size;
    }

    std::vector<unsigned char> optional_header(optional_size);
    if (!readAt(input, optional_offset, optional_header.data(), optional_header.size())) {
        return file_size;
    }
    const std::uint16_t optional_magic = readWord(optional_header.data());
    std::size_t number_of_directories_offset = 0;
    std::size_t data_directory_offset = 0;
    if (optional_magic == 0x10b) {
        number_of_directories_offset = 92;
        data_directory_offset = 96;
    } else if (optional_magic == 0x20b) {
        number_of_directories_offset = 108;
        data_directory_offset = 112;
    } else {
        return file_size;
    }
    if (optional_header.size() < number_of_directories_offset + sizeof(std::uint32_t) ||
        readDword(optional_header.data() + number_of_directories_offset) <= 4 ||
        optional_header.size() < data_directory_offset + 5 * 8) {
        return file_size;
    }

    const auto* security_directory = optional_header.data() + data_directory_offset + 4 * 8;
    const std::uint32_t certificate_offset = readDword(security_directory);
    const std::uint32_t certificate_size = readDword(security_directory + 4);
    if (certificate_offset == 0 || certificate_size == 0 || certificate_offset > file_size ||
        certificate_size > file_size - certificate_offset) {
        return file_size;
    }
    return certificate_offset;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool readExpectedPayloadDigest(std::ifstream& input,
                               std::uint64_t image_end,
                               std::array<unsigned char, 32>& expected) {
    if (kPayloadDigestMarker[0] != kPayloadDigestPrefix[0]) {
        return false;
    }
    constexpr std::size_t prefix_size = sizeof(kPayloadDigestPrefix) - 1;
    constexpr std::size_t overlap_size = prefix_size - 1;
    constexpr std::size_t chunk_size = 64 * 1024;
    std::array<char, chunk_size> chunk{};
    std::string overlap;
    std::uint64_t scanned = 0;
    while (scanned < image_end) {
        const auto remaining = image_end - scanned;
        const auto to_read = static_cast<std::streamsize>(
            remaining < chunk.size() ? remaining : chunk.size());
        if (!readAt(input, scanned, chunk.data(), static_cast<std::size_t>(to_read))) {
            return false;
        }
        std::string combined = overlap;
        combined.append(chunk.data(), static_cast<std::size_t>(to_read));
        const std::uint64_t combined_start = scanned - overlap.size();
        std::size_t position = combined.find(kPayloadDigestPrefix);
        while (position != std::string::npos) {
            const std::uint64_t marker_offset = combined_start + position;
            if (marker_offset <= image_end &&
                image_end - marker_offset >= prefix_size + kPayloadDigestHexSize) {
                std::array<char, kPayloadDigestHexSize> encoded{};
                if (!readAt(input, marker_offset + prefix_size, encoded.data(), encoded.size())) {
                    return false;
                }
                bool valid = true;
                for (std::size_t i = 0; i < encoded.size(); i += 2) {
                    const int high = hexValue(encoded[i]);
                    const int low = hexValue(encoded[i + 1]);
                    if (high < 0 || low < 0) {
                        valid = false;
                        break;
                    }
                    expected[i / 2] = static_cast<unsigned char>((high << 4) | low);
                }
                if (valid) {
                    return true;
                }
            }
            position = combined.find(kPayloadDigestPrefix, position + 1);
        }
        scanned += static_cast<std::uint64_t>(to_read);
        if (combined.size() > overlap_size) {
            overlap.assign(combined.data() + combined.size() - overlap_size, overlap_size);
        } else {
            overlap = std::move(combined);
        }
    }
    return false;
}

bool sha256Range(const std::filesystem::path& file,
                 std::uint64_t offset,
                 std::uint64_t size,
                 std::array<unsigned char, 32>& digest) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD result_size = 0;
    bool success = false;
    do {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                              &result_size, 0) != 0) {
            break;
        }
        std::vector<unsigned char> object(object_size);
        if (BCryptCreateHash(algorithm, &hash, object.data(), object.size(), nullptr, 0, 0) != 0) {
            break;
        }

        std::ifstream input(file, std::ios::binary);
        if (!input || offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()) ||
            size > static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)())) {
            break;
        }
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        std::array<unsigned char, 64 * 1024> buffer{};
        std::uint64_t remaining = size;
        while (remaining > 0) {
            const auto chunk = static_cast<std::streamsize>(
                remaining < buffer.size() ? remaining : buffer.size());
            input.read(reinterpret_cast<char*>(buffer.data()), chunk);
            if (!input || BCryptHashData(hash, buffer.data(), static_cast<ULONG>(chunk), 0) != 0) {
                break;
            }
            remaining -= static_cast<std::uint64_t>(chunk);
        }
        if (remaining != 0 || BCryptFinishHash(hash, digest.data(), digest.size(), 0) != 0) {
            break;
        }
        success = true;
    } while (false);

    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return success;
}

bool locatePayloadFooter(std::ifstream& input,
                         std::uint64_t logical_end,
                         std::uint64_t& footer_offset,
                         std::uint64_t& payload_size) {
    constexpr std::uint64_t search_window = 64 * 1024;
    if (logical_end < kFooterSize) {
        return false;
    }
    const std::uint64_t first = logical_end > search_window ? logical_end - search_window : 0;
    std::array<char, kPayloadMagic.size()> magic{};
    std::array<unsigned char, 8> size_bytes{};
    for (std::uint64_t offset = logical_end - kFooterSize;; --offset) {
        if (readAt(input, offset, magic.data(), magic.size()) && magic == kPayloadMagic &&
            readAt(input, offset + kPayloadMagic.size(), size_bytes.data(), size_bytes.size())) {
            const auto candidate_size = decodeLittleEndian(size_bytes);
            if (candidate_size > 0 && candidate_size <= offset &&
                offset <= logical_end - kFooterSize) {
                footer_offset = offset;
                payload_size = candidate_size;
                return true;
            }
        }
        if (offset == first) {
            break;
        }
    }
    return false;
}

} // namespace

bool extractEmbeddedZip(const std::filesystem::path& executable,
                        const std::filesystem::path& output,
                        ScopedHandle& output_lock) {
    std::ifstream input(executable, std::ios::binary);
    if (!input) {
        return false;
    }

    input.seekg(0, std::ios::end);
    const auto file_size_position = input.tellg();
    if (file_size_position < 0) {
        return false;
    }
    const auto file_size = static_cast<std::uint64_t>(file_size_position);
    const auto logical_end = logicalFileEnd(input, file_size);
    if (logical_end < kFooterSize) {
        return false;
    }

    std::uint64_t footer_offset = 0;
    std::uint64_t payload_size = 0;
    if (!locatePayloadFooter(input, logical_end, footer_offset, payload_size)) {
        return false;
    }

    const auto payload_start = footer_offset - payload_size;
    std::array<unsigned char, 32> expected_digest{};
    if (!readExpectedPayloadDigest(input, payload_start, expected_digest)) {
        return false;
    }
    std::array<unsigned char, 32> actual_digest{};
    if (!sha256Range(executable, payload_start, payload_size, actual_digest) ||
        actual_digest != expected_digest) {
        return false;
    }

    FileIdentity output_identity{};
    ScopedHandle output_writer = createNewFileForWrite(output, output_identity);
    if (!output_writer.valid()) {
        return false;
    }

    input.seekg(static_cast<std::streamoff>(payload_start));
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t remaining = payload_size;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(
            remaining < buffer.size() ? remaining : buffer.size());
        input.read(buffer.data(), chunk);
        if (!input) {
            return false;
        }
        if (!writeAll(output_writer.get(), buffer.data(), static_cast<std::size_t>(chunk))) {
            return false;
        }
        remaining -= static_cast<std::uint64_t>(chunk);
    }
    if (!FlushFileBuffers(output_writer.get())) {
        return false;
    }
    output_writer.reset();

    output_lock = openFileReadLocked(output, output_identity, payload_size);
    if (!output_lock.valid()) {
        return false;
    }
    std::array<unsigned char, 32> persisted_digest{};
    return sha256Range(output, 0, payload_size, persisted_digest) &&
           persisted_digest == expected_digest;
}

} // namespace vicon_lsl::portable
