#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr std::array<char, 16> kPayloadMagic{
    'V', 'I', 'C', 'O', 'N', 'L', 'S', 'L',
    '_', 'P', 'A', 'Y', 'L', 'O', 'A', 'D',
};
constexpr std::uint64_t kFooterSize = kPayloadMagic.size() + sizeof(std::uint64_t);

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

std::filesystem::path createTempDirectory() {
    std::array<wchar_t, MAX_PATH + 1> temp_path{};
    if (GetTempPathW(static_cast<DWORD>(temp_path.size()), temp_path.data()) == 0) {
        return {};
    }

    std::array<wchar_t, MAX_PATH + 1> temp_file{};
    if (GetTempFileNameW(temp_path.data(), L"vls", 0, temp_file.data()) == 0) {
        return {};
    }

    DeleteFileW(temp_file.data());
    if (!CreateDirectoryW(temp_file.data(), nullptr)) {
        return {};
    }
    return std::filesystem::path(temp_file.data());
}

std::uint64_t decodeLittleEndian(const std::array<unsigned char, 8>& bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }
    return value;
}

bool extractEmbeddedZip(const std::filesystem::path& executable,
                        const std::filesystem::path& output) {
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
    if (file_size < kFooterSize) {
        return false;
    }

    input.seekg(static_cast<std::streamoff>(file_size - kFooterSize));

    std::array<char, kPayloadMagic.size()> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!input || magic != kPayloadMagic) {
        return false;
    }

    std::array<unsigned char, 8> size_bytes{};
    input.read(reinterpret_cast<char*>(size_bytes.data()),
               static_cast<std::streamsize>(size_bytes.size()));
    if (!input) {
        return false;
    }

    const std::uint64_t payload_size = decodeLittleEndian(size_bytes);
    if (payload_size == 0 || payload_size > file_size - kFooterSize) {
        return false;
    }

    input.seekg(static_cast<std::streamoff>(file_size - kFooterSize - payload_size));
    std::ofstream zip(output, std::ios::binary | std::ios::trunc);
    if (!zip) {
        return false;
    }

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t remaining = payload_size;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(
            remaining < buffer.size() ? remaining : buffer.size());
        input.read(buffer.data(), chunk);
        if (!input) {
            return false;
        }
        zip.write(buffer.data(), chunk);
        if (!zip) {
            return false;
        }
        remaining -= static_cast<std::uint64_t>(chunk);
    }
    return true;
}

bool writeLauncherScript(const std::filesystem::path& path) {
    static constexpr char kScript[] = R"PS1(param(
    [Parameter(Mandatory = $true)][string]$Payload,
    [Parameter(Mandatory = $true)][string]$Target,
    [switch]$Test
)
$ErrorActionPreference = "Stop"
Expand-Archive -LiteralPath $Payload -DestinationPath $Target -Force
$gui = Join-Path $Target "vicon-lsl-bridge-gui.exe"
if (-not (Test-Path -LiteralPath $gui)) {
    throw "Portable payload does not contain vicon-lsl-bridge-gui.exe"
}
if ($Test) {
    $process = Start-Process -FilePath $gui -WorkingDirectory $Target `
        -ArgumentList "--test" -Wait -PassThru
} else {
    $process = Start-Process -FilePath $gui -WorkingDirectory $Target -Wait -PassThru
}
exit $process.ExitCode
)PS1";

    std::ofstream script(path, std::ios::binary | std::ios::trunc);
    script.write(kScript, sizeof(kScript) - 1);
    return static_cast<bool>(script);
}

std::wstring quoted(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

DWORD runPayload(const std::filesystem::path& script,
                 const std::filesystem::path& payload,
                 const std::filesystem::path& target,
                 bool test_mode) {
    std::wstring command =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
        quoted(script) + L" -Payload " + quoted(payload) + L" -Target " + quoted(target);
    if (test_mode) {
        command += L" -Test";
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr,
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
    const bool test_mode =
        command_line != nullptr &&
        std::wstring(command_line).find(L"--test") != std::wstring::npos;
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

    const auto payload = temp_directory / L"payload.zip";
    const auto script = temp_directory / L"launch.ps1";
    const auto extracted = temp_directory / L"app";

    int result = 1;
    try {
        std::filesystem::create_directory(extracted);
        if (!extractEmbeddedZip(executable, payload)) {
            if (!test_mode) {
                showError(L"The embedded application payload is missing or corrupt.");
            }
        } else if (!writeLauncherScript(script)) {
            if (!test_mode) {
                showError(L"Unable to write the portable launcher script.");
            }
        } else {
            result = static_cast<int>(runPayload(script, payload, extracted, test_mode));
            if (result != 0 && !test_mode) {
                showError(L"The packaged Vicon LSL Bridge exited with an error.");
            }
        }
    } catch (...) {
        if (!test_mode) {
            showError(L"Unexpected error while starting the portable Vicon LSL Bridge.");
        }
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(temp_directory, cleanup_error);
    return result;
}
