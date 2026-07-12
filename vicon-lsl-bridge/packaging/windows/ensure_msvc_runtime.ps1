param([string]$DeployDirectory)

if (-not $DeployDirectory) {
    throw "DeployDirectory is required."
}

$ErrorActionPreference = "Stop"

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

function Assert-X64PeFile {
    param([string]$Path, [string]$Description)

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
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "$Description is not an x64 PE file: $Path"
        }
    } finally {
        $reader.Dispose()
    }
}

# The CRT is deployed from the x64 VC143 redist directory that belongs to the
# installed Visual Studio toolchain.  Keep this allow-list explicit: it covers
# the split CRT components emitted by current MSVC builds without copying
# unrelated files from the redist directory.
$requiredDlls = @(
    "msvcp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "concrt140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll"
)

$mandatoryDlls = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")

function Add-RedistRoot {
    param(
        [System.Collections.IList]$Roots,
        [string]$Path
    )

    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        return
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    Assert-NoReparseTree $resolved "Visual Studio redist source"
    if (-not $Roots.Contains($resolved)) {
        $Roots.Add($resolved)
    }
}

function Get-CrtCandidates {
    param(
        [string]$Root,
        [string]$TrustedInstallation
    )

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        return @()
    }

    $rootPath = (Resolve-Path -LiteralPath $Root).Path
    Assert-NoReparseTree $rootPath "Visual Studio redist source"
    Assert-NoReparseAncestors $TrustedInstallation "Visual Studio installation"
    $trustedPath = (Resolve-Path -LiteralPath $TrustedInstallation).Path.TrimEnd('\') + '\'
    if (-not $rootPath.StartsWith($trustedPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing VC143 redist path outside the Visual Studio installation: $rootPath"
    }
    $directories = New-Object System.Collections.Generic.List[string]
    $leaf = Split-Path -Leaf $rootPath
    if ($leaf -ieq "Microsoft.VC143.CRT" -and (Split-Path -Leaf (Split-Path -Parent $rootPath)) -ieq "x64") {
        $directories.Add($rootPath)
    } elseif ($leaf -ieq "x64") {
        $direct = Join-Path $rootPath "Microsoft.VC143.CRT"
        if (Test-Path -LiteralPath $direct -PathType Container) {
            $directories.Add((Resolve-Path -LiteralPath $direct).Path)
        }
    } else {
        $direct = Join-Path $rootPath "x64\Microsoft.VC143.CRT"
        if (Test-Path -LiteralPath $direct -PathType Container) {
            $directories.Add((Resolve-Path -LiteralPath $direct).Path)
        }
        foreach ($directory in @(Get-ChildItem -LiteralPath $rootPath -Directory -Recurse -Filter "Microsoft.VC143.CRT" -ErrorAction SilentlyContinue)) {
            if ((Split-Path -Leaf (Split-Path -Parent $directory.FullName)) -ieq "x64") {
                if (-not $directories.Contains($directory.FullName)) {
                    $directories.Add($directory.FullName)
                }
            }
        }
    }

    foreach ($directory in $directories) {
        $dlls = @(foreach ($name in $requiredDlls) {
            $path = Join-Path $directory $name
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                Get-Item -LiteralPath $path
            }
        })
        if ($dlls.Count -eq 0) {
            continue
        }
        $versionDirectory = Split-Path -Parent (Split-Path -Parent $directory)
        $version = [version]"0.0"
        try {
            $version = [version](Split-Path -Leaf $versionDirectory)
        } catch {
            # Keep an unparseable version below parseable VC143 versions.
        }
        [pscustomobject]@{
            Path = $directory
            Version = $version
            Dlls = $dlls
        }
    }
}

