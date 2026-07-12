param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$ViconSdkDirectory,

    [Parameter(Mandatory = $true)]
    [string]$LabRecorderSourceDirectory,

    [Parameter(Mandatory = $true)]
    [string]$LiblslSourceDirectory,

    [Parameter(Mandatory = $true)]
    [string]$QtRootDirectory,

    [Parameter(Mandatory = $true)]
    [string]$BoostRootDirectory
)

$ErrorActionPreference = "Stop"

$expectedRevisions = @{
    Vicon = "a5096f283f484acca98b434c08810cd922551701"
    LabRecorder = "6d65fa96b94d049478ef4f7188c39202fe14977d"
    liblsl = "6ca188c266c21f7228dc67077303fa6abaf2e8be"
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
    $rootItem = Get-Item -LiteralPath $Root -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer -or
        ($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description is not a normal directory: $Root"
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

function Assert-GitRevision {
    param([string]$Root, [string]$Expected, [string]$Description)

    $gitDirectory = Join-Path $Root ".git"
    if (-not (Test-Path -LiteralPath $gitDirectory)) {
        throw "$Description must be a git checkout at the pinned revision; .git was not found: $Root"
    }
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) {
        $git = Get-Command git -ErrorAction SilentlyContinue
    }
    if (-not $git) {
        throw "git is required to verify the pinned $Description revision."
    }
    $gitPath = if ($git.Source) { $git.Source } else { $git.Path }
    $resolved = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path
    $head = (& $gitPath -C $resolved rev-parse --verify HEAD 2>$null).Trim()
    if ($LASTEXITCODE -ne 0 -or $head -ne $Expected) {
        throw "$Description revision mismatch: expected $Expected, got '$head'."
    }
    & $gitPath -C $resolved diff --quiet -- 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "$Description checkout has tracked working-tree changes; refusing to claim the pinned license text."
    }
    & $gitPath -C $resolved diff --cached --quiet -- 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "$Description checkout has staged working-tree changes; refusing to claim the pinned license text."
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

function Require-File {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required $Description was not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Require-Directory {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Required $Description was not found: $Path"
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    Assert-NoReparseTree $resolved $Description
    return $resolved
}

function Relative-Path {
    param([string]$Root, [string]$Path)
    $prefix = $Root.TrimEnd('\', '/') + '\'
    if ($Path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring($prefix.Length).Replace('\', '/')
    }
    return ([System.IO.Path]::GetFileName($Path)).Replace('\', '/')
}

function Copy-License {
    param([string]$Source, [string]$RelativeDestination)
    $sourcePath = Require-File $Source "license text"
    $destination = Join-Path $licensesDirectory $RelativeDestination
    $destinationParent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $destination -Force
}

$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
Assert-NoReparseAncestors $outputPath "license bundle output directory"
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
Assert-NoReparseTree $outputPath "license bundle output directory"
$licensesDirectory = Join-Path $outputPath "licenses"
if (Test-Path -LiteralPath $licensesDirectory) {
    Remove-TreeSafe $licensesDirectory "stale license bundle directory"
}
New-Item -ItemType Directory -Path $licensesDirectory -Force | Out-Null

$viconSdkPath = Require-Directory $ViconSdkDirectory "Vicon DataStream SDK source directory"
$labRecorderPath = Require-Directory $LabRecorderSourceDirectory "LabRecorder source directory"
$liblslPath = Require-Directory $LiblslSourceDirectory "liblsl source directory"
$qtRootPath = Require-Directory $QtRootDirectory "Qt installation root"
$boostRootPath = Require-Directory $BoostRootDirectory "Boost installation root"

Assert-GitRevision $viconSdkPath $expectedRevisions.Vicon "Vicon DataStream SDK"
Assert-GitRevision $labRecorderPath $expectedRevisions.LabRecorder "LabRecorder"
Assert-GitRevision $liblslPath $expectedRevisions.liblsl "liblsl"

$noticeSource = Require-File (Join-Path $PSScriptRoot "THIRD_PARTY_NOTICES.txt") "third-party notice"
Copy-Item -LiteralPath $noticeSource -Destination (Join-Path $outputPath "THIRD_PARTY_NOTICES.txt") -Force

# These are copied from the exact source trees used by the build.  Do not
# replace them with summaries: the release must contain the upstream text.
$viconLicense = Require-File (Join-Path $viconSdkPath "LICENSE") "Vicon DataStream SDK license"
Copy-Item -LiteralPath $viconLicense -Destination (Join-Path $outputPath "VICON-DATASTREAM-SDK-LICENSE.txt") -Force
Copy-License $viconLicense "Vicon-DataStream-SDK\LICENSE"
Copy-License (Join-Path $labRecorderPath "LICENSE") "LabRecorder\LICENSE"
Copy-License (Join-Path $liblslPath "LICENSE") "liblsl\LICENSE"
Copy-License (Join-Path $liblslPath "thirdparty\pugixml\readme.txt") "liblsl\pugixml\readme.txt"
Copy-License (Join-Path $liblslPath "thirdparty\loguru\LICENSE") "liblsl\loguru\LICENSE"
Copy-License (Join-Path $liblslPath "src\portable_archive\license.txt") "liblsl\portable-archive\license.txt"

$boostShare = Join-Path $boostRootPath "share"
$boostLicense = $null
if (Test-Path -LiteralPath $boostShare -PathType Container) {
    $boostLicense = Get-ChildItem -LiteralPath $boostShare -Directory -Filter "boost*" |
        Sort-Object FullName |
        ForEach-Object {
            $copyright = Join-Path $_.FullName "copyright"
            if (Test-Path -LiteralPath $copyright -PathType Leaf) {
                Get-Item -LiteralPath $copyright
            }
        } |
        Select-Object -First 1
}
if (-not $boostLicense) {
    $boostLicense = Get-ChildItem -LiteralPath $boostRootPath -Recurse -File -Filter "LICENSE_1_0.txt" |
        Sort-Object FullName |
        Select-Object -First 1
}
if (-not $boostLicense) {
    throw "Required Boost Software License text was not found under: $boostRootPath"
}
if ((Get-Content -LiteralPath $boostLicense.FullName -Raw) -notmatch
    'Boost Software License\s*-\s*Version 1\.0') {
    throw "Selected vcpkg Boost copyright file does not contain the Boost Software License 1.0 text: $($boostLicense.FullName)"
}
Copy-License $boostLicense.FullName "Boost\LICENSE_1_0.txt"

# install-qt-action installs all Qt and plugin notices beneath LICENSES.
# Copy every file so plugin-specific notices are retained, not just LGPL.txt.
$qtLicenses = Join-Path $qtRootPath "LICENSES"
if (-not (Test-Path -LiteralPath $qtLicenses -PathType Container)) {
    $qtLicenses = Get-ChildItem -LiteralPath $qtRootPath -Directory -Filter "LICENSES" -Recurse |
        Sort-Object FullName |
        Select-Object -First 1 |
        ForEach-Object { $_.FullName }
}
$qtLicenses = Require-Directory $qtLicenses "Qt LICENSES directory"
$qtLicenseFiles = @(Get-ChildItem -LiteralPath $qtLicenses -Recurse -File | Sort-Object FullName)
if ($qtLicenseFiles.Count -eq 0) {
    throw "Qt LICENSES directory is empty: $qtLicenses"
}
foreach ($qtFile in $qtLicenseFiles) {
    $relative = Relative-Path $qtLicenses $qtFile.FullName
    Copy-License $qtFile.FullName (Join-Path "Qt" $relative)
}

# A sorted hash inventory makes the license bundle auditable and reproducible.
$inventory = New-Object System.Collections.Generic.List[string]
foreach ($licenseFile in @(Get-ChildItem -LiteralPath $licensesDirectory -Recurse -File | Sort-Object FullName)) {
    $relative = Relative-Path $licensesDirectory $licenseFile.FullName
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hash = ([System.BitConverter]::ToString(
            $sha256.ComputeHash([System.IO.File]::ReadAllBytes($licenseFile.FullName))
        )).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
    $inventory.Add("$hash  $relative")
}
$inventoryPath = Join-Path $outputPath "LICENSE-INVENTORY.txt"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($inventoryPath, $inventory, $utf8NoBom)

Assert-NoReparseTree $outputPath "completed license bundle"

Write-Host "License bundle created at $licensesDirectory ($($inventory.Count) files)."
