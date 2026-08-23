#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "portable_payload.h"
#include "portable_process.h"
#include "portable_safe_fs.h"

#pragma comment(lib, "shell32.lib")

namespace portable = vicon_lsl::portable;

namespace {

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

    const auto temp_directory = portable::createTempDirectory();
    if (temp_directory.empty()) {
        if (!test_mode) {
            showError(L"Unable to create a temporary directory.");
        }
        return 1;
    }
    portable::ScopedHandle temp_directory_handle =
        portable::openDirectoryNoDelete(temp_directory);
    if (!temp_directory_handle.valid()) {
        if (!test_mode) {
            showError(L"Unable to protect the portable temporary directory.");
        }
        portable::removeTreeIfSafe(temp_directory);
        return 1;
    }

    const auto payload = temp_directory / L"payload.zip";
    const auto script = temp_directory / L"launch.ps1";
    auto extracted = temp_directory / L"app";
    bool extract_directory_created = false;
    portable::ScopedHandle extract_directory_handle;
    portable::ScopedHandle payload_file_handle;
    portable::ScopedHandle script_file_handle;
    if (extract_mode) {
        if (!portable::createFreshExtractionDirectory(
                extract_directory, extracted, extract_directory_handle)) {
            if (!test_mode) {
                showError(L"The --extract destination must be a new, non-reparse directory.");
            }
            temp_directory_handle.reset();
            portable::removeTreeIfSafe(temp_directory);
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
            if (portable::hasReparsePointInAncestors(extracted)) {
                throw std::runtime_error("Temporary extraction directory is a reparse point");
            }
            extract_directory_handle = portable::openDirectoryNoDelete(extracted);
            if (!extract_directory_handle.valid()) {
                throw std::runtime_error("Unable to protect temporary extraction directory");
            }
        }
        if (!portable::extractEmbeddedZip(executable, payload, payload_file_handle)) {
            if (!test_mode) {
                showError(L"The embedded application payload is missing or corrupt.");
            }
        } else if (!portable::writeLauncherScript(script, script_file_handle)) {
            if (!test_mode) {
                showError(L"Unable to write the portable launcher script.");
            }
        } else {
            result = static_cast<int>(
                portable::runPayload(script, payload, extracted, test_mode, extract_mode));
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
        portable::removeTreeIfSafe(extracted);
    }
    extract_directory_handle.reset();
    script_file_handle.reset();
    payload_file_handle.reset();
    temp_directory_handle.reset();
    portable::removeTreeIfSafe(temp_directory);
    return result;
}
