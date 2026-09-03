<#
.SYNOPSIS
    Rejects files that should never enter the repository.

.DESCRIPTION
    The repository accumulated roughly half a gigabyte of debug screenshots,
    probe directories and duplicated reference checkouts before anything
    objected. .gitignore alone does not prevent that: it is advisory, `git add
    -f` walks straight past it, and it says nothing about a file that is in an
    allowed directory but has no business being committed.

    This runs in two places, deliberately. The pre-commit hook gives fast local
    feedback; continuous integration is the enforcement, because hooks are not
    shared by git and can be skipped with --no-verify.

    Every rule below is checked against the whole repository in CI, so a rule
    that would fail on existing content fails the build immediately rather than
    lying in wait.

.PARAMETER Staged
    Check only the files staged for commit. This is what the hook uses.
#>
[CmdletBinding()]
param(
    [switch]$Staged
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

# Nothing legitimately tracked is anywhere near this. The largest art file in
# the repository is about 2.4 MB, so 10 MB catches an accident without
# second-guessing the art pipeline.
$maximumFileBytes = 10MB

# A source or configuration file this large is a mistake, whatever it is.
$maximumNonAssetBytes = 1MB

# Directories that may hold binary assets. Everything else may not.
$assetDirectories = @("art/", "game/assets/", "tests/fixtures/")

$imageExtensions = @(".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tga", ".psd",
                     ".aseprite", ".ase", ".webp", ".tiff")

# Build output, editor leftovers and crash/scratch artefacts.
$forbiddenExtensions = @(".log", ".tmp", ".temp", ".bak", ".orig", ".rej",
                         ".obj", ".exe", ".dll", ".pdb", ".ilk", ".exp",
                         ".lib", ".zip", ".7z", ".rar", ".suo", ".user")

$forbiddenNames = @("thumbs.db", ".ds_store", "desktop.ini")

# Path fragments that name scratch work rather than project content.
$forbiddenPathPatterns = @("-probe/", "/probe/", "bug-repros/", "/scratch/",
                           "/tmp/", "reference-hazel/", "zip-smoke")

function Test-UnderAssetDirectory {
    param([string]$Path)

    foreach ($directory in $assetDirectories) {
        if ($Path.StartsWith($directory, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

Push-Location $repoRoot
try {
    if ($Staged) {
        # Added, copied or modified; renames report their destination.
        $paths = & git diff --cached --name-only --diff-filter=ACMR
    }
    else {
        $paths = & git ls-files
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Listing repository files failed."
    }

    $problems = @()
    foreach ($path in $paths) {
        if ([string]::IsNullOrWhiteSpace($path)) { continue }

        $full = Join-Path $repoRoot $path
        if (-not (Test-Path -LiteralPath $full)) { continue }

        $normalized = $path.Replace("\", "/")
        $name = [System.IO.Path]::GetFileName($normalized).ToLowerInvariant()
        $extension = [System.IO.Path]::GetExtension($normalized).ToLowerInvariant()
        $size = (Get-Item -LiteralPath $full).Length
        $isAsset = Test-UnderAssetDirectory -Path $normalized

        if ($forbiddenExtensions -contains $extension) {
            $problems += "$normalized : '$extension' files are build output or scratch and are never committed."
        }
        elseif ($forbiddenNames -contains $name) {
            $problems += "$normalized : '$name' is an operating-system or editor artefact."
        }

        foreach ($pattern in $forbiddenPathPatterns) {
            if ($normalized.ToLowerInvariant().Contains($pattern)) {
                $problems += "$normalized : '$pattern' names scratch work rather than project content."
                break
            }
        }

        if (($imageExtensions -contains $extension) -and (-not $isAsset)) {
            $problems +=
                "$normalized : images belong under $($assetDirectories -join ', '), not here."
        }

        if ($size -gt $maximumFileBytes) {
            $megabytes = [math]::Round($size / 1MB, 1)
            $problems += "$normalized : $megabytes MB exceeds the $($maximumFileBytes / 1MB) MB limit."
        }
        elseif ((-not $isAsset) -and ($size -gt $maximumNonAssetBytes)) {
            $megabytes = [math]::Round($size / 1MB, 1)
            $problems +=
                "$normalized : $megabytes MB is too large for a non-asset file (limit $($maximumNonAssetBytes / 1MB) MB)."
        }
    }

    if ($problems.Count -gt 0) {
        Write-Host "Repository hygiene check failed:"
        Write-Host ""
        foreach ($problem in ($problems | Sort-Object -Unique)) {
            Write-Host "  $problem"
        }
        Write-Host ""
        Write-Host "If one of these is genuinely wanted, add it to the rules in"
        Write-Host "tools/check-repository-hygiene.ps1 rather than bypassing the check,"
        Write-Host "so the next person meets the same decision you just made."
        exit 1
    }

    $count = @($paths).Count
    if ($Staged) {
        Write-Host "Repository hygiene: $count staged file(s) are fine."
    }
    else {
        Write-Host "Repository hygiene: all $count tracked file(s) are fine."
    }
}
finally {
    Pop-Location
}
