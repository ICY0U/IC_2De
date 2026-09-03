<#
.SYNOPSIS
    Formats the IC_2DE C++ sources with clang-format, or checks that they are
    already formatted.

.DESCRIPTION
    clang-format is not on PATH on a stock Windows development machine, so this
    script locates the copy bundled with Visual Studio the same way
    tools/build.ps1 locates CMake and Ninja. That keeps the formatter version
    tied to the toolchain the project already builds with, rather than to
    whatever a contributor happens to have installed.

.PARAMETER Check
    Reports files that are not formatted and exits non-zero without writing to
    them. This is the mode continuous integration runs.
#>
[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

# Third-party sources vendored into the tree. Reformatting them would produce a
# diff against upstream for no benefit and make future updates harder to apply.
$vendoredPaths = @(
    "engine/src/core/renderdoc_app.h"
)

function Find-ClangFormat {
    $onPath = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($null -ne $onPath) {
        return $onPath.Source
    }

    $vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vsWhere)) {
        throw "clang-format is not on PATH and Visual Studio Installer's vswhere.exe was not found."
    }

    $visualStudioPath = & $vsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudioPath) {
        throw "clang-format is not on PATH and no Visual Studio C++ installation was found."
    }

    $candidate = Join-Path $visualStudioPath "VC\Tools\Llvm\x64\bin\clang-format.exe"
    if (-not (Test-Path -LiteralPath $candidate)) {
        throw "Visual Studio's bundled clang-format was not found at: $candidate"
    }
    return $candidate
}

$clangFormat = Find-ClangFormat

Push-Location $repoRoot
try {
    $excluded = $vendoredPaths | ForEach-Object {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $_))
    }

    $files = Get-ChildItem -Path "engine", "game", "tests" -Recurse -File `
        -Include *.cpp, *.hpp, *.h |
        Where-Object { $excluded -notcontains $_.FullName }

    if ($files.Count -eq 0) {
        throw "No C++ sources were found to format."
    }

    if ($Check) {
        # --output-replacements-xml reports on stdout and exits zero whatever it
        # finds: a file needs formatting exactly when the document holds at
        # least one <replacement> element. The --dry-run -Werror spelling is
        # more obvious but writes its diagnostics to stderr, and Windows
        # PowerShell turns a native command's stderr into terminating error
        # records, so it cannot be used from a script with a Stop preference.
        $unformatted = @()
        foreach ($file in $files) {
            $replacements = & $clangFormat --output-replacements-xml --style=file $file.FullName
            if ($LASTEXITCODE -ne 0) {
                throw "clang-format failed on $($file.FullName)."
            }
            if ($replacements -match "<replacement ") {
                $unformatted += $file.FullName.Substring($repoRoot.Length + 1)
            }
        }

        if ($unformatted.Count -gt 0) {
            Write-Host "The following files are not formatted:"
            foreach ($path in $unformatted) {
                Write-Host "  $path"
            }
            Write-Host ""
            Write-Host "Run tools/format.ps1 to fix them."
            exit 1
        }

        Write-Host "All $($files.Count) files are correctly formatted."
    }
    else {
        & $clangFormat -i --style=file @($files.FullName)
        if ($LASTEXITCODE -ne 0) {
            throw "clang-format failed."
        }
        Write-Host "Formatted $($files.Count) files."
    }
}
finally {
    Pop-Location
}
