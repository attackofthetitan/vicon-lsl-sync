#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

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

void showError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"Vicon LSL Bridge Portable", MB_OK | MB_ICONERROR);
}

std::filesystem::path executablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

bool hasReparsePoint(const std::filesystem::path& path);
bool hasReparsePointInAncestors(const std::filesystem::path& requested);

class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.release()) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    ~ScopedHandle() { reset(); }

    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE release() {
        const HANDLE result = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return result;
    }
    void reset(HANDLE handle = INVALID_HANDLE_VALUE) {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

struct FileIdentity {
    DWORD volume_serial = 0;
    DWORD index_high = 0;
    DWORD index_low = 0;
};

bool fileIdentity(HANDLE handle, FileIdentity& identity) {
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        return false;
    }
    identity.volume_serial = information.dwVolumeSerialNumber;
    identity.index_high = information.nFileIndexHigh;
    identity.index_low = information.nFileIndexLow;
    return true;
}

bool sameFileIdentity(const FileIdentity& left, const FileIdentity& right) {
    return left.volume_serial == right.volume_serial &&
           left.index_high == right.index_high && left.index_low == right.index_low;
}

bool writeAll(HANDLE handle, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t written_total = 0;
    while (written_total < size) {
        const auto remaining = size - written_total;
        const DWORD chunk = static_cast<DWORD>(
            remaining < static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())
                ? remaining
                : static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()));
        DWORD written = 0;
        if (!WriteFile(handle, bytes + written_total, chunk, &written, nullptr) ||
            written != chunk) {
            return false;
        }
        written_total += written;
    }
    return true;
}

ScopedHandle createNewFileForWrite(const std::filesystem::path& path,
                                   FileIdentity& identity) {
    if (path.empty() || hasReparsePointInAncestors(path.parent_path())) {
        return {};
    }
    const HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }
    if (!fileIdentity(handle, identity)) {
        CloseHandle(handle);
        return {};
    }
    return ScopedHandle(handle);
}

ScopedHandle openFileReadLocked(const std::filesystem::path& path,
                                const FileIdentity& expected_identity,
                                std::uint64_t expected_size) {
    const HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }
    FileIdentity actual_identity{};
    LARGE_INTEGER size{};
    if (!fileIdentity(handle, actual_identity) ||
        !sameFileIdentity(actual_identity, expected_identity) ||
        !GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) != expected_size) {
        CloseHandle(handle);
        return {};
    }
    return ScopedHandle(handle);
}

bool handleContentsEqual(HANDLE handle, const char* expected, std::size_t expected_size) {
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(handle, beginning, nullptr, FILE_BEGIN)) {
        return false;
    }
    std::array<char, 4096> buffer{};
    std::size_t compared = 0;
    while (compared < expected_size) {
        const DWORD chunk = static_cast<DWORD>(
            (expected_size - compared) < buffer.size()
                ? (expected_size - compared)
                : buffer.size());
        DWORD read = 0;
        if (!ReadFile(handle, buffer.data(), chunk, &read, nullptr) || read != chunk) {
            return false;
        }
        for (DWORD index = 0; index < read; ++index) {
            if (buffer[index] != expected[compared + index]) {
                return false;
            }
        }
        compared += read;
    }
    return true;
}

// Open a directory without FILE_SHARE_DELETE.  Keeping this handle open for
// the lifetime of extraction prevents another process from deleting or
// renaming the extraction root while Expand-Archive is populating it.  A
// same-user process can still race individual child entries; the launcher
// rejects reparse points and only extracts into a newly-created root.
ScopedHandle openDirectoryNoDelete(const std::filesystem::path& directory) {
    if (directory.empty() || hasReparsePointInAncestors(directory)) {
        return {};
    }
    const HANDLE handle = CreateFileW(
        directory.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }
    if (hasReparsePoint(directory)) {
        CloseHandle(handle);
        return {};
    }
    return ScopedHandle(handle);
}

