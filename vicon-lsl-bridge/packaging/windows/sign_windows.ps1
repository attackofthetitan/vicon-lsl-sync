param(
    [Parameter(Mandatory = $true)]
    [string[]]$Path,

    [string]$TimestampUrl = "https://timestamp.digicert.com",

    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$expectedSubject = [string]$env:WINDOWS_SIGNING_CERTIFICATE_SUBJECT
if ([string]::IsNullOrWhiteSpace($expectedSubject)) {
    throw "Windows signing requires the expected certificate subject configuration."
}
$expectedSubject = $expectedSubject.Trim()
if (-not $VerifyOnly -and ([string]::IsNullOrWhiteSpace($env:WINDOWS_SIGNING_CERTIFICATE_BASE64) -or
    [string]::IsNullOrWhiteSpace($env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD))) {
    throw "Windows signing requires the protected certificate and password secrets."
}

$timestampUri = $null
if (-not [Uri]::TryCreate($TimestampUrl, [UriKind]::Absolute, [ref]$timestampUri) -or
    $timestampUri.Scheme -cne "https" -or [string]::IsNullOrWhiteSpace($timestampUri.Host)) {
    throw "The Authenticode RFC 3161 timestamp URL must be an absolute HTTPS URL."
}

function Assert-NoReparseAncestors {
    param([string]$Path, [string]$Description = "path")

    try {
        $current = [System.IO.Path]::GetFullPath($Path)
    } catch {
        throw "$Description is not a valid filesystem path: $Path"
    }
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Description or an ancestor is a reparse point: $current"
            }
        }
        $parentInfo = [System.IO.Directory]::GetParent($current)
        $parent = if ($parentInfo) { $parentInfo.FullName } else { $null }
        if (-not $parent -or $parent -eq $current) {
            break
        }
        $current = $parent
    }
}

