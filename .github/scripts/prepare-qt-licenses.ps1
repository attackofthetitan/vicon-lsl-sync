param(
    [Parameter(Mandatory = $true)]
    [string]$Workspace,
    [Parameter(Mandatory = $true)]
    [string]$TempDirectory
)

$ErrorActionPreference = "Stop"

$licenseRoot = Join-Path $Workspace "qt-license-source\LICENSES"
if (Test-Path -LiteralPath (Split-Path -Parent $licenseRoot)) {
    throw "Qt license source destination unexpectedly already exists"
}
New-Item -ItemType Directory -Path $licenseRoot -ErrorAction Stop | Out-Null

$archives = @(
    @{
        Name = "qtbase"
        Url = "https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.zip"
        Sha256 = "992bf7766e214a341ef793eb3665fb784787d2fd666955f5f507f4c6f1f770dd"
    },
    @{
        Name = "qtsvg"
        Url = "https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/qtsvg-everywhere-src-6.8.3.zip"
        Sha256 = "90643c1ba05245abedc2bcd9d4f745568dc27a1b37dd3ba861fd19d8ac6a7c46"
    })

foreach ($archive in $archives) {
    $zip = Join-Path $TempDirectory "$($archive.Name)-6.8.3.zip"
    $extract = Join-Path $TempDirectory "$($archive.Name)-6.8.3-source"
    if ((Test-Path -LiteralPath $zip) -or (Test-Path -LiteralPath $extract)) {
        throw "Qt license temporary path unexpectedly already exists for $($archive.Name)"
    }
    Invoke-WebRequest -Uri $archive.Url -OutFile $zip
    $actualHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $archive.Sha256) {
        throw "Qt $($archive.Name) source hash mismatch: expected $($archive.Sha256), got $actualHash"
    }
    Expand-Archive -LiteralPath $zip -DestinationPath $extract
    $moduleRoot = @(Get-ChildItem -LiteralPath $extract -Directory -ErrorAction Stop)
    if ($moduleRoot.Count -ne 1) {
        throw "Qt $($archive.Name) archive did not contain exactly one source root"
    }
    $sourceLicenses = Join-Path $moduleRoot[0].FullName "LICENSES"
    if (-not (Test-Path -LiteralPath $sourceLicenses -PathType Container)) {
        throw "Qt $($archive.Name) archive is missing its LICENSES directory"
    }
    $destination = Join-Path $licenseRoot $archive.Name
    Copy-Item -LiteralPath $sourceLicenses -Destination $destination -Recurse -ErrorAction Stop
}

if (@(Get-ChildItem -LiteralPath $licenseRoot -Recurse -File).Count -eq 0) {
    throw "Pinned Qt source license bundle is empty"
}
