param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string] $Config = "Release",

    [switch] $Static
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$LiblslSource = Join-Path $Root "external\liblsl"
$BuildDir = Join-Path $Root "build\liblsl-uwp-arm64"
$InstallDir = Join-Path $Root "build\liblsl-uwp-arm64-install"
$PatchFile = Join-Path $Root "patches\liblsl-uwp-arm64.patch"

function Invoke-Native {
    $command = $args[0]
    $commandArgs = @()
    if ($args.Count -gt 1) {
        $commandArgs = $args[1..($args.Count - 1)]
    }

    & $command @commandArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
    }
}

if (-not (Test-Path (Join-Path $LiblslSource "CMakeLists.txt"))) {
    throw "liblsl submodule is missing. Run: git submodule update --init --recursive hololens-gaze-lsl/external/liblsl"
}

if (Test-Path $PatchFile) {
    $commonCpp = Join-Path $LiblslSource "src\common.cpp"
    $cmakeLists = Join-Path $LiblslSource "CMakeLists.txt"
    $hasTimerPatch = Select-String -Path $commonCpp -SimpleMatch "WINAPI_FAMILY != WINAPI_FAMILY_APP" -Quiet
    $hasLslverPatch = Select-String -Path $cmakeLists -SimpleMatch "if(NOT WINDOWS_STORE)" -Quiet

    if (-not ($hasTimerPatch -and $hasLslverPatch)) {
        Push-Location $LiblslSource
        try {
            Invoke-Native git apply $PatchFile
        } finally {
            Pop-Location
        }
    }
}

$staticValue = if ($Static) { "ON" } else { "OFF" }
$cmakeHelp = cmake --help
$generator = if ($cmakeHelp -match "Visual Studio 18 2026") {
    "Visual Studio 18 2026"
} else {
    "Visual Studio 17 2022"
}
$toolsetArgs = @()
if ($generator -eq "Visual Studio 18 2026" -and
    (Test-Path "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v170\Platforms\ARM64") -and
    -not (Test-Path "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Platforms\ARM64")) {
    $toolsetArgs = @("-T", "v143")
}

Invoke-Native cmake `
    -S $LiblslSource `
    -B $BuildDir `
    -G $generator `
    @toolsetArgs `
    -A ARM64 `
    -DCMAKE_SYSTEM_NAME=WindowsStore `
    "-DCMAKE_SYSTEM_VERSION=10.0" `
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY `
    "-DCMAKE_INSTALL_PREFIX=$InstallDir" `
    "-DLSL_BUILD_STATIC=$staticValue" `
    -DLSL_BUILD_EXAMPLES=OFF `
    -DLSL_UNITTESTS=OFF `
    -DLSL_TOOLS=OFF `
    -DLSL_BUNDLED_BOOST=ON `
    -DLSL_BUNDLED_PUGIXML=ON `
    -DLSL_SLIMARCHIVE=ON `
    -DLSL_WINVER=0x0A00

Invoke-Native cmake --build $BuildDir --config $Config
Invoke-Native cmake --install $BuildDir --config $Config

Write-Host "liblsl UWP ARM64 build installed to: $InstallDir"
