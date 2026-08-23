param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactName,
    [Parameter(Mandatory = $true)]
    [string]$RunnerTemp
)

$ErrorActionPreference = "Stop"

$portable = "$ArtifactName-gui-portable.exe"
$tampered = Join-Path $RunnerTemp "tampered-portable.exe"
Copy-Item $portable $tampered -Force
$bytes = [System.IO.File]::ReadAllBytes($tampered)
$peOffset = [BitConverter]::ToUInt32($bytes, 0x3c)
$optionalMagic = [BitConverter]::ToUInt16($bytes, $peOffset + 24)
$directoryOffset = if ($optionalMagic -eq 0x20b) {
    $peOffset + 24 + 112 + (4 * 8)
} else {
    $peOffset + 24 + 96 + (4 * 8)
}
$certificateOffset = [BitConverter]::ToUInt32($bytes, $directoryOffset)
$logicalEnd = if ($certificateOffset -gt 0 -and $certificateOffset -lt $bytes.Length) {
    $certificateOffset
} else {
    $bytes.Length
}
$footerOffset = -1
$payloadSize = 0
$firstFooter = [Math]::Max(0, $logicalEnd - 65536)
for ($offset = $logicalEnd - 24; $offset -ge $firstFooter; $offset--) {
    if ([Text.Encoding]::ASCII.GetString($bytes, $offset, 16) -eq "VICONLSL_PAYLOAD") {
        $candidateSize = [BitConverter]::ToUInt64($bytes, $offset + 16)
        if ($candidateSize -gt 0 -and $candidateSize -le $offset) {
            $footerOffset = $offset
            $payloadSize = $candidateSize
            break
        }
    }
}
$payloadStart = $footerOffset - [int64]$payloadSize
if ($footerOffset -lt 0 -or $payloadStart -lt 1 -or $payloadStart -ge $logicalEnd) {
    throw "Portable payload footer could not be located."
}
$bytes[$payloadStart] = $bytes[$payloadStart] -bxor 1
[System.IO.File]::WriteAllBytes($tampered, $bytes)
$process = Start-Process -FilePath $tampered -ArgumentList "--test" `
    -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -eq 0) {
    throw "Tampered portable payload unexpectedly launched."
}
