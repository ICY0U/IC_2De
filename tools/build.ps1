[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$RunTests,

    # Skips the tests labelled "gpu", which drive a real executable and open a
    # window. A headless machine, a continuous integration runner typically,
    # can still run everything else.
    [switch]$ExcludeGpuTests,
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
# Visual Studio's bundled CMake and Ninja are preferred, so that an ordinary
# local build uses the same toolchain the project is developed against. They
# come from an optional workload component, though, and a machine without it can
# still build perfectly well with whatever is already on PATH. Failing outright
# there would make this script the reason the build does not run.
$bundledToolchain = ""
if (Test-Path -LiteralPath (Join-Path $cmakeDirectory "cmake.exe")) {
    $bundledToolchain = "$cmakeDirectory;$ninjaDirectory;"
}

$env:Path = "$vsWhereDirectory;$env:Path"
$developerEnvironment = & $env:ComSpec /d /s /c "`"$devCommand`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $developerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
}

$env:Path = "$bundledToolchain$env:Path"

function Find-Tool {
    param([string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "$Name was not found. Install Visual Studio's C++ CMake tools component, or put it on PATH."
    }
    return $command.Source
}

$cmake = Find-Tool "cmake"
$ctest = Find-Tool "ctest"
# Every preset selects the Ninja generator, so its absence would otherwise
# surface later as a confusing configure error.
$null = Find-Tool "ninja"
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
        # Driven through ctest rather than the "test" build target, because
        # only ctest takes the label filter.
        $ctestArguments = @("--preset", $preset)
        if ($ExcludeGpuTests) {
            $ctestArguments += @("--label-exclude", "gpu")
        }
        & $ctest @ctestArguments
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
