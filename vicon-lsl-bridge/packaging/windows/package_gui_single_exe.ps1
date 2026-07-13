param(
    [Parameter(Mandatory = $true)]
    [string]$DeployDir,

    [string]$OutputExe = "",

    [string]$OutputZip = "",

    [ValidateSet("Auto", "Enigma", "Native")]
    [string]$Mode = "Auto",

    [string]$LauncherExe = "",
    [string]$NativeLauncherPath = "",
    [string]$EnigmaConsole = $env:ENIGMA_VB_CONSOLE,
    [string]$ProjectFile = "",
    [string]$LabRecorderDeployDir = "",
    [string]$StairModelDir = "",
    [string]$LiblslSourceDir = "",
    [string]$QtRootDir = "",
    [string]$BoostRootDir = "",
    [string]$ViconSdkDir = "",
    [string]$LabRecorderSourceDir = "",
    [switch]$UseExistingLicenseBundle,
    [switch]$ValidateOnly,
    [string]$StageMarkerName = ".vicon-lsl-bridge-package-stage"
)

$ErrorActionPreference = "Stop"

if ($NativeLauncherPath) {
    if ($LauncherExe) {
        throw "Specify only one of -LauncherExe and -NativeLauncherPath."
    }
    $LauncherExe = $NativeLauncherPath
}

$packageManifestName = ".vicon-lsl-bridge-package-manifest"

if ($ValidateOnly -and -not $UseExistingLicenseBundle) {
    throw "-ValidateOnly requires -UseExistingLicenseBundle and an existing package manifest."
}
function Assert-NoReparseAncestors {
    param([string]$Path, [string]$Description = "path")

    try {
        $current = [System.IO.Path]::GetFullPath($Path)
    } catch {
        throw "$Description is not a valid filesystem path: $Path"
    }
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Description or an ancestor is a reparse point: $current"
            }
        }
        $parentInfo = [System.IO.Directory]::GetParent($current)
        $parent = if ($parentInfo) { $parentInfo.FullName } else { $null }
        if (-not $parent -or $parent -eq $current) {
            break
        }
        $current = $parent
    }
}

function Assert-NoReparseTree {
    param([string]$Root, [string]$Description = "tree")

    Assert-NoReparseAncestors $Root $Description
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "$Description was not found: $Root"
    }
    $rootItem = Get-Item -LiteralPath $Root -Force -ErrorAction Stop
    if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description is a reparse point: $Root"
    }
    $pending = New-Object System.Collections.Generic.Stack[string]
    $pending.Push($rootItem.FullName)
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Description contains a reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) {
                $pending.Push($item.FullName)
            }
        }
    }
}

function Remove-TreeSafe {
    param([string]$Path, [string]$Description = "tree")

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    Assert-NoReparseAncestors $Path $Description
    Assert-NoReparseTree $Path $Description
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
    if (Test-Path -LiteralPath $Path) {
        throw "Unable to remove ${Description}: $Path"
    }
}