function Find-LatestCrtDirectory {
    $installations = New-Object System.Collections.Generic.List[string]
    $programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    if (-not $programFilesX86) {
        throw "ProgramFiles(x86) is unavailable; cannot locate the trusted Visual Studio installer."
    }
    $vswherePath = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
        throw "Trusted Visual Studio locator was not found: $vswherePath"
    }
    $vswherePath = (Resolve-Path -LiteralPath $vswherePath).Path
    Assert-NoReparseAncestors $vswherePath "Visual Studio locator"
    $programFilesPrefix = ((Resolve-Path -LiteralPath $programFilesX86).Path).TrimEnd('\') + '\'
    if (-not $vswherePath.StartsWith($programFilesPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        ((Get-Item -LiteralPath $vswherePath).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing Visual Studio locator outside the trusted Program Files tree: $vswherePath"
    }
    Assert-MicrosoftSignedTool $vswherePath
    $located = @(& $vswherePath -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
    if ($LASTEXITCODE -ne 0) {
        throw "vswhere failed while locating a Visual Studio installation."
    }
    foreach ($installation in $located) {
        if ($installation -and (Test-Path -LiteralPath ([string]$installation) -PathType Container)) {
            $resolved = (Resolve-Path -LiteralPath ([string]$installation)).Path
            if (-not $installations.Contains($resolved)) {
                $installations.Add($resolved)
            }
        }
    }
    if ($installations.Count -eq 0) {
        throw "No Visual Studio installation with the x64 VC tools was found by vswhere."
    }

    $roots = New-Object System.Collections.Generic.List[object]
    foreach ($installation in $installations) {
        $redistRoot = Join-Path $installation "VC\Redist\MSVC"
        Add-RedistRoot $roots $redistRoot
        foreach ($preferred in @([Environment]::GetEnvironmentVariable("VCToolsRedistDir"))) {
            if ($preferred -and (Test-Path -LiteralPath $preferred -PathType Container)) {
                $preferredPath = (Resolve-Path -LiteralPath $preferred).Path
                $trustedPrefix = $installation.TrimEnd('\') + '\'
                if ($preferredPath.StartsWith($trustedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                    Add-RedistRoot $roots $preferredPath
                }
            }
        }
    }

    $candidates = @()
    foreach ($root in $roots) {
        $installation = $installations | Where-Object {
            $prefix = $_.TrimEnd('\') + '\'
            $root.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
        } | Select-Object -First 1
        if ($installation) {
            $candidates += @(Get-CrtCandidates $root $installation)
        }
    }
    $candidates = @($candidates | Sort-Object Version, Path -Descending)
    foreach ($candidate in $candidates) {
        $names = @($candidate.Dlls | ForEach-Object { $_.Name.ToLowerInvariant() })
        if (@($mandatoryDlls | Where-Object { $names -notcontains $_ }).Count -eq 0) {
            return $candidate
        }
    }

    $searched = if ($roots.Count -gt 0) { ($roots -join "; ") } else { "none" }
    throw "Unable to locate an x64 VC143 CRT redist containing msvcp140.dll, vcruntime140.dll, and vcruntime140_1.dll. Searched: $searched"
}

function Assert-MicrosoftSignedDll {
    param([System.IO.FileInfo]$File)

    $signature = Get-AuthenticodeSignature -LiteralPath $File.FullName
    if ($signature.Status -ne "Valid" -or -not $signature.SignerCertificate) {
        throw "MSVC runtime DLL is not Authenticode-valid: $($File.FullName)"
    }
    $subject = [string]$signature.SignerCertificate.Subject
    if ($subject -notmatch '(?i)^CN=Microsoft (Windows(?: Software Compatibility Publisher)?|Corporation)(,|$)') {
        throw "MSVC runtime DLL is not signed by Microsoft: $($File.FullName) ($subject)"
    }
    Assert-X64PeFile $File.FullName "MSVC runtime DLL"
}

function Assert-MicrosoftSignedTool {
    param([string]$Path)

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne "Valid" -or -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch '(?i)^CN=Microsoft (Windows(?: Software Compatibility Publisher)?|Corporation)(,|$)') {
        throw "Trusted Visual Studio locator is not Authenticode-valid Microsoft code: $Path"
    }
}

$deployPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DeployDirectory)
Assert-NoReparseAncestors $deployPath "CRT deployment directory"
New-Item -ItemType Directory -Path $deployPath -Force | Out-Null
if (-not (Test-Path -LiteralPath $deployPath -PathType Container)) {
    throw "Deployment directory could not be created: $deployPath"
}
Assert-NoReparseAncestors $deployPath "CRT deployment directory"

$candidate = Find-LatestCrtDirectory
$selectedDlls = New-Object System.Collections.Generic.List[System.IO.FileInfo]
foreach ($name in $requiredDlls) {
    $source = Join-Path $candidate.Path $name
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        $file = Get-Item -LiteralPath $source
        Assert-MicrosoftSignedDll $file
        $selectedDlls.Add($file)
    }
}
foreach ($mandatory in $mandatoryDlls) {
    if (-not ($selectedDlls.Name -contains $mandatory)) {
        throw "Selected CRT redist is missing required DLL ${mandatory}: $($candidate.Path)"
    }
}
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("vicon-lsl-msvc-runtime-" + [guid]::NewGuid().ToString("N"))
try {
    Assert-NoReparseAncestors $stage "CRT staging directory"
    New-Item -ItemType Directory -Path $stage -ErrorAction Stop | Out-Null
    foreach ($dll in @($selectedDlls | Sort-Object Name)) {
        # DLLs only: do not copy PDBs, manifests, catalog files, or signatures.
        Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $stage $dll.Name) -Force -ErrorAction Stop
    }
    Assert-NoReparseTree $stage "CRT staging directory"
    foreach ($required in $mandatoryDlls) {
        $staged = Join-Path $stage $required
        if (-not (Test-Path -LiteralPath $staged -PathType Leaf) -or (Get-Item -LiteralPath $staged).Length -le 0) {
            throw "Selected CRT redist is missing required DLL ${required}: $($candidate.Path)"
        }
        Assert-MicrosoftSignedDll (Get-Item -LiteralPath $staged -Force)
    }
    foreach ($stagedDll in @(Get-ChildItem -LiteralPath $stage -File -Filter "*.dll")) {
        $destination = Join-Path $deployPath $stagedDll.Name
        Assert-NoReparseAncestors $destination "CRT deployment destination"
        if (Test-Path -LiteralPath $destination) {
            $existing = Get-Item -LiteralPath $destination -Force -ErrorAction Stop
            if (($existing.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing to overwrite a reparse-point CRT destination: $destination"
            }
        }
        Copy-Item -LiteralPath $stagedDll.FullName -Destination $destination -Force -ErrorAction Stop
        Assert-MicrosoftSignedDll (Get-Item -LiteralPath $destination -Force)
    }
    Assert-NoReparseAncestors $deployPath "completed CRT deployment directory"
    foreach ($required in $mandatoryDlls) {
        $deployed = Join-Path $deployPath $required
        if (-not (Test-Path -LiteralPath $deployed -PathType Leaf) -or (Get-Item -LiteralPath $deployed).Length -le 0) {
            throw "CRT deployment is missing required DLL ${required}: $deployPath"
        }
    }
    Write-Host "Deployed x64 VC143 CRT DLLs from $($candidate.Path) to $deployPath"
} finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-TreeSafe $stage "CRT staging directory"
    }
}
