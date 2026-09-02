[CmdletBinding()]
param(
    [switch]$RunSmoke
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$distRoot = Join-Path $repoRoot "dist"
$packageRoot = Join-Path $distRoot "windows-x64"
$archivePath = Join-Path $distRoot "IC_2DE-Windows-x64.zip"
$resolvedDistRoot = [System.IO.Path]::GetFullPath($distRoot)
$resolvedPackageRoot = [System.IO.Path]::GetFullPath($packageRoot)

if (-not $resolvedPackageRoot.StartsWith($resolvedDistRoot + [System.IO.Path]::DirectorySeparatorChar,
                                         [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package output escaped the repository dist directory."
}

& (Join-Path $PSScriptRoot "validate-sprite-atlases.ps1")

& (Join-Path $PSScriptRoot "build.ps1") -Configuration Debug -RunTests
if ($LASTEXITCODE -ne 0) { throw "Debug verification failed." }

& (Join-Path $PSScriptRoot "build.ps1") -Configuration Release -RunTests
if ($LASTEXITCODE -ne 0) { throw "Release verification failed." }

if (Test-Path -LiteralPath $resolvedPackageRoot) {
    Remove-Item -LiteralPath $resolvedPackageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedPackageRoot -Force | Out-Null

& (Join-Path $PSScriptRoot "build.ps1") -Configuration Release -Shipping -Install `
    -InstallPrefix $resolvedPackageRoot
if ($LASTEXITCODE -ne 0) { throw "Shipping install failed." }

$debugSource = Join-Path $repoRoot "build\windows-debug\ic2de_testbed.exe"
$debugDestination = Join-Path $resolvedPackageRoot "IC_2DE-Debug.exe"
if (-not (Test-Path -LiteralPath $debugSource)) {
    throw "Debug executable was not produced: $debugSource"
}
Copy-Item -LiteralPath $debugSource -Destination $debugDestination -Force

# The editor ships from the Release preset: development tools stay compiled in,
# but the tool itself stays responsive to use.
$editorSource = Join-Path $repoRoot "build\windows-release\IC_2DE-Editor.exe"
$editorDestination = Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe"
if (-not (Test-Path -LiteralPath $editorSource)) {
    throw "Editor executable was not produced: $editorSource"
}
Copy-Item -LiteralPath $editorSource -Destination $editorDestination -Force

Push-Location $resolvedPackageRoot
try {
    & $debugDestination --validate-content
    if ($LASTEXITCODE -ne 0) {
        throw "Combined-folder Debug content validation failed with code $LASTEXITCODE."
    }

    & $editorDestination --validate-content
    if ($LASTEXITCODE -ne 0) {
        throw "Combined-folder editor content validation failed with code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}
Compress-Archive -Path (Join-Path $resolvedPackageRoot "*") -DestinationPath $archivePath `
    -CompressionLevel Optimal

if ($RunSmoke) {
    Push-Location $resolvedPackageRoot
    try {
        & (Join-Path $resolvedPackageRoot "IC_2DE.exe") --shipping-smoke --uncapped
        if ($LASTEXITCODE -ne 0) { throw "Packaged runtime smoke failed with code $LASTEXITCODE." }
        if (-not (Test-Path -LiteralPath (Join-Path $resolvedPackageRoot "shipping-smoke.png"))) {
            throw "Packaged runtime did not produce shipping-smoke.png."
        }
    }
    finally {
        Pop-Location
    }
}

$archive = Get-Item -LiteralPath $archivePath
$hash = Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
Write-Output "Package: $resolvedPackageRoot"
Write-Output "Archive: $($archive.FullName)"
Write-Output "Bytes: $($archive.Length)"
Write-Output "SHA256: $($hash.Hash)"