function Get-PackageFilePaths {
    param([string]$Root)

    $rootPath = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path.TrimEnd('\') + '\'
    $markerPath = [System.IO.Path]::GetFullPath((Join-Path $Root $StageMarkerName))
    $manifestPath = [System.IO.Path]::GetFullPath((Join-Path $Root $packageManifestName))
    return @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force -ErrorAction Stop |
        Where-Object {
            $_.FullName -ne $markerPath -and $_.FullName -ne $manifestPath
        } |
        ForEach-Object {
            $relative = $_.FullName.Substring($rootPath.Length).Replace('\', '/')
            if ([System.IO.Path]::IsPathRooted($relative) -or $relative.Contains('..')) {
                throw "Package file escaped deployment root: $($_.FullName)"
            }
            $relative
        } | Sort-Object)
}

function Write-PackageManifest {
    param([string]$Root)

    $manifestPath = Join-Path $Root $packageManifestName
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $manifestLines = @(foreach ($relative in @(Get-PackageFilePaths $Root)) {
        $path = Join-Path $Root $relative.Replace('/', '\')
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    })
    [System.IO.File]::WriteAllLines($manifestPath, $manifestLines, $utf8NoBom)
}

function Validate-PackageManifest {
    param([string]$Root)

    $manifestPath = Join-Path $Root $packageManifestName
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Deployment directory is missing the deterministic package manifest: $manifestPath"
    }
    $manifestItem = Get-Item -LiteralPath $manifestPath -Force -ErrorAction Stop
    if (($manifestItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Package manifest is a reparse point: $manifestPath"
    }
    $manifestLines = @(Get-Content -LiteralPath $manifestPath -ErrorAction Stop)
    $expected = New-Object System.Collections.Generic.List[string]
    $expectedHashes = @{}
    foreach ($line in $manifestLines) {
        if ($line -notmatch '^(?<hash>[0-9a-f]{64})  (?<relative>[^\r\n]+)$') {
            throw "Package manifest contains an invalid line: $line"
        }
        $relative = $Matches.relative
        if ($relative -match '[\\]' -or [System.IO.Path]::IsPathRooted($relative) -or
            $relative.Contains('..')) {
            throw "Package manifest contains an invalid relative path: $relative"
        }
        if ($expectedHashes.ContainsKey($relative.ToLowerInvariant())) {
            throw "Package manifest contains a duplicate path: $relative"
        }
        $expected.Add($relative)
        $expectedHashes[$relative.ToLowerInvariant()] = $Matches.hash
    }
    $actual = @(Get-PackageFilePaths $Root)
    $expectedSorted = @($expected | Sort-Object)
    $actualSorted = @($actual | Sort-Object)
    if ($expectedSorted.Count -ne $actualSorted.Count -or
        ([string]::Join("`n", $expectedSorted) -cne [string]::Join("`n", $actualSorted))) {
        throw "Deployment directory does not match its deterministic package manifest; stale or unexpected files were found."
    }

    foreach ($relative in $actual) {
        $key = $relative.ToLowerInvariant()
        $path = Join-Path $Root $relative.Replace('/', '\')
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -cne $expectedHashes[$key]) {
            throw "Deployment file hash does not match the deterministic package manifest: $relative"
        }
    }
}

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
        $payloadEntries = @(Get-ChildItem -LiteralPath $SourceDirectory -Force -ErrorAction Stop |
            Where-Object { $_.Name -ne $StageMarkerName -and $_.Name -ne $packageManifestName })
        if ($payloadEntries.Count -eq 0) {
            throw "Portable deployment directory is empty after excluding package metadata."
        }
        Compress-Archive -Path @($payloadEntries.FullName) -DestinationPath $payload -Force
        Copy-Item -LiteralPath $LauncherPath -Destination $ExpectedOutput -Force

        # Bind the overlay payload to bytes inside the launcher image so the
        # extracted package can be checked independently of the PE image.
        $payloadHash = (Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash.ToLowerInvariant()
        $digestPlaceholder = [System.Text.Encoding]::ASCII.GetBytes(
            "VICONLSL_PAYLOAD_SHA256=" + ('0' * 64))
        $digestReplacement = [System.Text.Encoding]::ASCII.GetBytes(
            "VICONLSL_PAYLOAD_SHA256=" + $payloadHash)
        $launcherBytes = [System.IO.File]::ReadAllBytes($ExpectedOutput)
        $markerOffset = -1
        $markerCount = 0
        for ($offset = 0; $offset -le $launcherBytes.Length - $digestPlaceholder.Length; $offset++) {
            $match = $true
            for ($index = 0; $index -lt $digestPlaceholder.Length; $index++) {
                if ($launcherBytes[$offset + $index] -ne $digestPlaceholder[$index]) {
                    $match = $false
                    break
                }
            }
            if ($match) {
                $markerOffset = $offset
                $markerCount++
            }
        }
        if ($markerCount -ne 1) {
            throw "Portable launcher digest slot was expected exactly once, found $markerCount occurrences."
        }
        [System.Buffer]::BlockCopy(
            $digestReplacement, 0, $launcherBytes, $markerOffset, $digestReplacement.Length)
        [System.IO.File]::WriteAllBytes($ExpectedOutput, $launcherBytes)

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
        Remove-TreeSafe $work "portable packaging temporary directory"
    }
}

function Copy-DeploymentTree {
    param([string]$SourceDirectory, [string]$DestinationDirectory)

    if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
        throw "Deployment directory was not found: $SourceDirectory"
    }
    Assert-NoReparseTree $SourceDirectory "deployment source tree"
    Assert-NoReparseAncestors $DestinationDirectory "deployment destination"
    New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
    if ((Get-Item -LiteralPath $DestinationDirectory -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        throw "Deployment destination is a reparse point: $DestinationDirectory"
    }
    Get-ChildItem -LiteralPath $SourceDirectory -Force -ErrorAction Stop |
        Copy-Item -Destination $DestinationDirectory -Recurse -Force
    Assert-NoReparseTree $DestinationDirectory "copied deployment tree"
}

function Find-DeploymentFile {
    param([string]$Root, [string]$Name)
    $matches = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Name -ErrorAction Stop |
        Sort-Object FullName)
    if ($matches.Count -gt 1) {
        throw "Deployment contains multiple candidates named ${Name}: $Root"
    }
    if ($matches.Count -eq 1) {
        return $matches[0]
    }
    return $null
}

