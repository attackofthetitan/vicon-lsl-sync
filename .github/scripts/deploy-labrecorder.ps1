param(
    [Parameter(Mandatory = $true)]
    [string]$Workspace
)

$ErrorActionPreference = "Stop"

$buildDirectory = Join-Path $Workspace "build-labrecorder"
$installDirectory = Join-Path $Workspace "recorder-install"
$deploy = Join-Path $Workspace "recorder-deploy"
cmake --install $buildDirectory --config Release --prefix $installDirectory
if (Test-Path -LiteralPath $deploy) {
    throw "LabRecorder deployment unexpectedly already exists: $deploy"
}
New-Item -ItemType Directory -Path $deploy -ErrorAction Stop | Out-Null
foreach ($name in @("LabRecorder.exe", "LabRecorderCLI.exe", "LabRecorder.cfg", "LICENSE")) {
    $source = Get-ChildItem $installDirectory -Recurse -File -Filter $name |
        Select-Object -First 1
    if (-not $source) {
        throw "LabRecorder install missing $name"
    }
    Copy-Item $source.FullName $deploy -Force
}
$lsl = Get-ChildItem -Path $buildDirectory -Recurse -File -Filter "lsl*.dll" |
    Select-Object -First 1
if (-not $lsl) {
    throw "LabRecorder liblsl runtime was not built"
}
Copy-Item $lsl.FullName (Join-Path $deploy "lsl.dll") -Force
& windeployqt --no-translations --compiler-runtime (Join-Path $deploy "LabRecorder.exe")
if ($LASTEXITCODE -ne 0) {
    throw "LabRecorder windeployqt failed with exit code $LASTEXITCODE"
}
& (Join-Path $Workspace "vicon-lsl-bridge\packaging\windows\ensure_msvc_runtime.ps1") `
    -DeployDirectory $deploy
if (-not (Test-Path (Join-Path $deploy "platforms\qwindows.dll"))) {
    throw "LabRecorder Qt deployment is missing platforms/qwindows.dll"
}
