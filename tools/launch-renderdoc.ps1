[CmdletBinding()]
param(
    [string]$Executable = (Join-Path $PSScriptRoot "..\dist\editor-windows-x64\IC_2DE-Editor.exe"),
    [string]$RenderDocCmd = "C:\Program Files\RenderDoc\renderdoccmd.exe",
    [switch]$WaitForExit,
    [switch]$ApiValidation
)

$ErrorActionPreference = "Stop"

$Executable = [System.IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Editor executable not found: $Executable"
}
if (-not (Test-Path -LiteralPath $RenderDocCmd -PathType Leaf)) {
    throw "renderdoccmd.exe not found: $RenderDocCmd"
}

$workingDir = Split-Path -Parent $Executable
$captureDir = Join-Path $workingDir "RenderDocCaptures"
New-Item -ItemType Directory -Force -Path $captureDir | Out-Null
$captureTemplate = Join-Path $captureDir "IC_2DE"

$rdcArgs = @(
    "capture",
    "-d", $workingDir,
    "-c", $captureTemplate
)
if ($WaitForExit) { $rdcArgs += "-w" }
if ($ApiValidation) { $rdcArgs += "--opt-api-validation" }
$rdcArgs += $Executable

Write-Host "Launching '$Executable' under RenderDoc (captures -> $captureDir)..."
& $RenderDocCmd @rdcArgs