function Assert-X64PeFile {
    param([string]$Path, [string]$Description)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read)
    $reader = New-Object System.IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) {
            throw "$Description is not a PE executable: $Path"
        }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or ([int64]$peOffset + 6) -gt $stream.Length) {
            throw "$Description has an invalid PE header offset: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "$Description has an invalid PE signature: $Path"
        }
        if ($reader.ReadUInt16() -ne 0x8664) {
            throw "$Description is not an x64 PE file: $Path"
        }
    } finally {
        $reader.Dispose()
    }
}

function Assert-MsvcRuntime {
    param([string]$Root, [string]$Description)

    foreach ($required in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
        $path = Join-Path $Root $required
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "$Description is missing required x64 MSVC runtime DLL ${required}: $Root"
        }
        if ((Get-Item -LiteralPath $path).Length -le 0) {
            throw "$Description contains an empty MSVC runtime DLL ${required}: $Root"
        }
        Assert-X64PeFile $path "$Description MSVC runtime ${required}"
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne "Valid" -or -not $signature.SignerCertificate -or
            $signature.SignerCertificate.Subject -notmatch '(?i)^CN=Microsoft (Windows(?: Software Compatibility Publisher)?|Corporation)(,|$)') {
            throw "$Description contains an MSVC runtime DLL without a valid Microsoft signature: $path"
        }
    }
}

function Resolve-SafeOutputPath {
    param([string]$Path, [string]$Description)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Description path is required."
    }
    $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    Assert-NoReparseAncestors $resolved $Description
    $parent = Split-Path -Parent $resolved
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -ErrorAction Stop | Out-Null
    }
    Assert-NoReparseAncestors $resolved $Description
    if (Test-Path -LiteralPath $resolved) {
        $existing = Get-Item -LiteralPath $resolved -Force -ErrorAction Stop
        if (($existing.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description is a reparse point: $resolved"
        }
    }
    return $resolved
}

function New-PackageArchive {
    param([string]$SourceDirectory, [string]$DestinationPath)

    Assert-NoReparseTree $SourceDirectory "package archive source"
    $entries = @(Get-ChildItem -LiteralPath $SourceDirectory -Force -ErrorAction Stop |
        Where-Object { $_.Name -ne $StageMarkerName -and $_.Name -ne $packageManifestName })
    if ($entries.Count -eq 0) {
        throw "Package deployment directory is empty after excluding package metadata."
    }
    Compress-Archive -Path @($entries.FullName) -DestinationPath $DestinationPath -Force
    if (-not (Test-Path -LiteralPath $DestinationPath -PathType Leaf) -or
        (Get-Item -LiteralPath $DestinationPath).Length -le 0) {
        throw "Package archive was not created: $DestinationPath"
    }
}

