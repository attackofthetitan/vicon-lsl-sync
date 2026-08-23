#include "portable_process.h"

#include <array>
#include <string>
#include <vector>

namespace vicon_lsl::portable {
namespace {

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

} // namespace

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

} // namespace vicon_lsl::portable
