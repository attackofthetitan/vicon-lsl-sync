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
    param(
        [string]$Root,
        [string]$Description = "tree",
        [ValidateSet("NotFound", "NotNormalDirectory")]
        [string]$InvalidRootError = "NotNormalDirectory"
    )

    Assert-NoReparseAncestors $Root $Description
    if ($InvalidRootError -eq "NotFound") {
        if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
            throw "$Description was not found: $Root"
        }
        $rootItem = Get-Item -LiteralPath $Root -Force -ErrorAction Stop
        if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description is a reparse point: $Root"
        }
    } else {
        $rootItem = Get-Item -LiteralPath $Root -Force -ErrorAction Stop
        if (-not $rootItem.PSIsContainer -or
            ($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description is not a normal directory: $Root"
        }
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
    param(
        [string]$Path,
        [string]$Description = "tree",
        [ValidateSet("NotFound", "NotNormalDirectory")]
        [string]$InvalidRootError = "NotNormalDirectory"
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    Assert-NoReparseAncestors $Path $Description
    Assert-NoReparseTree $Path $Description -InvalidRootError $InvalidRootError
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
    if (Test-Path -LiteralPath $Path) {
        throw "Unable to remove ${Description}: $Path"
    }
}

function Assert-X64PeFile {
    param(
        [string]$Path,
        [string]$Description,
        [ValidateSet("Detailed", "Compact")]
        [string]$ErrorMode = "Detailed"
    )

    if ($ErrorMode -eq "Detailed" -and
        -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
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
        if ($ErrorMode -eq "Detailed") {
            if ($reader.ReadUInt32() -ne 0x00004550) {
                throw "$Description has an invalid PE signature: $Path"
            }
            if ($reader.ReadUInt16() -ne 0x8664) {
                throw "$Description is not an x64 PE file: $Path"
            }
        } elseif ($reader.ReadUInt32() -ne 0x00004550 -or
                  $reader.ReadUInt16() -ne 0x8664) {
            throw "$Description is not an x64 PE file: $Path"
        }
    } finally {
        $reader.Dispose()
    }
}

function Get-MandatoryMsvcRuntimeDllNames {
    return @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
}

Export-ModuleMember -Function @(
    "Assert-NoReparseAncestors",
    "Assert-NoReparseTree",
    "Remove-TreeSafe",
    "Assert-X64PeFile",
    "Get-MandatoryMsvcRuntimeDllNames"
)
