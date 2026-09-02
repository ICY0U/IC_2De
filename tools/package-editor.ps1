[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [switch]$RunHotSwapProbe,

    [switch]$RunDodgeProbe,

    [switch]$RunGameplayReplayProbe,

    [switch]$RunMovingAttackerProbe,

    [switch]$RunNavigationGridProbe,

    [switch]$RunNavigationPathProbe,

    [switch]$RunRunnerPathProbe,

    [switch]$RunEnemyStressProbe,

    [switch]$RunEditorLayoutProbe
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$distRoot = Join-Path $repoRoot "dist"
$packageRoot = Join-Path $distRoot "editor-windows-x64"
$archivePath = Join-Path $distRoot "IC_2DE-Editor-Windows-x64.zip"
$resolvedDistRoot = [System.IO.Path]::GetFullPath($distRoot)
$resolvedPackageRoot = [System.IO.Path]::GetFullPath($packageRoot)

if (-not $resolvedPackageRoot.StartsWith($resolvedDistRoot + [System.IO.Path]::DirectorySeparatorChar,
                                         [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Editor package output escaped the repository dist directory."
}

& (Join-Path $PSScriptRoot "validate-sprite-atlases.ps1")
& (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration -EditorOnly

$preset = "windows-$($Configuration.ToLowerInvariant())"
$buildRoot = Join-Path $repoRoot "build/$preset"
$editorSource = Join-Path $buildRoot "IC_2DE-Editor.exe"
$manifestSource = Join-Path $buildRoot "IC_2DE.runtime"
$contentSource = Join-Path $buildRoot "Content"

foreach ($requiredPath in @($editorSource, $manifestSource, $contentSource)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Editor package input is missing: $requiredPath"
    }
}

if (Test-Path -LiteralPath $resolvedPackageRoot) {
    Remove-Item -LiteralPath $resolvedPackageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedPackageRoot -Force | Out-Null

Copy-Item -LiteralPath $editorSource -Destination (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") -Force
Copy-Item -LiteralPath $manifestSource -Destination (Join-Path $resolvedPackageRoot "IC_2DE.runtime") -Force
Copy-Item -LiteralPath $contentSource -Destination (Join-Path $resolvedPackageRoot "Content") -Recurse -Force

Push-Location $resolvedPackageRoot
try {
    & (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") --validate-content
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged editor content validation failed with code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

if ($RunHotSwapProbe) {
    & (Join-Path $PSScriptRoot "verify-editor-hotswap.ps1") `
        -BuildDirectory "dist/editor-windows-x64"
}

if ($RunDodgeProbe) {
    $packagedCaptureDirectory = Join-Path $resolvedPackageRoot "build"
    New-Item -ItemType Directory -Path $packagedCaptureDirectory -Force | Out-Null
    Push-Location $resolvedPackageRoot
    try {
        & (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") --smoke-dodge --fps=60
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged editor dodge probe failed with code $LASTEXITCODE."
        }
        $packagedCapture = Join-Path $packagedCaptureDirectory "runtime-dodge-smoke.png"
        if (-not (Test-Path -LiteralPath $packagedCapture)) {
            throw "Packaged editor dodge probe did not produce its capture."
        }
        Copy-Item -LiteralPath $packagedCapture `
            -Destination (Join-Path $repoRoot "build/runtime-dodge-packaged-smoke.png") -Force
    }
    finally {
        Pop-Location
    }
    Remove-Item -LiteralPath $packagedCaptureDirectory -Recurse -Force
}

if ($RunGameplayReplayProbe) {
    $packagedCaptureDirectory = Join-Path $resolvedPackageRoot "build"
    New-Item -ItemType Directory -Path $packagedCaptureDirectory -Force | Out-Null
    Push-Location $resolvedPackageRoot
    try {
        & (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") `
            --smoke-gameplay-replay --fps=60
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged editor gameplay replay probe failed with code $LASTEXITCODE."
        }
        $packagedCapture = Join-Path $packagedCaptureDirectory `
            "runtime-gameplay-digest-smoke.png"
        if (-not (Test-Path -LiteralPath $packagedCapture)) {
            throw "Packaged editor gameplay replay probe did not produce its capture."
        }
        Copy-Item -LiteralPath $packagedCapture `
            -Destination (Join-Path $repoRoot `
                "build/runtime-gameplay-digest-packaged-smoke.png") -Force
    }
    finally {
        Pop-Location
    }
    Remove-Item -LiteralPath $packagedCaptureDirectory -Recurse -Force
}

if ($RunMovingAttackerProbe) {
    $packagedCaptureDirectory = Join-Path $resolvedPackageRoot "build"
    New-Item -ItemType Directory -Path $packagedCaptureDirectory -Force | Out-Null
    Push-Location $resolvedPackageRoot
    try {
        & (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") `
            --smoke-moving-attacker --fps=60
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged editor moving-attacker probe failed with code $LASTEXITCODE."
        }
        $packagedCapture = Join-Path $packagedCaptureDirectory `
            "runtime-moving-attacker-smoke.png"
        if (-not (Test-Path -LiteralPath $packagedCapture)) {
            throw "Packaged editor moving-attacker probe did not produce its capture."
        }
        Copy-Item -LiteralPath $packagedCapture `
            -Destination (Join-Path $repoRoot `
                "build/runtime-moving-attacker-packaged-smoke.png") -Force
    }
    finally {
        Pop-Location
    }
    Remove-Item -LiteralPath $packagedCaptureDirectory -Recurse -Force
}

if ($RunNavigationGridProbe) {
    $packagedCaptureDirectory = Join-Path $resolvedPackageRoot "build"
    New-Item -ItemType Directory -Path $packagedCaptureDirectory -Force | Out-Null
    Push-Location $resolvedPackageRoot
    try {
        & (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") `
            --smoke-nav-grid --fps=60
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged editor navigation-grid probe failed with code $LASTEXITCODE."
        }
        $packagedCapture = Join-Path $packagedCaptureDirectory "runtime-nav-grid-smoke.png"
        if (-not (Test-Path -LiteralPath $packagedCapture)) {
            throw "Packaged editor navigation-grid probe did not produce its capture."
        }
        Copy-Item -LiteralPath $packagedCapture `
            -Destination (Join-Path $repoRoot "build/runtime-nav-grid-packaged-smoke.png") -Force
    }
    finally {
        Pop-Location
    }
    Remove-Item -LiteralPath $packagedCaptureDirectory -Recurse -Force
}

if ($RunNavigationPathProbe) {
    $packagedCaptureDirectory = Join-Path $resolvedPackageRoot "build"
    New-Item -ItemType Directory -Path $packagedCaptureDirectory -Force | Out-Null
    Push-Location $resolvedPackageRoot
    try {
        & (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") `
            --smoke-nav-path --fps=60
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged editor navigation-path probe failed with code $LASTEXITCODE."
        }
        $packagedCapture = Join-Path $packagedCaptureDirectory "runtime-nav-path-smoke.png"
        if (-not (Test-Path -LiteralPath $packagedCapture)) {
            throw "Packaged editor navigation-path probe did not produce its capture."
        }
        Copy-Item -LiteralPath $packagedCapture `
            -Destination (Join-Path $repoRoot "build/runtime-nav-path-packaged-smoke.png") -Force
    }
    finally {
        Pop-Location
    }
    Remove-Item -LiteralPath $packagedCaptureDirectory -Recurse -Force
}

if ($RunRunnerPathProbe) {
    $packagedCaptureDirectory = Join-Path $resolvedPackageRoot "build"
    New-Item -ItemType Directory -Path $packagedCaptureDirectory -Force | Out-Null
    Push-Location $resolvedPackageRoot
    try {
        & (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") `
            --smoke-runner-path --fps=60
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged editor Runner-path probe failed with code $LASTEXITCODE."
        }
        $packagedCapture = Join-Path $packagedCaptureDirectory `
            "runtime-runner-path-smoke.png"
        if (-not (Test-Path -LiteralPath $packagedCapture)) {
            throw "Packaged editor Runner-path probe did not produce its capture."
        }
        Copy-Item -LiteralPath $packagedCapture `
            -Destination (Join-Path $repoRoot `
                "build/runtime-runner-path-packaged-smoke.png") -Force
    }
    finally {
        Pop-Location
    }
    Remove-Item -LiteralPath $packagedCaptureDirectory -Recurse -Force
}

if ($RunEnemyStressProbe) {
    $packagedCaptureDirectory = Join-Path $resolvedPackageRoot "build"
    New-Item -ItemType Directory -Path $packagedCaptureDirectory -Force | Out-Null
    Push-Location $resolvedPackageRoot
    try {
        & (Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe") `
            --smoke-enemy-stress --fps=60
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged editor enemy-stress probe failed with code $LASTEXITCODE."
        }
        $packagedCapture = Join-Path $packagedCaptureDirectory `
            "runtime-enemy-stress-smoke.png"
        if (-not (Test-Path -LiteralPath $packagedCapture)) {
            throw "Packaged editor enemy-stress probe did not produce its capture."
        }
        Copy-Item -LiteralPath $packagedCapture `
            -Destination (Join-Path $repoRoot `
                "build/runtime-enemy-stress-packaged-smoke.png") -Force
    }
    finally {
        Pop-Location
    }
    Remove-Item -LiteralPath $packagedCaptureDirectory -Recurse -Force
}

if ($RunEditorLayoutProbe) {
    $packagedCaptureDirectory = Join-Path $resolvedPackageRoot "build"
    $probeLayout = Join-Path $packagedCaptureDirectory "editor-layout-probe-v1.ini"
    New-Item -ItemType Directory -Path $packagedCaptureDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $probeLayout) {
        Remove-Item -LiteralPath $probeLayout -Force
    }
    $packagedEditor = Join-Path $resolvedPackageRoot "IC_2DE-Editor.exe"
    # The dock-node width the probe hand-arranges between the two launches. A
    # rebuild recomputes every split from the default ratios, so only a real
    # restoration can carry this value into the second launch.
    $dockNodePattern =
        '(?m)^(?<prefix>\s*DockNode\s+ID=0x[0-9A-Fa-f]+\s+Parent=0x[0-9A-Fa-f]+\s+SizeRef=)(?<width>\d+)(?<suffix>,\d+)'
    $arrangedWidth = 0

    for ($launch = 1; $launch -le 2; ++$launch) {
        $probeStdout = Join-Path $packagedCaptureDirectory "editor-layout-launch$launch-stdout.log"
        $probeStderr = Join-Path $packagedCaptureDirectory "editor-layout-launch$launch-stderr.log"
        # The layout path can contain spaces, so the argument is quoted here.
        # An unquoted value is split and the ini lands outside the package.
        $probeProcess = Start-Process -FilePath $packagedEditor `
            -ArgumentList "--smoke-nav-path", "--fps=60", "`"--editor-layout=$probeLayout`"" `
            -WorkingDirectory $resolvedPackageRoot `
            -RedirectStandardOutput $probeStdout -RedirectStandardError $probeStderr `
            -WindowStyle Hidden -PassThru
        try {
            if (-not $probeProcess.WaitForExit(60000)) {
                throw "Packaged editor layout probe launch $launch did not exit within 60 seconds."
            }
            # The timed overload returns before the exit code is published.
            $probeProcess.WaitForExit()
        }
        finally {
            if (-not $probeProcess.HasExited) {
                Stop-Process -Id $probeProcess.Id -Force
                $probeProcess.WaitForExit()
            }
        }
        if ($null -ne $probeProcess.ExitCode -and $probeProcess.ExitCode -ne 0) {
            throw "Packaged editor layout probe launch $launch failed with code $($probeProcess.ExitCode)."
        }

        $probeLog = ((Get-Content -LiteralPath $probeStdout -Raw) +
            (Get-Content -LiteralPath $probeStderr -Raw)) -replace "\r?\n", " "
        if ($launch -eq 1) {
            if ($probeLog -notmatch 'Built default editor layout') {
                throw "The packaged first launch did not build the default editor workspace."
            }
            if ($probeLog -match 'Restored editor layout') {
                throw "The packaged first launch restored a layout that should not have existed."
            }
        }
        else {
            if ($probeLog -notmatch 'Restored editor layout') {
                throw "The packaged second launch did not restore the saved editor workspace."
            }
            if ($probeLog -match 'Built default editor layout') {
                throw "The packaged second launch rebuilt the default workspace instead of restoring it."
            }
        }

        if (-not (Test-Path -LiteralPath $probeLayout -PathType Leaf)) {
            throw "Packaged editor layout probe launch $launch did not save its layout."
        }
        $layoutText = Get-Content -LiteralPath $probeLayout -Raw
        if (($layoutText -notmatch '\[Docking\]\[Data\]') -or
            ($layoutText -notmatch 'DockSpace')) {
            throw "Packaged editor layout probe launch $launch saved no usable dock tree."
        }

        $dockNodeMatch = [regex]::Match($layoutText, $dockNodePattern)
        if (-not $dockNodeMatch.Success) {
            throw "The packaged dock tree contained no sizeable dock node at launch $launch."
        }
        if ($launch -eq 1) {
            $arrangedWidth = [int]$dockNodeMatch.Groups['width'].Value + 190
            $arrangedLayout = [regex]::Replace($layoutText, $dockNodePattern, {
                param($match)
                $match.Groups['prefix'].Value + $arrangedWidth + $match.Groups['suffix'].Value
            }, 1)
            Set-Content -LiteralPath $probeLayout -Value $arrangedLayout -Encoding utf8 -NoNewline
        }
        else {
            $restoredWidth = [int]$dockNodeMatch.Groups['width'].Value
            if ($restoredWidth -ne $arrangedWidth) {
                throw ("The packaged arranged dock width was discarded. " +
                    "Expected $arrangedWidth, found $restoredWidth.")
            }
        }
    }

    $packagedCapture = Join-Path $packagedCaptureDirectory "runtime-nav-path-smoke.png"
    if (-not (Test-Path -LiteralPath $packagedCapture)) {
        throw "Packaged editor layout probe did not produce its second-launch capture."
    }
    Copy-Item -LiteralPath $packagedCapture `
        -Destination (Join-Path $repoRoot `
            "build/runtime-editor-layout-packaged-smoke.png") -Force
    Remove-Item -LiteralPath $packagedCaptureDirectory -Recurse -Force
}

if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}
Compress-Archive -Path (Join-Path $resolvedPackageRoot "*") -DestinationPath $archivePath `
    -CompressionLevel Optimal

$archive = Get-Item -LiteralPath $archivePath
$hash = Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
Write-Output "Editor package: $resolvedPackageRoot"
Write-Output "Editor archive: $($archive.FullName)"
Write-Output "Bytes: $($archive.Length)"
Write-Output "SHA256: $($hash.Hash)"