std::filesystem::path createTempDirectory() {
    std::array<wchar_t, 32768> temp_path{};
    const DWORD temp_length = GetTempPathW(static_cast<DWORD>(temp_path.size()), temp_path.data());
    if (temp_length == 0 || temp_length >= temp_path.size()) {
        return {};
    }

    static constexpr wchar_t hex[] = L"0123456789abcdef";
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::array<unsigned char, 16> random_bytes{};
        if (BCryptGenRandom(nullptr, random_bytes.data(), static_cast<ULONG>(random_bytes.size()),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            return {};
        }
        std::wstring name = L"vicon-lsl-";
        name.reserve(10 + random_bytes.size() * 2);
        for (const auto byte : random_bytes) {
            name.push_back(hex[(byte >> 4) & 0x0f]);
            name.push_back(hex[byte & 0x0f]);
        }
        const auto candidate = std::filesystem::path(temp_path.data()) / name;
        if (hasReparsePointInAncestors(candidate.parent_path())) {
            return {};
        }
        if (CreateDirectoryW(candidate.c_str(), nullptr)) {
            if (hasReparsePointInAncestors(candidate)) {
                // An ancestor changed after the pre-check. Do not follow the
                // path again for cleanup; leaving an empty random directory is
                // safer than deleting through a substituted reparse point.
                return {};
            }
            return candidate;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return {};
        }
    }
    return {};
}

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
// Authenticode certificate table.  A signed self-extracting executable has its
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

bool hasReparsePoint(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// A destination is safe only when neither it nor any existing ancestor is a
// reparse point.  Checking the entire chain matters for TEMP as well as for
// user-provided --extract paths: a junction higher in the chain could redirect
// extraction outside the path the user selected.
bool hasReparsePointInAncestors(const std::filesystem::path& requested) {
    std::error_code absolute_error;
    auto current = std::filesystem::absolute(requested, absolute_error);
    if (absolute_error || current.empty()) {
        return true;
    }
    current = current.lexically_normal();
    for (;;) {
        if (hasReparsePoint(current)) {
            return true;
        }
        const auto parent = current.parent_path();
        if (parent.empty() || parent == current) {
            break;
        }
        current = parent;
    }
    return false;
}

bool removeTreeIfSafe(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code status_error;
    const auto absolute = std::filesystem::absolute(path, status_error).lexically_normal();
    if (status_error || !std::filesystem::exists(absolute, status_error) || status_error ||
        hasReparsePointInAncestors(absolute)) {
        return !status_error && !std::filesystem::exists(absolute, status_error);
    }

    // Hold the root without delete sharing while enumerating and deleting its
    // children.  This prevents a parent process from swapping the root for a
    // junction during cleanup.  Child-entry races remain contained by the
    // reparse checks below and never follow a reparse target.
    ScopedHandle root_handle = openDirectoryNoDelete(absolute);
    if (!root_handle.valid()) {
        return false;
    }

    std::vector<std::filesystem::path> directories;
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> pending{absolute};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (hasReparsePointInAncestors(current) || hasReparsePoint(current)) {
            return false;
        }
        directories.push_back(current);
        std::filesystem::directory_iterator iterator(current, status_error);
        if (status_error) {
            return false;
        }
        for (const auto& entry : iterator) {
            const auto child = entry.path();
            if (hasReparsePoint(child) || hasReparsePointInAncestors(child)) {
                return false;
            }
            std::error_code type_error;
            if (entry.is_directory(type_error)) {
                if (type_error) {
                    return false;
                }
                pending.push_back(child);
            } else if (!type_error) {
                files.push_back(child);
            } else {
                return false;
            }
        }
    }

    for (const auto& file : files) {
        if (hasReparsePointInAncestors(file) || hasReparsePoint(file)) {
            return false;
        }
        std::error_code remove_error;
        std::filesystem::remove(file, remove_error);
        if (remove_error) {
            return false;
        }
    }
    for (auto directory = directories.rbegin(); directory != directories.rend(); ++directory) {
        if (hasReparsePointInAncestors(*directory) || hasReparsePoint(*directory)) {
            return false;
        }
        if (*directory == absolute) {
            continue;
        }
        std::error_code remove_error;
        std::filesystem::remove(*directory, remove_error);
        if (remove_error) {
            return false;
        }
    }

    // The root handle must be closed before removing the root itself.  Recheck
    // the parent immediately; if it is no longer trustworthy, leave the empty
    // directory in place rather than deleting through a substituted ancestor.
    root_handle.reset();
    if (hasReparsePointInAncestors(absolute) || hasReparsePoint(absolute)) {
        return false;
    }
    std::error_code remove_error;
    std::filesystem::remove(absolute, remove_error);
    return !remove_error;
}

