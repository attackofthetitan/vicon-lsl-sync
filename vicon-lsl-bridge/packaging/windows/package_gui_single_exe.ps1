param(
    [Parameter(Mandatory = $true)]
    [string]$DeployDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputExe,

    [ValidateSet("Auto", "Enigma", "Native")]
    [string]$Mode = "Auto",

    [string]$LauncherExe = "",
    [string]$EnigmaConsole = $env:ENIGMA_VB_CONSOLE,
    [string]$ProjectFile = "",
    [string]$LabRecorderDeployDir = "",
    [string]$StairModelDir = ""
)

$ErrorActionPreference = "Stop"

function Find-EnigmaConsole {
    param([string]$RequestedPath)

    if ($RequestedPath -and (Test-Path -LiteralPath $RequestedPath)) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $candidates = @(
        "$env:ProgramFiles\Enigma Virtual Box\enigmavbconsole.exe",
        "${env:ProgramFiles(x86)}\Enigma Virtual Box\enigmavbconsole.exe",
        "$env:ProgramFiles\Enigma Virtual Box\EnigmaVBConsole.exe",
        "${env:ProgramFiles(x86)}\Enigma Virtual Box\EnigmaVBConsole.exe"
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Invoke-EnigmaPackage {
    param(
        [string]$ConsolePath,
        [string]$ProjectPath,
        [string]$ExpectedOutput
    )

    Write-Host "Packaging with Enigma Virtual Box"
    & $ConsolePath $ProjectPath
    if ($LASTEXITCODE -ne 0) {
        throw "Enigma Virtual Box console failed with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $ExpectedOutput)) {
        throw "Enigma completed, but output executable was not found: $ExpectedOutput"
    }
}

function Invoke-NativePackage {
    param(
        [string]$SourceDirectory,
        [string]$LauncherPath,
        [string]$ExpectedOutput
    )

    if (-not $LauncherPath -or -not (Test-Path -LiteralPath $LauncherPath)) {
        throw "Native mode requires -LauncherExe pointing to vicon-lsl-bridge-portable-launcher.exe."
    }

    $work = Join-Path $env:TEMP ("vicon-lsl-bridge-package-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $work | Out-Null

    try {
        $payload = Join-Path $work "payload.zip"
        Compress-Archive -Path (Join-Path $SourceDirectory "*") -DestinationPath $payload -Force
        Copy-Item -LiteralPath $LauncherPath -Destination $ExpectedOutput -Force

        $payloadLength = (Get-Item -LiteralPath $payload).Length
        $output = [System.IO.File]::Open(
            $ExpectedOutput,
            [System.IO.FileMode]::Append,
            [System.IO.FileAccess]::Write,
            [System.IO.FileShare]::None)
        $input = [System.IO.File]::OpenRead($payload)
        try {
            $input.CopyTo($output)
            $magic = [System.Text.Encoding]::ASCII.GetBytes("VICONLSL_PAYLOAD")
            if ($magic.Length -ne 16) {
                throw "Internal payload marker must be exactly 16 bytes."
            }
            $output.Write($magic, 0, $magic.Length)
            $size = [System.BitConverter]::GetBytes([UInt64]$payloadLength)
            $output.Write($size, 0, $size.Length)
        } finally {
            $input.Dispose()
            $output.Dispose()
        }

        $expectedLength = (Get-Item -LiteralPath $LauncherPath).Length + $payloadLength + 24
        $actualLength = (Get-Item -LiteralPath $ExpectedOutput).Length
        if ($actualLength -ne $expectedLength) {
            throw "Native package length mismatch: expected $expectedLength bytes, got $actualLength."
        }

        $verify = [System.IO.File]::OpenRead($ExpectedOutput)
        try {
            $verify.Seek(-24, [System.IO.SeekOrigin]::End) | Out-Null
            $footer = New-Object byte[] 24
            if ($verify.Read($footer, 0, $footer.Length) -ne $footer.Length) {
                throw "Unable to read native package footer."
            }
            $footerMagic = [System.Text.Encoding]::ASCII.GetString($footer, 0, 16)
            $footerSize = [System.BitConverter]::ToUInt64($footer, 16)
            if ($footerMagic -ne "VICONLSL_PAYLOAD" -or $footerSize -ne $payloadLength) {
                throw "Native package footer validation failed."
            }
        } finally {
            $verify.Dispose()
        }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Copy-DeploymentTree {
    param([string]$SourceDirectory, [string]$DestinationDirectory)

    if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
        throw "Deployment directory was not found: $SourceDirectory"
    }
    New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $SourceDirectory -Force | Copy-Item -Destination $DestinationDirectory -Recurse -Force
}

function Find-DeploymentFile {
    param([string]$Root, [string]$Name)
    return Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Name -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

$deployPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DeployDir)
New-Item -ItemType Directory -Path $deployPath -Force | Out-Null

# Keep deployment inputs in the same directory layout used by both the
# regular zip and the single-file payload.  The recorder is deliberately
# isolated so its Qt/LSL runtime cannot collide with the bridge runtime.
if ($LabRecorderDeployDir) {
    $labRecorderPath = Join-Path $deployPath "labrecorder"
    Remove-Item -LiteralPath $labRecorderPath -Recurse -Force -ErrorAction SilentlyContinue
    Copy-DeploymentTree $LabRecorderDeployDir $labRecorderPath
}
if ($StairModelDir) {
    $stairPath = Join-Path $deployPath "stair_model"
    Remove-Item -LiteralPath $stairPath -Recurse -Force -ErrorAction SilentlyContinue
    Copy-DeploymentTree $StairModelDir $stairPath
}

$guiExe = Join-Path $deployPath "vicon-lsl-bridge-gui.exe"
if (-not (Test-Path -LiteralPath $guiExe)) {
    throw "Expected GUI executable was not found: $guiExe"
}
$cliExe = Join-Path $deployPath "vicon-lsl-bridge.exe"
if (-not (Test-Path -LiteralPath $cliExe)) {
    throw "Expected bridge CLI executable was not found: $cliExe"
}

$qtPlatformPlugin = Join-Path $deployPath "platforms\qwindows.dll"
if (-not (Test-Path -LiteralPath $qtPlatformPlugin)) {
    throw "Qt platform plugin was not found. Run windeployqt first: $qtPlatformPlugin"
}

$lslRuntime = Get-ChildItem -LiteralPath $deployPath -File |
    Where-Object { $_.Name -match '^(lib)?lsl.*\.dll$' } |
    Select-Object -First 1
if (-not $lslRuntime) {
    throw "liblsl runtime DLL was not found in the deployment directory: $deployPath"
}

$stairModel = Join-Path $deployPath "stair_model\stair_model1.obj"
$stairMaterial = Join-Path $deployPath "stair_model\stair_model1.mtl"
if (-not (Test-Path -LiteralPath $stairModel) -or -not (Test-Path -LiteralPath $stairMaterial)) {
    throw "The deployment must contain stair_model/stair_model1.obj and stair_model1.mtl."
}

$labRecorderPath = Join-Path $deployPath "labrecorder"
if (-not (Test-Path -LiteralPath $labRecorderPath -PathType Container)) {
    throw "The deployment must contain an isolated labrecorder directory."
}
if (Test-Path -LiteralPath $labRecorderPath -PathType Container) {
    foreach ($required in @("LabRecorder.exe", "LabRecorderCLI.exe", "LabRecorder.cfg", "LICENSE")) {
        if (-not (Test-Path -LiteralPath (Join-Path $labRecorderPath $required))) {
            $located = Find-DeploymentFile $labRecorderPath $required
            if (-not $located) {
                throw "LabRecorder deployment is missing ${required}: $labRecorderPath"
            }
            Copy-Item -LiteralPath $located.FullName -Destination (Join-Path $labRecorderPath $required) -Force
        }
    }
    $recorderLsl = Get-ChildItem -LiteralPath $labRecorderPath -Recurse -File |
        Where-Object { $_.Name -match '^(lib)?lsl.*\.dll$' } | Select-Object -First 1
    if (-not $recorderLsl) {
        throw "LabRecorder deployment does not contain an lsl runtime DLL: $labRecorderPath"
    }
    if ($recorderLsl.Name -ne $lslRuntime.Name) {
        throw "LabRecorder lsl runtime ($($recorderLsl.Name)) does not match bridge runtime ($($lslRuntime.Name))."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $labRecorderPath "platforms\qwindows.dll"))) {
        throw "LabRecorder deployment is missing Qt platforms/qwindows.dll: $labRecorderPath"
    }
}

$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputExe)
$outputDir = Split-Path -Parent $outputPath
if ($outputDir -and -not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

$consolePath = Find-EnigmaConsole $EnigmaConsole
$projectPath = $null
if ($ProjectFile) {
    if (-not (Test-Path -LiteralPath $ProjectFile)) {
        throw "Enigma project file was not found: $ProjectFile"
    }
    $projectPath = (Resolve-Path -LiteralPath $ProjectFile).Path
}

$launcherPath = $null
if ($LauncherExe) {
    if (-not (Test-Path -LiteralPath $LauncherExe)) {
        throw "Portable launcher executable was not found: $LauncherExe"
    }
    $launcherPath = (Resolve-Path -LiteralPath $LauncherExe).Path
}

if ($Mode -eq "Enigma") {
    if (-not $consolePath) {
        throw "Enigma Virtual Box console was not found."
    }
    if (-not $projectPath) {
        throw "Enigma mode requires -ProjectFile."
    }
    Invoke-EnigmaPackage $consolePath $projectPath $outputPath
} elseif ($Mode -eq "Native") {
    Invoke-NativePackage $deployPath $launcherPath $outputPath
} elseif ($consolePath -and $projectPath) {
    Invoke-EnigmaPackage $consolePath $projectPath $outputPath
} else {
    Invoke-NativePackage $deployPath $launcherPath $outputPath
}

Write-Host "Portable GUI executable created: $outputPath"
