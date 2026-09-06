param(
    [Parameter(Mandatory = $true)]
    [string]$Workspace,
    [Parameter(Mandatory = $true)]
    [string]$TempDirectory
)

$ErrorActionPreference = "Stop"

$licenseRoot = Join-Path $Workspace "qt-license-source\LICENSES"
if (Test-Path -LiteralPath (Split-Path -Parent $licenseRoot)) {
    if (@(Get-ChildItem -LiteralPath $licenseRoot -Recurse -File -ErrorAction SilentlyContinue).Count -gt 0) {
        Write-Host "Qt license source already prepared at $licenseRoot."
        return
    }
}
New-Item -ItemType Directory -Path $licenseRoot -Force -ErrorAction Stop | Out-Null

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

Add-Type -AssemblyName System.IO.Compression.FileSystem

foreach ($archive in $archives) {
    $zip = Join-Path $TempDirectory "$($archive.Name)-6.8.3.zip"
    $destination = Join-Path $licenseRoot $archive.Name

    if (-not (Test-Path -LiteralPath $zip)) {
        Invoke-WebRequest -Uri $archive.Url -OutFile $zip
    }
    $actualHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $archive.Sha256) {
        throw "Qt $($archive.Name) source hash mismatch: expected $($archive.Sha256), got $actualHash"
    }

    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null

    $zipArchive = [System.IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $extractedCount = 0
        foreach ($entry in $zipArchive.Entries) {
            if ($entry.FullName -match '^[^\/]+\/LICENSES\/(?<rel>.+)$') {
                $rel = $Matches['rel']
                if (-not $rel.EndsWith('/')) {
                    $targetFile = Join-Path $destination ($rel.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
                    $targetDir = Split-Path -Parent $targetFile
                    if (-not (Test-Path -LiteralPath $targetDir)) {
                        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
                    }
                    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $targetFile, $true)
                    $extractedCount++
                }
            }
        }
        if ($extractedCount -eq 0) {
            throw "Qt $($archive.Name) archive is missing its LICENSES directory"
        }
    } finally {
        $zipArchive.Dispose()
    }
}

if (@(Get-ChildItem -LiteralPath $licenseRoot -Recurse -File).Count -eq 0) {
    throw "Pinned Qt source license bundle is empty"
}
