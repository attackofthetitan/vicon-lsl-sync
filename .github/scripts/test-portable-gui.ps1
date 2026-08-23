param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactName
)

$ErrorActionPreference = "Stop"

$portable = "$ArtifactName-gui-portable.exe"
if (-not (Test-Path $portable)) {
    throw "Portable GUI artifact was not created: $portable"
}
$process = Start-Process `
    -FilePath ".\$portable" `
    -ArgumentList "--test" `
    -WindowStyle Hidden `
    -Wait `
    -PassThru
if ($process.ExitCode -ne 0) {
    throw "Portable GUI test failed with exit code $($process.ExitCode)."
}