bool createFreshExtractionDirectory(const std::filesystem::path& requested,
                                    std::filesystem::path& absolute,
                                    ScopedHandle& directory_handle) {
    try {
        absolute = std::filesystem::absolute(requested).lexically_normal();
    } catch (...) {
        return false;
    }
    if (absolute.empty() || hasReparsePointInAncestors(absolute)) {
        return false;
    }

    std::error_code status_error;
    const bool already_exists = std::filesystem::exists(absolute, status_error);
    if (status_error || already_exists) {
        return false;
    }
    const auto parent = absolute.parent_path();
    if (parent.empty() || hasReparsePoint(parent) ||
        !std::filesystem::is_directory(parent, status_error) || status_error) {
        return false;
    }

    // CreateDirectoryW is the atomic no-stale-content operation.  A race that
    // creates the target after the exists check is treated as failure.
    if (!CreateDirectoryW(absolute.c_str(), nullptr)) {
        return false;
    }
    if (hasReparsePointInAncestors(absolute)) {
        // An ancestor changed after the pre-check. Avoid any cleanup through
        // the now-untrusted path.
        return false;
    }
    directory_handle = openDirectoryNoDelete(absolute);
    if (!directory_handle.valid()) {
        // The root was just created, but do not clean it up through a path
        // whose identity could no longer be trusted.
        return false;
    }
    return true;
}

