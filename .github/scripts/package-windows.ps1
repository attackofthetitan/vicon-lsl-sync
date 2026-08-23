param(
    [Parameter(Mandatory = $true)]
    [string]$Workspace,
    [Parameter(Mandatory = $true)]
    [string]$ArtifactName
)

$ErrorActionPreference = "Stop"

$bridgeBuild = Join-Path $Workspace "vicon-lsl-bridge\build"
cmake --build $bridgeBuild --config Release --target vicon-lsl-bridge-gui-portable
if ($LASTEXITCODE -ne 0) {
    throw "Portable package target failed with exit code $LASTEXITCODE"
}

$stage = Join-Path $bridgeBuild "vicon-lsl-bridge-gui-portable-stage"
if (-not (Test-Path -LiteralPath $stage -PathType Container)) {
    throw "Portable package stage was not created: $stage"
}
$package = Join-Path $Workspace "package"
if (Test-Path -LiteralPath $package) {
    throw "Windows package destination unexpectedly already exists: $package"
}
New-Item -ItemType Directory -Path $package -ErrorAction Stop | Out-Null
Get-ChildItem -LiteralPath $stage -Force -ErrorAction Stop |
    Copy-Item -Destination $package -Recurse -Force -ErrorAction Stop

$packageScript = Join-Path $Workspace "vicon-lsl-bridge\packaging\windows\package_gui_single_exe.ps1"
$windowsZip = Join-Path $Workspace "$ArtifactName.zip"
& $packageScript -DeployDir $package -UseExistingLicenseBundle -OutputZip $windowsZip

$portableSource = Join-Path $bridgeBuild "vicon-lsl-bridge-gui-portable.exe"
$portableOutput = Join-Path $Workspace "$ArtifactName-gui-portable.exe"
$launcherSource = Join-Path $bridgeBuild "Release\vicon-lsl-bridge-portable-launcher.exe"
if (-not (Test-Path -LiteralPath $portableSource -PathType Leaf)) {
    throw "Portable package target did not create: $portableSource"
}
if (-not (Test-Path -LiteralPath $launcherSource -PathType Leaf)) {
    throw "Portable package target did not create: $launcherSource"
}
Copy-Item -LiteralPath $portableSource -Destination $portableOutput -Force -ErrorAction Stop

$required = @(
    "vicon-lsl-bridge.exe", "vicon-lsl-bridge-gui.exe", "platforms\qwindows.dll",
    "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll",
    "stair_model\stair_model1.obj", "stair_model\stair_model1.mtl",
    "labrecorder\LabRecorder.exe", "labrecorder\LabRecorderCLI.exe",
    "labrecorder\LabRecorder.cfg", "labrecorder\LICENSE",
    "labrecorder\platforms\qwindows.dll",
    "labrecorder\msvcp140.dll", "labrecorder\vcruntime140.dll",
    "labrecorder\vcruntime140_1.dll", "THIRD_PARTY_NOTICES.txt",
    "VICON-DATASTREAM-SDK-LICENSE.txt", "LICENSE-INVENTORY.txt",
    "licenses\Vicon-DataStream-SDK\LICENSE", "licenses\LabRecorder\LICENSE",
    "licenses\liblsl\LICENSE", "licenses\liblsl\pugixml\readme.txt",
    "licenses\liblsl\loguru\LICENSE", "licenses\liblsl\portable-archive\license.txt",
    "licenses\Boost\LICENSE_1_0.txt", ".vicon-lsl-bridge-package-stage",
    ".vicon-lsl-bridge-package-manifest")
foreach ($path in $required) {
    if (-not (Test-Path (Join-Path $package $path))) {
        throw "Package inventory missing package\$path"
    }
}
$packageLsl = Get-ChildItem $package -File -Filter "lsl*.dll"
$recorderLsl = Get-ChildItem (Join-Path $package "labrecorder") -File -Filter "lsl*.dll"
if (-not $packageLsl -or -not $recorderLsl) {
    throw "Package inventory is missing bridge or recorder lsl.dll"
}
$qtLicenseCount = @(
    Get-ChildItem (Join-Path $package "licenses\Qt") -Recurse -File).Count
if ($qtLicenseCount -eq 0) {
    throw "Package inventory is missing Qt LICENSES texts"
}
foreach ($output in @($windowsZip, $portableOutput)) {
    if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
        throw "Windows package output is missing: $output"
    }
}
