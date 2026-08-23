param(
    [Parameter(Mandatory = $true)]
    [string]$TempDirectory,
    [Parameter(Mandatory = $true)]
    [string]$EnvironmentFile
)

$ErrorActionPreference = "Stop"

$revision = "d87340acc46bdeda386037b38aca30136e667e47"
$vcpkgRoot = Join-Path $TempDirectory "vcpkg-$revision"
if (Test-Path -LiteralPath $vcpkgRoot) {
    throw "Pinned vcpkg destination unexpectedly already exists: $vcpkgRoot"
}
git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
if ($LASTEXITCODE -ne 0) {
    throw "Unable to clone the pinned vcpkg source"
}
Push-Location $vcpkgRoot
try {
    git checkout --detach $revision
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to check out the pinned vcpkg commit"
    }
    $head = (git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $head -ne $revision) {
        throw "Pinned vcpkg revision mismatch: $head"
    }
    $remote = (git remote get-url origin).TrimEnd("/")
    if ($LASTEXITCODE -ne 0 -or $remote -notin @(
            "https://github.com/microsoft/vcpkg",
            "https://github.com/microsoft/vcpkg.git")) {
        throw "Unexpected vcpkg origin: $remote"
    }
    & .\bootstrap-vcpkg.bat -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        throw "Pinned vcpkg bootstrap failed"
    }
} finally {
    Pop-Location
}
"VCPKG_ROOT=$vcpkgRoot" |
    Out-File -FilePath $EnvironmentFile -Encoding utf8 -Append
"VCPKG_INSTALLATION_ROOT=$vcpkgRoot" |
    Out-File -FilePath $EnvironmentFile -Encoding utf8 -Append
