$ErrorActionPreference = "Stop"

$packagingRoot = Split-Path -Parent $PSScriptRoot
$modulePath = (Resolve-Path -LiteralPath (
    Join-Path $packagingRoot "PackagingSafety.psm1") -ErrorAction Stop).Path
Import-Module -Name $modulePath -Scope Local -Force `
    -DisableNameChecking -ErrorAction Stop

$script:passed = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)

    if (-not $Condition) {
        throw $Message
    }
    $script:passed++
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)

    if ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
    $script:passed++
}

function Assert-ThrowsExactly {
    param([scriptblock]$Action, [string]$ExpectedMessage)

    $thrown = $null
    try {
        & $Action
    } catch {
        $thrown = $_.Exception.Message
    }
    if ($null -eq $thrown) {
        throw "Expected an exception with message '$ExpectedMessage'."
    }
    Assert-Equal $ExpectedMessage $thrown "Exception message changed."
}

function Write-PeFixture {
    param(
        [string]$Path,
        [uint16]$Machine,
        [uint32]$Signature = 0x00004550,
        [int32]$PeOffset = 64
    )

    [byte[]]$bytes = New-Object byte[] 128
    [BitConverter]::GetBytes([uint16]0x5a4d).CopyTo($bytes, 0)
    [BitConverter]::GetBytes($PeOffset).CopyTo($bytes, 0x3c)
    if ($PeOffset -ge 0 -and ($PeOffset + 6) -le $bytes.Length) {
        [BitConverter]::GetBytes($Signature).CopyTo($bytes, $PeOffset)
        [BitConverter]::GetBytes($Machine).CopyTo($bytes, $PeOffset + 4)
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

$sourceFiles = @(
    $modulePath,
    (Join-Path $packagingRoot "package_gui_single_exe.ps1"),
    (Join-Path $packagingRoot "ensure_msvc_runtime.ps1"),
    (Join-Path $packagingRoot "collect_license_bundle.ps1"),
    $PSCommandPath
)
foreach ($sourceFile in $sourceFiles) {
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        $sourceFile,
        [ref]$tokens,
        [ref]$errors) | Out-Null
    Assert-Equal 0 @($errors).Count "PowerShell parse errors in $sourceFile."
}

$module = Get-Module | Where-Object { $_.Path -eq $modulePath } | Select-Object -First 1
$expectedExports = @(
    "Assert-NoReparseAncestors",
    "Assert-NoReparseTree",
    "Assert-X64PeFile",
    "Get-MandatoryMsvcRuntimeDllNames",
    "Remove-TreeSafe"
) | Sort-Object
$actualExports = @($module.ExportedFunctions.Keys) | Sort-Object
Assert-Equal ($expectedExports -join "|") ($actualExports -join "|") `
    "PackagingSafety exported function surface changed."
Assert-Equal 0 @($module.ExportedAliases.Keys).Count `
    "PackagingSafety unexpectedly exported aliases."
Assert-Equal 0 @($module.ExportedVariables.Keys).Count `
    "PackagingSafety unexpectedly exported variables."

