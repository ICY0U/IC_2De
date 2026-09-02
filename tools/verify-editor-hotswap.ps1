param(
    [string]$BuildDirectory = "build/windows-debug"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$resolvedBuildDirectory = Join-Path $projectRoot $BuildDirectory
$probeDirectory = Join-Path $projectRoot "build/editor-hotswap-probe"
$probeCaptureDirectory = Join-Path $probeDirectory "build"
$editorExecutable = Join-Path $resolvedBuildDirectory "IC_2DE-Editor.exe"
$stagedTree = Join-Path $resolvedBuildDirectory "Content/tree-atlas.png"
$sourceTree = Join-Path $projectRoot "game/assets/runtime/tree-atlas.png"
$replacementTexture = Join-Path $projectRoot "game/assets/runtime/npc-patchwork-atlas.png"
$backupTexture = Join-Path $probeDirectory "tree-atlas.verify-backup.png"
$stdoutLog = Join-Path $probeDirectory "editor-hotswap-stdout.log"
$stderrLog = Join-Path $probeDirectory "editor-hotswap-stderr.log"
$captureFile = Join-Path $probeCaptureDirectory "editor-hot-swap-smoke.png"

foreach ($requiredFile in @($editorExecutable, $stagedTree, $sourceTree, $replacementTexture)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required editor hot-swap probe file is missing: $requiredFile"
    }
}

New-Item -ItemType Directory -Force -Path $probeCaptureDirectory | Out-Null
Copy-Item -LiteralPath $sourceTree -Destination $stagedTree -Force
Copy-Item -LiteralPath $stagedTree -Destination $backupTexture -Force

foreach ($oldResult in @($stdoutLog, $stderrLog, $captureFile)) {
    if (Test-Path -LiteralPath $oldResult) {
        Remove-Item -LiteralPath $oldResult -Force
    }
}

$editorProcess = $null
try {
    $editorProcess = Start-Process `
        -FilePath $editorExecutable `
        -ArgumentList "--smoke-editor-hot-swap", "--fps=60" `
        -WorkingDirectory $probeDirectory `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -WindowStyle Hidden `
        -PassThru

    Start-Sleep -Milliseconds 1500
    if ($editorProcess.HasExited) {
        throw "Editor probe exited before the replacement was written. Exit code: $($editorProcess.ExitCode)"
    }

    Copy-Item -LiteralPath $replacementTexture -Destination $stagedTree -Force
    if (-not $editorProcess.WaitForExit(15000)) {
        throw "Editor probe did not observe the texture change within 15 seconds."
    }
    $editorProcess.WaitForExit()
    if ($null -ne $editorProcess.ExitCode -and $editorProcess.ExitCode -ne 0) {
        throw "Editor probe failed with exit code $($editorProcess.ExitCode)."
    }
}
finally {
    if ($null -ne $editorProcess -and -not $editorProcess.HasExited) {
        Stop-Process -Id $editorProcess.Id -Force
        $editorProcess.WaitForExit()
    }
    if (Test-Path -LiteralPath $backupTexture -PathType Leaf) {
        Copy-Item -LiteralPath $backupTexture -Destination $stagedTree -Force
        Remove-Item -LiteralPath $backupTexture -Force
    }
}

$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceTree).Hash
$stagedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $stagedTree).Hash
if ($sourceHash -ne $stagedHash) {
    throw "The staged tree atlas was not restored after the editor probe."
}
if (-not (Test-Path -LiteralPath $captureFile -PathType Leaf)) {
    throw "The editor probe did not write its captured frame."
}

$stdout = Get-Content -LiteralPath $stdoutLog -Raw
$stderr = Get-Content -LiteralPath $stderrLog -Raw
if (($stdout + $stderr) -notmatch "Texture hot reloaded revision 2") {
    throw "The editor probe did not report a successful live texture replacement."
}

Write-Output $stdout.TrimEnd()
if ($stderr.Length -gt 0) {
    Write-Output $stderr.TrimEnd()
}
Write-Output "Editor hot-swap capture: $captureFile"
Write-Output "Restored tree atlas SHA256: $stagedHash"