function Validate-LicenseBundle {
    param([string]$Root)

    $requiredFiles = @(
        "THIRD_PARTY_NOTICES.txt",
        "VICON-DATASTREAM-SDK-LICENSE.txt",
        "LICENSE-INVENTORY.txt",
        "licenses\Vicon-DataStream-SDK\LICENSE",
        "licenses\LabRecorder\LICENSE",
        "licenses\liblsl\LICENSE",
        "licenses\liblsl\pugixml\readme.txt",
        "licenses\liblsl\loguru\LICENSE",
        "licenses\liblsl\portable-archive\license.txt",
        "licenses\Boost\LICENSE_1_0.txt")
    foreach ($relative in $requiredFiles) {
        $path = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Existing license bundle is missing $relative"
        }
    }
    $qtFiles = @(Get-ChildItem -LiteralPath (Join-Path $Root "licenses\Qt") -Recurse -File -ErrorAction SilentlyContinue)
    if ($qtFiles.Count -eq 0) {
        throw "Existing license bundle is missing Qt LICENSES texts"
    }

    $licensesRoot = (Resolve-Path -LiteralPath (Join-Path $Root "licenses")).Path.TrimEnd('\') + '\'
    $inventoryPath = Join-Path $Root "LICENSE-INVENTORY.txt"
    $inventoryLines = @(Get-Content -LiteralPath $inventoryPath)
    if ($inventoryLines.Count -eq 0) {
        throw "Existing license inventory is empty: $inventoryPath"
    }

    $seen = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $inventoryLines) {
        if ($line -notmatch '^(?<hash>[0-9a-fA-F]{64})  (?<relative>[^\r\n]+)$') {
            throw "Existing license inventory contains an invalid line: $line"
        }
        $relative = $Matches.relative.Replace('/', '\')
        if ([System.IO.Path]::IsPathRooted($relative) -or $relative.Contains('..')) {
            throw "Existing license inventory contains an unsafe path: $($Matches.relative)"
        }
        $path = Join-Path $Root (Join-Path "licenses" $relative)
        $resolved = (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path
        if (-not $resolved.StartsWith($licensesRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Existing license inventory escapes the licenses directory: $($Matches.relative)"
        }
        if (-not $seen.Add($relative)) {
            throw "Existing license inventory contains a duplicate path: $($Matches.relative)"
        }
        $actual = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $Matches.hash.ToLowerInvariant()) {
            throw "Existing license inventory hash mismatch for $($Matches.relative)"
        }
    }

    $actualFiles = @(Get-ChildItem -LiteralPath $licensesRoot -Recurse -File)
    if ($actualFiles.Count -ne $seen.Count) {
        throw "Existing license inventory does not cover every file under licenses (inventory=$($seen.Count), files=$($actualFiles.Count))"
    }
    foreach ($file in $actualFiles) {
        $relative = $file.FullName.Substring($licensesRoot.Length).Replace('\', '\')
        if (-not $seen.Contains($relative)) {
            throw "Existing license bundle contains an unlisted file: $relative"
        }
    }

    $requiredMatches = @(
        @{
            Packaged = Join-Path $Root "THIRD_PARTY_NOTICES.txt"
            Expected = Join-Path $PSScriptRoot "THIRD_PARTY_NOTICES.txt"
            Description = "third-party notice"
        },
        @{
            Packaged = Join-Path $Root "VICON-DATASTREAM-SDK-LICENSE.txt"
            Expected = Join-Path $Root "licenses\Vicon-DataStream-SDK\LICENSE"
            Description = "Vicon DataStream SDK root license"
        })
    foreach ($match in $requiredMatches) {
        $packagedHash = (Get-FileHash -LiteralPath $match.Packaged -Algorithm SHA256).Hash
        $expectedHash = (Get-FileHash -LiteralPath $match.Expected -Algorithm SHA256).Hash
        if ($packagedHash -cne $expectedHash) {
            throw "Existing $($match.Description) does not match its audited source copy."
        }
    }
    Write-Host "Validated existing license bundle ($($seen.Count) files)."
}

function Find-BoostRoot {
    param([string]$RequestedPath)
    if ($RequestedPath) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }
    if (-not $env:VCPKG_INSTALLATION_ROOT) {
        return $null
    }
    $installed = Join-Path $env:VCPKG_INSTALLATION_ROOT "installed"
    if (-not (Test-Path -LiteralPath $installed -PathType Container)) {
        return $null
    }
    foreach ($triplet in Get-ChildItem -LiteralPath $installed -Directory | Sort-Object FullName) {
        $license = Get-ChildItem -LiteralPath $triplet.FullName -Recurse -File -Filter "LICENSE_1_0.txt" |
            Select-Object -First 1
        if ($license) {
            return $triplet.FullName
        }
    }
    return $null
}

function Find-QtRoot {
    param([string]$RequestedPath)
    if ($RequestedPath) {
        $path = (Resolve-Path -LiteralPath $RequestedPath).Path
        if ((Split-Path -Leaf $path) -ieq "bin") {
            return (Split-Path -Parent $path)
        }
        return $path
    }
    $windeploy = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if (-not $windeploy) {
        $windeploy = Get-Command windeployqt -ErrorAction SilentlyContinue
    }
    if (-not $windeploy) {
        return $null
    }
    $windeployPath = if ($windeploy.Source) { $windeploy.Source } else { $windeploy.Path }
    return (Split-Path -Parent (Split-Path -Parent $windeployPath))
}

function Find-LiblslSource {
    param([string]$RequestedPath)
    if ($RequestedPath) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }
    $candidate = Join-Path $PSScriptRoot "..\..\..\hololens-gaze-lsl\external\liblsl"
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }
    return $null
}

