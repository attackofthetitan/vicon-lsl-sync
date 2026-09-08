param(
    [Parameter(Mandatory = $true)]
    [string]$TempDirectory,
    [Parameter(Mandatory = $true)]
    [string]$EnvironmentFile
)

$ErrorActionPreference = "Stop"

$revision = "d87340acc46bdeda386037b38aca30136e667e47"
$vcpkgRoot = Join-Path $TempDirectory "vcpkg-$revision"
$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"

if (Test-Path -LiteralPath $vcpkgExe -PathType Leaf) {
    Write-Host "Pinned vcpkg already bootstrapped at: $vcpkgRoot"
} else {
    if (-not (Test-Path -LiteralPath $vcpkgRoot)) {
        New-Item -ItemType Directory -Path $vcpkgRoot -Force | Out-Null
    }
    Push-Location $vcpkgRoot
    try {
        if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot ".git"))) {
            git init
            git remote add origin https://github.com/microsoft/vcpkg.git
            git fetch --depth 1 origin $revision
            if ($LASTEXITCODE -ne 0) {
                git fetch --depth 50 origin
            }
            git checkout --detach $revision
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to check out the pinned vcpkg commit"
            }
        } else {
            git checkout --detach $revision
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to check out the pinned vcpkg commit"
            }
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
}
"VCPKG_ROOT=$vcpkgRoot" |
    Out-File -FilePath $EnvironmentFile -Encoding utf8 -Append
"VCPKG_INSTALLATION_ROOT=$vcpkgRoot" |
    Out-File -FilePath $EnvironmentFile -Encoding utf8 -Append
