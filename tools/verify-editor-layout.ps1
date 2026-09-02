param(
    [string]$BuildDirectory = "build/windows-debug"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$resolvedBuildDirectory = Join-Path $projectRoot $BuildDirectory
$probeDirectory = Join-Path $projectRoot "build/editor-layout-probe"
$editorExecutable = Join-Path $resolvedBuildDirectory "IC_2DE-Editor.exe"
$layoutFile = Join-Path $probeDirectory "layout-probe-v1.ini"

if (-not (Test-Path -LiteralPath $editorExecutable -PathType Leaf)) {
    throw "Required editor layout probe file is missing: $editorExecutable"
}

# The probe never touches the personal %LOCALAPPDATA% workspace, so running it
# cannot disturb the arrangement the owner is actually working in.
if (Test-Path -LiteralPath $probeDirectory) {
    Remove-Item -LiteralPath $probeDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $probeDirectory "build") | Out-Null

function Invoke-EditorLaunch {
    param(
        [Parameter(Mandatory = $true)][int]$Launch
    )

    $stdoutLog = Join-Path $probeDirectory "editor-layout-launch$Launch-stdout.log"
    $stderrLog = Join-Path $probeDirectory "editor-layout-launch$Launch-stderr.log"

    # The layout path contains spaces in this repository, so the argument is
    # quoted explicitly. An unquoted value is split at the space and the ini is
    # written to a truncated path outside the probe directory.
    $editorProcess = Start-Process `
        -FilePath $editorExecutable `
        -ArgumentList "--smoke-nav-path", "--fps=60", "`"--editor-layout=$layoutFile`"" `
        -WorkingDirectory $probeDirectory `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -WindowStyle Hidden `
        -PassThru

    try {
        if (-not $editorProcess.WaitForExit(60000)) {
            throw "Editor layout probe launch $Launch did not exit within 60 seconds."
        }
        # The timed overload returns before the exit code is published, so the
        # parameterless wait is what makes ExitCode readable here.
        $editorProcess.WaitForExit()
    }
    finally {
        if (-not $editorProcess.HasExited) {
            Stop-Process -Id $editorProcess.Id -Force
            $editorProcess.WaitForExit()
        }
    }

    if ($null -ne $editorProcess.ExitCode -and $editorProcess.ExitCode -ne 0) {
        throw "Editor layout probe launch $Launch failed with exit code $($editorProcess.ExitCode)."
    }

    $stdout = if (Test-Path -LiteralPath $stdoutLog) { Get-Content -LiteralPath $stdoutLog -Raw } else { "" }
    $stderr = if (Test-Path -LiteralPath $stderrLog) { Get-Content -LiteralPath $stderrLog -Raw } else { "" }

    # Wrapped console lines would otherwise hide a match that spans a newline.
    return (($stdout + $stderr) -replace "\r?\n", " ")
}

function Assert-UsableLayout {
    param(
        [Parameter(Mandatory = $true)][int]$Launch
    )

    if (-not (Test-Path -LiteralPath $layoutFile -PathType Leaf)) {
        throw "Editor layout probe launch $Launch did not save its layout."
    }
    $layoutText = Get-Content -LiteralPath $layoutFile -Raw
    if (($layoutText -notmatch '\[Docking\]\[Data\]') -or ($layoutText -notmatch 'DockSpace')) {
        throw "Editor layout probe launch $Launch saved no usable dock tree."
    }
    return $layoutText
}

# Launch 1: no file exists, so the shell must build the cleaned default
# workspace and save it.
$firstLog = Invoke-EditorLaunch -Launch 1
if ($firstLog -notmatch 'Built default editor layout') {
    throw "The first launch did not build the default editor workspace."
}
if ($firstLog -match 'Restored editor layout') {
    throw "The first launch restored a layout that should not have existed."
}
$firstLayout = Assert-UsableLayout -Launch 1

# A rebuild would recompute every split from the hard-coded default ratios, so
# hand-widening one dock node is what separates real restoration from a
# convincing-looking rebuild of the same arrangement.
$dockNodePattern = '(?m)^(?<prefix>\s*DockNode\s+ID=0x[0-9A-Fa-f]+\s+Parent=0x[0-9A-Fa-f]+\s+SizeRef=)(?<width>\d+)(?<suffix>,\d+)'
$dockNodeMatch = [regex]::Match($firstLayout, $dockNodePattern)
if (-not $dockNodeMatch.Success) {
    throw "The saved dock tree contained no sizeable dock node to arrange."
}
$originalWidth = [int]$dockNodeMatch.Groups['width'].Value
$arrangedWidth = $originalWidth + 190
$arrangedLayout = [regex]::Replace(
    $firstLayout,
    $dockNodePattern,
    { param($match) $match.Groups['prefix'].Value + $arrangedWidth + $match.Groups['suffix'].Value },
    1)
Set-Content -LiteralPath $layoutFile -Value $arrangedLayout -Encoding utf8 -NoNewline

# Launch 2: the arranged file exists, so the shell must restore it untouched.
$secondLog = Invoke-EditorLaunch -Launch 2
if ($secondLog -notmatch 'Restored editor layout') {
    throw "The second launch did not restore the saved editor workspace."
}
if ($secondLog -match 'Built default editor layout') {
    throw "The second launch rebuilt the default workspace instead of restoring it."
}
$secondLayout = Assert-UsableLayout -Launch 2

$restoredMatch = [regex]::Match($secondLayout, $dockNodePattern)
if (-not $restoredMatch.Success) {
    throw "The restored dock tree lost its sizeable dock node."
}
$restoredWidth = [int]$restoredMatch.Groups['width'].Value
if ($restoredWidth -ne $arrangedWidth) {
    throw ("The arranged dock width was discarded. Expected $arrangedWidth, found $restoredWidth. " +
        "The second launch rebuilt the default workspace.")
}

$capture = Join-Path $probeDirectory "build/runtime-nav-path-smoke.png"
if (-not (Test-Path -LiteralPath $capture -PathType Leaf)) {
    throw "The editor layout probe did not produce its second-launch capture."
}

# Launch 3 keeps the restore assertion honest. ImGui will happily reload window
# positions with no dock tree, which looks like a layout and is not, so a
# window-only ini must fall back to the complete default workspace.
Set-Content -LiteralPath $layoutFile -Encoding utf8 `
    -Value "[Window][Viewport]`nPos=0,0`nSize=640,360`n"
$thirdLog = Invoke-EditorLaunch -Launch 3
if ($thirdLog -notmatch 'Built default editor layout') {
    throw "A window-only layout file did not fall back to the default workspace."
}
if ($thirdLog -match 'Restored editor layout') {
    throw "A window-only layout file was wrongly accepted as a saved workspace."
}
$null = Assert-UsableLayout -Launch 3

Write-Output "Launch 1 built the default workspace: $layoutFile"
Write-Output "Arranged dock width: $originalWidth -> $arrangedWidth"
Write-Output "Launch 2 restored the arranged workspace and kept width $restoredWidth."
Write-Output "Launch 3 rejected a window-only ini and rebuilt the default workspace."
Write-Output "Second-launch capture: $capture"
