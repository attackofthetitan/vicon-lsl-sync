param(
    [Parameter(Mandatory = $true)]
    [string]$Workspace,
    [Parameter(Mandatory = $true)]
    [string]$RunnerTemp,
    [Parameter(Mandatory = $true)]
    [string]$VcpkgInstallationRoot,
    [Parameter(Mandatory = $true)]
    [string]$Triplet
)

$ErrorActionPreference = "Stop"

$qtCommand = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (-not $qtCommand) {
    $qtCommand = Get-Command windeployqt -ErrorAction SilentlyContinue
}
if (-not $qtCommand) {
    throw "windeployqt is unavailable for the license collector test"
}
$qtCommandPath = if ($qtCommand.Source) { $qtCommand.Source } else { $qtCommand.Path }
$qtRoot = Split-Path -Parent (Split-Path -Parent $qtCommandPath)
$boostRoot = if ($VcpkgInstallationRoot -and (Test-Path -LiteralPath (Join-Path $VcpkgInstallationRoot "installed\$Triplet"))) {
    Join-Path $VcpkgInstallationRoot "installed\$Triplet"
} elseif ($env:BOOST_ROOT -and (Test-Path -LiteralPath $env:BOOST_ROOT)) {
    $env:BOOST_ROOT
} elseif ($VcpkgInstallationRoot) {
    Join-Path $VcpkgInstallationRoot "installed\$Triplet"
} else {
    $null
}
$liblslSource = (Resolve-Path (Join-Path $Workspace "vicon-lsl-bridge\build\_deps\liblsl-src")).Path
$probe = Join-Path $RunnerTemp (
    "missing-license-bundle-" + [guid]::NewGuid().ToString("N"))
$failed = $false
try {
    & (Join-Path $Workspace "vicon-lsl-bridge\packaging\windows\collect_license_bundle.ps1") `
        -OutputDirectory $probe `
        -ViconSdkDirectory (Join-Path $RunnerTemp "missing-vicon-sdk") `
        -LabRecorderSourceDirectory (Join-Path $Workspace "labrecorder") `
        -LiblslSourceDirectory $liblslSource `
        -QtRootDirectory $qtRoot `
        -BoostRootDirectory $boostRoot
} catch {
    $failed = $true
} finally {
    if (Test-Path -LiteralPath $probe) {
        Remove-Item -LiteralPath $probe -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if (-not $failed) {
    throw "License collector unexpectedly succeeded with a missing Vicon license source"
}