function Assert-CertificateChain {
    param(
        [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [string]$Description,
        [switch]$RequireMicrosoftRoot
    )

    $chain = New-Object System.Security.Cryptography.X509Certificates.X509Chain
    $chain.ChainPolicy.RevocationMode =
        [System.Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
    $chain.ChainPolicy.VerificationFlags =
        [System.Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
    $certificateLifetime = $Certificate.NotAfter - $Certificate.NotBefore
    $chain.ChainPolicy.VerificationTime =
        $Certificate.NotBefore.AddTicks([long]($certificateLifetime.Ticks / 2))
    try {
        if (-not $chain.Build($Certificate)) {
            $statuses = @($chain.ChainStatus | ForEach-Object { $_.Status.ToString() }) -join ", "
            throw "$Description certificate chain is invalid: $statuses"
        }
        if ($RequireMicrosoftRoot) {
            $root = $chain.ChainElements[$chain.ChainElements.Count - 1].Certificate
            if ($root.Subject -notmatch '(?i)Microsoft.*Root|Root.*Microsoft' -or
                $root.Subject -notmatch '(?i)(^|,\s*)O=Microsoft Corporation(,|$)') {
                throw "$Description is not rooted in a Microsoft certificate authority: $($root.Subject)"
            }
        }
    } finally {
        $chain.Dispose()
    }
}

function Find-SignTool {
    $programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    if (-not $programFilesX86) {
        throw "ProgramFiles(x86) is unavailable; cannot locate the trusted Windows SDK."
    }
    $kitsRoot = Join-Path $programFilesX86 "Windows Kits\10\bin"
    Assert-NoReparseAncestors $kitsRoot "Windows SDK directory"
    if (-not (Test-Path -LiteralPath $kitsRoot -PathType Container)) {
        throw "Windows SDK SignTool directory was not found: $kitsRoot"
    }
    $kitsPrefix = ((Resolve-Path -LiteralPath $kitsRoot).Path).TrimEnd('\') + '\'
    $candidates = @(Get-ChildItem -LiteralPath $kitsRoot -Directory -ErrorAction Stop |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "x64\signtool.exe" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($candidates.Count -eq 0) {
        throw "x64 signtool.exe was not found beneath the Windows Kits directory."
    }

    $selected = (Resolve-Path -LiteralPath $candidates[0]).Path
    Assert-NoReparseAncestors $selected "Windows Kits SignTool"
    if (-not $selected.StartsWith($kitsPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing SignTool outside the Windows Kits directory: $selected"
    }
    $toolSignature = Get-AuthenticodeSignature -LiteralPath $selected
    if ($toolSignature.Status -ne "Valid" -or -not $toolSignature.SignerCertificate -or
        $toolSignature.SignerCertificate.Subject -notmatch '(?i)^CN=Microsoft (Windows(?: Software Compatibility Publisher)?|Corporation)(,|$)') {
        throw "Windows Kits SignTool is not Authenticode-valid Microsoft code: $selected"
    }
    Assert-CertificateChain $toolSignature.SignerCertificate "Windows Kits SignTool" -RequireMicrosoftRoot
    return $selected
}

function Remove-TemporaryPfx {
    param([string]$PfxPath)

    if (-not $PfxPath -or -not (Test-Path -LiteralPath $PfxPath)) {
        return
    }
    $item = Get-Item -LiteralPath $PfxPath -Force -ErrorAction Stop
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Temporary Windows signing certificate became a reparse point: $PfxPath"
    }
    Remove-Item -LiteralPath $PfxPath -Force -ErrorAction Stop
    if (Test-Path -LiteralPath $PfxPath) {
        throw "Temporary Windows signing certificate could not be removed."
    }
}

$resolvedPaths = @()
foreach ($item in $Path) {
    $unresolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($item)
    Assert-NoReparseAncestors $unresolved "signing input"
    if (-not (Test-Path -LiteralPath $unresolved -PathType Leaf)) {
        throw "Signing input was not found: $unresolved"
    }
    $resolved = (Resolve-Path -LiteralPath $unresolved).Path
    if ([System.IO.Path]::GetExtension($resolved) -ine ".exe") {
        throw "Only expected Windows executables may be signed by this release script: $resolved"
    }
    $resolvedPaths += $resolved
}

$signTool = Find-SignTool
$pfxPath = $null
$signingThumbprint = $null
$importedThumbprints = New-Object System.Collections.Generic.List[string]
$cleanupErrors = New-Object System.Collections.Generic.List[string]

try {
    if (-not $VerifyOnly) {
        $tempRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
        Assert-NoReparseAncestors $tempRoot "signing temporary directory"
        if (-not (Test-Path -LiteralPath $tempRoot -PathType Container)) {
            throw "Signing temporary directory was not found: $tempRoot"
        }
        $pfxPath = Join-Path $tempRoot ("vicon-lsl-signing-" + [guid]::NewGuid().ToString("N") + ".pfx")
        Assert-NoReparseAncestors $pfxPath "temporary signing certificate"

        $certificateBase64 = [string]$env:WINDOWS_SIGNING_CERTIFICATE_BASE64
        $certificatePassword = [string]$env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD
        $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = $null
        $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD = $null
        try {
            $certificateBytes = [Convert]::FromBase64String($certificateBase64)
        } catch {
            throw "The Windows signing certificate secret is not valid base64."
        } finally {
            $certificateBase64 = $null
        }
        if ($certificateBytes.Length -eq 0) {
            throw "The Windows signing certificate secret is empty."
        }
        $stream = [System.IO.File]::Open(
            $pfxPath,
            [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::Write,
            [System.IO.FileShare]::None)
        try {
            $stream.Write($certificateBytes, 0, $certificateBytes.Length)
            $stream.Flush($true)
        } finally {
            $stream.Dispose()
            [Array]::Clear($certificateBytes, 0, $certificateBytes.Length)
        }

        $preview = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2Collection
        $preview.Import(
            $pfxPath,
            $certificatePassword,
            [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
        $privateCertificates = @($preview | Where-Object { $_.HasPrivateKey })
        if ($privateCertificates.Count -ne 1) {
            throw "The signing PFX must contain exactly one certificate with a private key."
        }
        $previewLeaf = $privateCertificates[0]
        if ($previewLeaf.Subject.Trim() -cne $expectedSubject) {
            throw "The signing PFX subject does not match the configured expected subject."
        }
        $signingThumbprint = $previewLeaf.Thumbprint
        foreach ($certificate in $preview) {
            $storePath = "Cert:\CurrentUser\My\$($certificate.Thumbprint)"
            if (Test-Path -LiteralPath $storePath) {
                throw "Refusing to overwrite a pre-existing CurrentUser certificate: $($certificate.Thumbprint)"
            }
        }

        $securePassword = ConvertTo-SecureString $certificatePassword -AsPlainText -Force
        $imported = @(Import-PfxCertificate -FilePath $pfxPath `
            -CertStoreLocation 'Cert:\CurrentUser\My' -Password $securePassword -Exportable:$false)
        $certificatePassword = $null
        $securePassword = $null
        foreach ($certificate in $imported) {
            if (-not $importedThumbprints.Contains($certificate.Thumbprint)) {
                $importedThumbprints.Add($certificate.Thumbprint)
            }
        }
        $storedLeafPath = "Cert:\CurrentUser\My\$signingThumbprint"
        if (-not (Test-Path -LiteralPath $storedLeafPath)) {
            throw "The signing certificate was not imported into the temporary user store."
        }
        $storedLeaf = Get-Item -LiteralPath $storedLeafPath
        if (-not $storedLeaf.HasPrivateKey -or $storedLeaf.Subject.Trim() -cne $expectedSubject) {
            throw "The imported signing certificate is missing its private key or has the wrong subject."
        }
        $preview.Clear()
        $preview = $null
        Remove-TemporaryPfx $pfxPath
        $pfxPath = $null
    }

    foreach ($file in $resolvedPaths) {
        if (-not $VerifyOnly) {
            & $signTool sign /fd SHA256 /sha1 $signingThumbprint /s My `
                /tr $TimestampUrl /td SHA256 $file *> $null
            if ($LASTEXITCODE -ne 0) {
                throw "Authenticode signing failed for $file."
            }
        }

        # /pa selects standard Authenticode policy, /all checks every
        # signature, and /tw rejects a missing or invalid timestamp.
        & $signTool verify /pa /all /tw $file *> $null
        if ($LASTEXITCODE -ne 0) {
            throw "Authenticode verification failed for $file."
        }

        $signature = Get-AuthenticodeSignature -LiteralPath $file
        if ($signature.Status -ne "Valid" -or -not $signature.SignerCertificate) {
            throw "Authenticode signature status was not Valid for $file ($($signature.Status))."
        }
        if ($signature.SignerCertificate.Subject.Trim() -cne $expectedSubject) {
            throw "Signer subject did not match the configured expected subject for $file."
        }
        if (-not $signature.TimeStamperCertificate) {
            throw "Authenticode timestamp verification did not return a timestamp certificate for $file."
        }
    }
} finally {
    $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = $null
    $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD = $null
    try {
        Remove-TemporaryPfx $pfxPath
    } catch {
        $cleanupErrors.Add($_.Exception.Message)
    }
    foreach ($thumbprint in $importedThumbprints) {
        $storePath = "Cert:\CurrentUser\My\$thumbprint"
        try {
            if (Test-Path -LiteralPath $storePath) {
                Remove-Item -LiteralPath $storePath -Force -ErrorAction Stop
            }
            if (Test-Path -LiteralPath $storePath) {
                throw "Imported signing certificate could not be removed: $thumbprint"
            }
        } catch {
            $cleanupErrors.Add($_.Exception.Message)
        }
    }
    if ($cleanupErrors.Count -gt 0) {
        throw "Windows signing key cleanup failed: $($cleanupErrors -join '; ')"
    }
}
