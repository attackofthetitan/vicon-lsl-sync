#include "portable_safe_fs.h"

#include <bcrypt.h>

#include <array>
#include <limits>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace vicon_lsl::portable {
namespace {

bool hasReparsePoint(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

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

} // namespace

ScopedHandle::ScopedHandle(HANDLE handle) : handle_(handle) {}

ScopedHandle::ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.release()) {}

ScopedHandle& ScopedHandle::operator=(ScopedHandle&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

ScopedHandle::~ScopedHandle() {
    reset();
}

HANDLE ScopedHandle::get() const {
    return handle_;
}

bool ScopedHandle::valid() const {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
}

HANDLE ScopedHandle::release() {
    const HANDLE result = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return result;
}

void ScopedHandle::reset(HANDLE handle) {
    if (valid()) {
        CloseHandle(handle_);
    }
    handle_ = handle;
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

// Open a directory without FILE_SHARE_DELETE. Keeping this handle open for
// the lifetime of extraction prevents another process from deleting or
// renaming the extraction root while Expand-Archive is populating it. A
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

// A destination is safe only when neither it nor any existing ancestor is a
// reparse point. Checking the entire chain matters for TEMP as well as for
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
    // children. This prevents a parent process from swapping the root for a
    // junction during cleanup. Child-entry races remain contained by the
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

    // The root handle must be closed before removing the root itself. Recheck
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

    // CreateDirectoryW is the atomic no-stale-content operation. A race that
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

} // namespace vicon_lsl::portable
