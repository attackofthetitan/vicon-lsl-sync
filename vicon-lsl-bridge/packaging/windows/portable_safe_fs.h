#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace vicon_lsl::portable {

class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE handle);
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept;
    ScopedHandle& operator=(ScopedHandle&& other) noexcept;
    ~ScopedHandle();

    HANDLE get() const;
    bool valid() const;
    HANDLE release();
    void reset(HANDLE handle = INVALID_HANDLE_VALUE);

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

struct FileIdentity {
    DWORD volume_serial = 0;
    DWORD index_high = 0;
    DWORD index_low = 0;
};

bool writeAll(HANDLE handle, const void* data, std::size_t size);
ScopedHandle createNewFileForWrite(const std::filesystem::path& path,
                                   FileIdentity& identity);
ScopedHandle openFileReadLocked(const std::filesystem::path& path,
                                const FileIdentity& expected_identity,
                                std::uint64_t expected_size);
bool handleContentsEqual(HANDLE handle, const char* expected, std::size_t expected_size);

ScopedHandle openDirectoryNoDelete(const std::filesystem::path& directory);
std::filesystem::path createTempDirectory();
bool hasReparsePointInAncestors(const std::filesystem::path& requested);
bool removeTreeIfSafe(const std::filesystem::path& path);
bool createFreshExtractionDirectory(const std::filesystem::path& requested,
                                    std::filesystem::path& absolute,
                                    ScopedHandle& directory_handle);

} // namespace vicon_lsl::portable