$mandatoryDlls = @(Get-MandatoryMsvcRuntimeDllNames)
Assert-Equal "msvcp140.dll|vcruntime140.dll|vcruntime140_1.dll" `
    ($mandatoryDlls -join "|") "Mandatory MSVC runtime list changed."

$tempBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$testRoot = Join-Path $tempBase (
    "vicon-lsl-packaging-safety-" + [Guid]::NewGuid().ToString("N"))
$resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
if (-not $resolvedTestRoot.StartsWith(
        $tempBase,
        [System.StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $resolvedTestRoot) -notlike "vicon-lsl-packaging-safety-*") {
    throw "Refusing to use an unsafe fixture root: $resolvedTestRoot"
}

$junctionPath = $null
try {
    New-Item -ItemType Directory -Path $resolvedTestRoot -ErrorAction Stop | Out-Null

    $ordinaryTree = Join-Path $resolvedTestRoot "ordinary"
    $nestedTree = Join-Path $ordinaryTree "nested"
    New-Item -ItemType Directory -Path $nestedTree -ErrorAction Stop | Out-Null
    Assert-NoReparseAncestors $nestedTree "ordinary ancestors"
    Assert-NoReparseTree $ordinaryTree "ordinary tree"
    $script:passed += 2

    $plainFile = Join-Path $resolvedTestRoot "plain-file.txt"
    [System.IO.File]::WriteAllText($plainFile, "fixture")
    Assert-ThrowsExactly {
        Assert-NoReparseTree $plainFile "fixture tree" -InvalidRootError NotFound
    } "fixture tree was not found: $plainFile"
    Assert-ThrowsExactly {
        Assert-NoReparseTree $plainFile "fixture tree"
    } "fixture tree is not a normal directory: $plainFile"

    $missingTree = Join-Path $resolvedTestRoot "missing"
    Remove-TreeSafe $missingTree "missing fixture"
    Assert-True (-not (Test-Path -LiteralPath $missingTree)) `
        "Removing a missing tree must remain a no-op."

    $removableTree = Join-Path $resolvedTestRoot "remove-me"
    New-Item -ItemType Directory -Path $removableTree -ErrorAction Stop | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $removableTree "payload.txt"), "payload")
    Remove-TreeSafe $removableTree "removable fixture"
    Assert-True (-not (Test-Path -LiteralPath $removableTree)) `
        "Safe removal left the fixture tree behind."

    $x64Path = Join-Path $resolvedTestRoot "x64.exe"
    Write-PeFixture $x64Path 0x8664
    Assert-X64PeFile $x64Path "x64 fixture"
    Assert-X64PeFile $x64Path "x64 fixture" -ErrorMode Compact
    $script:passed += 2

    $x86Path = Join-Path $resolvedTestRoot "x86.exe"
    Write-PeFixture $x86Path 0x014c
    Assert-ThrowsExactly {
        Assert-X64PeFile $x86Path "x86 fixture"
    } "x86 fixture is not an x64 PE file: $x86Path"
    Assert-ThrowsExactly {
        Assert-X64PeFile $x86Path "x86 fixture" -ErrorMode Compact
    } "x86 fixture is not an x64 PE file: $x86Path"

    $badSignaturePath = Join-Path $resolvedTestRoot "bad-signature.exe"
    Write-PeFixture $badSignaturePath 0x8664 0x12345678
    Assert-ThrowsExactly {
        Assert-X64PeFile $badSignaturePath "signature fixture"
    } "signature fixture has an invalid PE signature: $badSignaturePath"
    Assert-ThrowsExactly {
        Assert-X64PeFile $badSignaturePath "signature fixture" -ErrorMode Compact
    } "signature fixture is not an x64 PE file: $badSignaturePath"

    $truncatedPath = Join-Path $resolvedTestRoot "truncated.exe"
    [System.IO.File]::WriteAllBytes($truncatedPath, [byte[]]@(0x4d, 0x5a))
    Assert-ThrowsExactly {
        Assert-X64PeFile $truncatedPath "truncated fixture"
    } "truncated fixture is not a PE executable: $truncatedPath"

    $missingPePath = Join-Path $resolvedTestRoot "missing.exe"
    Assert-ThrowsExactly {
        Assert-X64PeFile $missingPePath "missing fixture"
    } "missing fixture was not found: $missingPePath"

    $junctionTarget = Join-Path $resolvedTestRoot "junction-target"
    $junctionChild = Join-Path $junctionTarget "child"
    $junctionTree = Join-Path $resolvedTestRoot "junction-tree"
    New-Item -ItemType Directory -Path $junctionChild -ErrorAction Stop | Out-Null
    New-Item -ItemType Directory -Path $junctionTree -ErrorAction Stop | Out-Null
    $junctionPath = Join-Path $junctionTree "linked"
    try {
        New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget `
            -ErrorAction Stop | Out-Null
        Assert-ThrowsExactly {
            Assert-NoReparseTree $junctionTree "junction fixture"
        } "junction fixture contains a reparse point: $junctionPath"
        Assert-ThrowsExactly {
            Assert-NoReparseAncestors (Join-Path $junctionPath "child") `
                "junction ancestor fixture"
        } "junction ancestor fixture or an ancestor is a reparse point: $junctionPath"
    } catch {
        if ($null -eq $junctionPath -or -not (Test-Path -LiteralPath $junctionPath)) {
            Write-Host "SKIP junction fixtures: $($_.Exception.Message)"
        } else {
            throw
        }
    }
} finally {
    if ($junctionPath -and (Test-Path -LiteralPath $junctionPath)) {
        [System.IO.Directory]::Delete($junctionPath)
    }
    if (Test-Path -LiteralPath $resolvedTestRoot) {
        Remove-TreeSafe $resolvedTestRoot "packaging safety fixture root"
    }
}

Write-Host "PASS PackagingSafety ($script:passed assertions)"