function Find-SourceDirectory {
    param([string]$RequestedPath, [string]$Fallback)
    if ($RequestedPath) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }
    if (Test-Path -LiteralPath $Fallback -PathType Container) {
        return (Resolve-Path -LiteralPath $Fallback).Path
    }
    return $null
}

$deployPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DeployDir)
Assert-NoReparseAncestors $deployPath "deployment directory"
New-Item -ItemType Directory -Path $deployPath -Force | Out-Null
if (-not (Test-Path -LiteralPath $deployPath -PathType Container)) {
    throw "Deployment directory could not be created: $deployPath"
}
Assert-NoReparseTree $deployPath "deployment directory"

$stageMarkerPath = Join-Path $deployPath $StageMarkerName
if (-not (Test-Path -LiteralPath $stageMarkerPath -PathType Leaf)) {
    throw "Deployment directory must be a fresh CMake staging tree containing ${StageMarkerName}: $deployPath"
}
if (-not $UseExistingLicenseBundle -and (Test-Path -LiteralPath (Join-Path $deployPath $packageManifestName))) {
    throw "A fresh package staging tree must not already contain a deterministic package manifest: $deployPath"
}
if ($UseExistingLicenseBundle) {
    Validate-PackageManifest $deployPath
}

# Keep deployment inputs in the same directory layout used by both the
# regular zip and the single-file payload.  The recorder is deliberately
# isolated so its Qt/LSL runtime cannot collide with the bridge runtime.
if ($LabRecorderDeployDir) {
    $labRecorderPath = Join-Path $deployPath "labrecorder"
    Remove-TreeSafe $labRecorderPath "LabRecorder deployment directory"
    Copy-DeploymentTree $LabRecorderDeployDir $labRecorderPath
}
if ($StairModelDir) {
    $stairPath = Join-Path $deployPath "stair_model"
    Remove-TreeSafe $stairPath "stair-model deployment directory"
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
Assert-X64PeFile $guiExe "Bridge GUI executable"
Assert-X64PeFile $cliExe "Bridge CLI executable"

$qtPlatformPlugin = Join-Path $deployPath "platforms\qwindows.dll"
if (-not (Test-Path -LiteralPath $qtPlatformPlugin)) {
    throw "Qt platform plugin was not found. Run windeployqt first: $qtPlatformPlugin"
}
Assert-X64PeFile $qtPlatformPlugin "Bridge Qt platform plugin"

$lslRuntime = Get-ChildItem -LiteralPath $deployPath -File |
    Where-Object { $_.Name -match '^(lib)?lsl.*\.dll$' } |
    Select-Object -First 1
if (-not $lslRuntime) {
    throw "liblsl runtime DLL was not found in the deployment directory: $deployPath"
}
Assert-X64PeFile $lslRuntime.FullName "Bridge liblsl runtime"
Assert-MsvcRuntime $deployPath "Bridge deployment"

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
    Assert-X64PeFile (Join-Path $labRecorderPath "LabRecorder.exe") "LabRecorder GUI executable"
    Assert-X64PeFile (Join-Path $labRecorderPath "LabRecorderCLI.exe") "LabRecorder CLI executable"
    Assert-X64PeFile $recorderLsl.FullName "LabRecorder liblsl runtime"
    Assert-X64PeFile (Join-Path $labRecorderPath "platforms\qwindows.dll") "LabRecorder Qt platform plugin"
    Assert-MsvcRuntime $labRecorderPath "LabRecorder deployment"
}