bool writeLauncherScript(const std::filesystem::path& path, ScopedHandle& script_lock) {
    static constexpr char kScript[] = R"PS1(param(
    [Parameter(Mandatory = $true)][string]$Payload,
    [Parameter(Mandatory = $true)][string]$Target,
    [switch]$Test,
    [switch]$ExtractOnly
)
$ErrorActionPreference = "Stop"
Expand-Archive -LiteralPath $Payload -DestinationPath $Target -Force
$gui = Join-Path $Target "vicon-lsl-bridge-gui.exe"
if (-not (Test-Path -LiteralPath $gui)) {
    throw "Portable payload does not contain vicon-lsl-bridge-gui.exe"
}
if ($ExtractOnly) {
    exit 0
}
if ($Test) {
    $process = Start-Process -FilePath $gui -WorkingDirectory $Target `
        -ArgumentList "--test" -Wait -PassThru
} else {
    $process = Start-Process -FilePath $gui -WorkingDirectory $Target -Wait -PassThru
}
exit $process.ExitCode
)PS1";

    FileIdentity script_identity{};
    ScopedHandle writer = createNewFileForWrite(path, script_identity);
    if (!writer.valid() || !writeAll(writer.get(), kScript, sizeof(kScript) - 1) ||
        !FlushFileBuffers(writer.get())) {
        return false;
    }
    writer.reset();
    script_lock = openFileReadLocked(path, script_identity, sizeof(kScript) - 1);
    return script_lock.valid() &&
           handleContentsEqual(script_lock.get(), kScript, sizeof(kScript) - 1);
}

std::wstring quoteWindowsArgument(const std::wstring& argument) {
    std::wstring quoted;
    quoted.reserve(argument.size() + 2);
    quoted.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::filesystem::path systemPowerShellPath() {
    std::array<wchar_t, 32768> system_directory{};
    const UINT length = GetSystemDirectoryW(
        system_directory.data(), static_cast<UINT>(system_directory.size()));
    if (length == 0 || length >= system_directory.size()) {
        return {};
    }
    return std::filesystem::path(system_directory.data()) /
           L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
}

DWORD runPayload(const std::filesystem::path& script,
                 const std::filesystem::path& payload,
                 const std::filesystem::path& target,
                 bool test_mode,
                 bool extract_only) {
    const auto powershell = systemPowerShellPath();
    if (powershell.empty() || !std::filesystem::is_regular_file(powershell)) {
        return ERROR_FILE_NOT_FOUND;
    }
    std::wstring command =
        quoteWindowsArgument(powershell.wstring()) +
        L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
        quoteWindowsArgument(script.wstring()) + L" -Payload " +
        quoteWindowsArgument(payload.wstring()) + L" -Target " +
        quoteWindowsArgument(target.wstring());
    if (test_mode) {
        command += L" -Test";
    }
    if (extract_only) {
        command += L" -ExtractOnly";
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(),
                        mutable_command.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startup,
                        &process)) {
        return ERROR_PROCESS_ABORTED;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = ERROR_PROCESS_ABORTED;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int) {
    bool test_mode = false;
    bool extract_mode = false;
    std::filesystem::path extract_directory;
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments != nullptr) {
        for (int index = 1; index < argument_count; ++index) {
            const std::wstring argument(arguments[index]);
            if (argument == L"--test") {
                test_mode = true;
            } else if (argument == L"--extract") {
                if (index + 1 >= argument_count || arguments[index + 1][0] == L'\0') {
                    if (!test_mode) {
                        showError(L"--extract requires a destination directory.");
                    }
                    LocalFree(arguments);
                    return 2;
                }
                extract_mode = true;
                extract_directory = std::filesystem::path(arguments[++index]);
            }
        }
        LocalFree(arguments);
    } else if (command_line != nullptr) {
        // CommandLineToArgvW should be available on supported Windows, but do
        // not silently lose --test if argument parsing ever fails.
        test_mode = std::wstring(command_line).find(L"--test") != std::wstring::npos;
    }
    const auto executable = executablePath();
    if (executable.empty()) {
        if (!test_mode) {
            showError(L"Unable to locate the portable executable.");
        }
        return 1;
    }

    const auto temp_directory = createTempDirectory();
    if (temp_directory.empty()) {
        if (!test_mode) {
            showError(L"Unable to create a temporary directory.");
        }
        return 1;
    }
    ScopedHandle temp_directory_handle = openDirectoryNoDelete(temp_directory);
    if (!temp_directory_handle.valid()) {
        if (!test_mode) {
            showError(L"Unable to protect the portable temporary directory.");
        }
        removeTreeIfSafe(temp_directory);
        return 1;
    }

    const auto payload = temp_directory / L"payload.zip";
    const auto script = temp_directory / L"launch.ps1";
    auto extracted = temp_directory / L"app";
    bool extract_directory_created = false;
    ScopedHandle extract_directory_handle;
    ScopedHandle payload_file_handle;
    ScopedHandle script_file_handle;
    if (extract_mode) {
        if (!createFreshExtractionDirectory(extract_directory, extracted, extract_directory_handle)) {
            if (!test_mode) {
                showError(L"The --extract destination must be a new, non-reparse directory.");
            }
            temp_directory_handle.reset();
            removeTreeIfSafe(temp_directory);
            return 1;
        }
        extract_directory_created = true;
    }

    int result = 1;
    try {
        if (!extract_mode && !std::filesystem::create_directory(extracted)) {
            throw std::runtime_error("Unable to create temporary extraction directory");
        }
        if (!extract_mode) {
            if (hasReparsePointInAncestors(extracted)) {
                throw std::runtime_error("Temporary extraction directory is a reparse point");
            }
            extract_directory_handle = openDirectoryNoDelete(extracted);
            if (!extract_directory_handle.valid()) {
                throw std::runtime_error("Unable to protect temporary extraction directory");
            }
        }
        if (!extractEmbeddedZip(executable, payload, payload_file_handle)) {
            if (!test_mode) {
                showError(L"The embedded application payload is missing or corrupt.");
            }
        } else if (!writeLauncherScript(script, script_file_handle)) {
            if (!test_mode) {
                showError(L"Unable to write the portable launcher script.");
            }
        } else {
            result = static_cast<int>(runPayload(script, payload, extracted, test_mode, extract_mode));
            if (result != 0 && !test_mode) {
                showError(L"The packaged Vicon LSL Bridge exited with an error.");
            }
        }
    } catch (...) {
        if (!test_mode) {
            showError(L"Unexpected error while starting the portable Vicon LSL Bridge.");
        }
    }

    if (extract_directory_created && result != 0) {
        extract_directory_handle.reset();
        removeTreeIfSafe(extracted);
    }
    extract_directory_handle.reset();
    script_file_handle.reset();
    payload_file_handle.reset();
    temp_directory_handle.reset();
    removeTreeIfSafe(temp_directory);
    return result;
}
