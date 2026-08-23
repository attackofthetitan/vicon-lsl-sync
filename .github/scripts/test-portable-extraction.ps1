param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactName,
    [Parameter(Mandatory = $true)]
    [string]$RunnerTemp
)

$ErrorActionPreference = "Stop"

$portable = "$ArtifactName-gui-portable.exe"
$extractDir = Join-Path $RunnerTemp (
    "vicon-lsl-portable-extracted-" + [guid]::NewGuid().ToString("N"))
$process = Start-Process -FilePath ".\$portable" `
    -ArgumentList @("--extract", $extractDir) -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "Portable extraction failed with exit code $($process.ExitCode)."
}
$required = @(
    "vicon-lsl-bridge-gui.exe", "vicon-lsl-bridge.exe",
    "THIRD_PARTY_NOTICES.txt", "VICON-DATASTREAM-SDK-LICENSE.txt",
    "LICENSE-INVENTORY.txt", "licenses\Qt",
    "licenses\liblsl\pugixml\readme.txt")
foreach ($path in $required) {
    if (-not (Test-Path (Join-Path $extractDir $path))) {
        throw "Portable extraction inventory missing $path"
    }
}