# License collection is mandatory for a fresh package. Reusing an existing
# bundle requires validating its immutable inventory before packaging.
if ($UseExistingLicenseBundle) {
    Validate-LicenseBundle $deployPath
} else {
    $collector = Join-Path $PSScriptRoot "collect_license_bundle.ps1"
    $viconSdkSource = Find-SourceDirectory $ViconSdkDir (Join-Path $PSScriptRoot "..\..\external\vicon-datastream-sdk")
    $labRecorderSource = Find-SourceDirectory $LabRecorderSourceDir (Join-Path $PSScriptRoot "..\..\..\labrecorder")
    $liblslSource = Find-LiblslSource $LiblslSourceDir
    $qtRoot = Find-QtRoot $QtRootDir
    $boostRoot = Find-BoostRoot $BoostRootDir
    $requiredSources = [ordered]@{
        "Vicon DataStream SDK" = $viconSdkSource
        "LabRecorder" = $labRecorderSource
        "liblsl" = $liblslSource
        "Qt" = $qtRoot
        "Boost" = $boostRoot
    }
    foreach ($requiredSource in $requiredSources.GetEnumerator()) {
        if (-not $requiredSource.Value) {
            throw "Unable to locate the $($requiredSource.Key) source required for the release license bundle."
        }
    }
    & $collector `
        -OutputDirectory $deployPath `
        -ViconSdkDirectory $viconSdkSource `
        -LabRecorderSourceDirectory $labRecorderSource `
        -LiblslSourceDirectory $liblslSource `
        -QtRootDirectory $qtRoot `
        -BoostRootDirectory $boostRoot
    Validate-LicenseBundle $deployPath
}

Assert-NoReparseTree $deployPath "final deployment tree"
if ($UseExistingLicenseBundle) {
    Validate-PackageManifest $deployPath
} else {
    Write-PackageManifest $deployPath
    Validate-PackageManifest $deployPath
}

if ($ValidateOnly) {
    Write-Host "Validated existing portable package tree: $deployPath"
    return
}

if (-not $OutputExe -and -not $OutputZip) {
    throw "Specify -OutputExe, -OutputZip, or -ValidateOnly."
}

if ($OutputZip) {
    $zipPath = Resolve-SafeOutputPath $OutputZip "Windows ZIP output path"
    New-PackageArchive $deployPath $zipPath
    Write-Host "Windows package ZIP created: $zipPath"
}

if ($OutputExe) {
    $outputPath = Resolve-SafeOutputPath $OutputExe "portable output path"

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
}
