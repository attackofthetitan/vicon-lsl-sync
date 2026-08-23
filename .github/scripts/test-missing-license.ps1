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
$boostRoot = Join-Path $VcpkgInstallationRoot "installed\$Triplet"
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
}
if (-not $failed) {
    throw "License collector unexpectedly succeeded with a missing Vicon license source"
}
