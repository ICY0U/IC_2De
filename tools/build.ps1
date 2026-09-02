[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$RunTests,
    [switch]$Launch,
    [switch]$EditorOnly,
    [switch]$Shipping,
    [switch]$Install,
    [string]$InstallPrefix,

    # Extra arguments forwarded to the executable started by -Launch, so
    # editor buttons can select a scene without editing this script.
    [string[]]$LaunchArgs = @()
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $vsWhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found."
}

$visualStudioPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioPath) {
    throw "Visual Studio with the Desktop development with C++ workload was not found."
}

$vsWhereDirectory = Split-Path -Parent $vsWhere
$devCommand = Join-Path $visualStudioPath "Common7\Tools\VsDevCmd.bat"
$cmakeDirectory = Join-Path $visualStudioPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaDirectory = Join-Path $visualStudioPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$cmake = Join-Path $cmakeDirectory "cmake.exe"

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Visual Studio's bundled CMake was not found at: $cmake"
}

$env:Path = "$vsWhereDirectory;$env:Path"
$developerEnvironment = & $env:ComSpec /d /s /c "`"$devCommand`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $developerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
}

$env:Path = "$cmakeDirectory;$ninjaDirectory;$env:Path"
if ($Shipping -and $Configuration -ne "Release") {
    throw "Shipping builds require -Configuration Release."
}
if ($Shipping -and $RunTests) {
    throw "The shipping preset excludes test targets; run Release tests separately."
}
if ($EditorOnly -and $Shipping) {
    throw "-EditorOnly and -Shipping select different build products."
}
if ($EditorOnly -and $Install) {
    throw "Editor-only packaging is handled by tools/package-editor.ps1."
}
if ($EditorOnly -and $RunTests) {
    throw "Editor-only builds exclude the full test target; run tests as a separate checkpoint."
}
$preset = if ($Shipping) { "windows-shipping" } else { "windows-$($Configuration.ToLowerInvariant())" }

Push-Location $repoRoot
try {
    & $cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    if ($EditorOnly) {
        & $cmake --build --preset $preset --target ic2de_editor_app
    }
    else {
        & $cmake --build --preset $preset
    }
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }

    if ($RunTests) {
        & $cmake --build --preset $preset --target test
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
    }

    if ($Install) {
        if ([string]::IsNullOrWhiteSpace($InstallPrefix)) {
            throw "-Install requires an explicit -InstallPrefix."
        }
        $resolvedInstallPrefix = [System.IO.Path]::GetFullPath($InstallPrefix)
        & $cmake --install "build\$preset" --prefix $resolvedInstallPrefix --config Release `
            --component Runtime
        if ($LASTEXITCODE -ne 0) { throw "Install failed." }
    }

    if ($Launch) {
        $executableName = if ($EditorOnly) {
            "IC_2DE-Editor.exe"
        }
        elseif ($Shipping) {
            "IC_2DE.exe"
        }
        else {
            "ic2de_testbed.exe"
        }
        $executable = Join-Path $repoRoot "build\$preset\$executableName"
        & $executable @LaunchArgs
        if ($LASTEXITCODE -ne 0) { throw "The selected executable exited with code $LASTEXITCODE." }
    }
}
finally {
    Pop-Location
}
