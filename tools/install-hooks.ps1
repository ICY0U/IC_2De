<#
.SYNOPSIS
    Installs this repository's git hooks.

.DESCRIPTION
    Git does not share hooks: .git/hooks is local to a clone and is never
    committed or fetched. So the hook lives here as a file that can be reviewed
    in the repository, and this copies it into place.

    The hook is fast local feedback, not enforcement. It can be skipped with
    `git commit --no-verify`, and someone who has never run this script has no
    hook at all. The same check therefore runs in continuous integration, which
    is where it actually holds.

.PARAMETER Uninstall
    Removes the hooks this script installed.
#>
[CmdletBinding()]
param(
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$hooksDirectory = Join-Path $repoRoot ".git\hooks"
if (-not (Test-Path -LiteralPath $hooksDirectory)) {
    throw "No .git/hooks directory found. Is this a git clone?"
}

$preCommit = Join-Path $hooksDirectory "pre-commit"

if ($Uninstall) {
    if (Test-Path -LiteralPath $preCommit) {
        Remove-Item -LiteralPath $preCommit -Force
        Write-Host "Removed the pre-commit hook."
    }
    else {
        Write-Host "No pre-commit hook was installed."
    }
    return
}

if ((Test-Path -LiteralPath $preCommit) -and
    (-not (Select-String -Path $preCommit -Pattern "check-repository-hygiene" -Quiet))) {
    throw "A pre-commit hook already exists and is not this one. Merge them by hand rather than losing it: $preCommit"
}

# Git runs hooks through its bundled shell, so this is sh rather than
# PowerShell, and it hands the work straight back to the shared check.
$hook = @'
#!/bin/sh
# Installed by tools/install-hooks.ps1. Edit that, not this copy.
#
# Rejects files that should never enter the repository. The same check runs in
# continuous integration, which is what enforces it: this hook is only here to
# tell you before the commit rather than after the push.
exec powershell -NoProfile -ExecutionPolicy Bypass \
    -File "$(git rev-parse --show-toplevel)/tools/check-repository-hygiene.ps1" -Staged
'@

# LF endings and no BOM: git's shell will not run the hook otherwise.
$bytes = [System.Text.Encoding]::UTF8.GetBytes(($hook -replace "`r`n", "`n"))
[System.IO.File]::WriteAllBytes($preCommit, $bytes)

Write-Host "Installed the pre-commit hook."
Write-Host "It rejects oversized files, images outside the asset directories,"
Write-Host "build output and scratch paths. Run with -Uninstall to remove it."
