[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$preset = "windows-$($Configuration.ToLowerInvariant())"
$executable = Join-Path $repoRoot "build\$preset\ic2de_testbed.exe"

Push-Location $repoRoot
try {
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration -RunTests
        if ($LASTEXITCODE -ne 0) { throw "The $Configuration build or tests failed." }
    }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "The replay testbed does not exist: $executable"
    }

    $modes = @(
        @{ Name = "30 Hz"; Args = @("--smoke-movement", "--fps=30") },
        @{ Name = "60 Hz"; Args = @("--smoke-movement", "--fps=60") },
        @{ Name = "120 Hz"; Args = @("--smoke-movement", "--fps=120") },
        @{ Name = "monitor"; Args = @("--smoke-movement", "--monitor-hz") },
        @{ Name = "uncapped"; Args = @("--smoke-movement", "--uncapped") }
    )
    $expectedHash = $null
    foreach ($mode in $modes) {
        $stdoutPath = [System.IO.Path]::GetTempFileName()
        $stderrPath = [System.IO.Path]::GetTempFileName()
        try {
            $process = Start-Process -FilePath $executable -ArgumentList @($mode.Args) `
                -WorkingDirectory $repoRoot -NoNewWindow -Wait -PassThru `
                -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
            $output = @(
                Get-Content -LiteralPath $stdoutPath
                Get-Content -LiteralPath $stderrPath
            )
            $exitCode = $process.ExitCode
        }
        finally {
            Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
        }
        if ($exitCode -ne 0) {
            $output | Write-Host
            throw "Replay mode '$($mode.Name)' exited with code $exitCode."
        }
        $hashLine = $output | Where-Object { $_ -match 'Replay state hash: ([0-9]+)\.' } |
            Select-Object -Last 1
        if (-not $hashLine) {
            $output | Write-Host
            throw "Replay mode '$($mode.Name)' did not report a state hash."
        }
        $hash = [regex]::Match($hashLine, 'Replay state hash: ([0-9]+)\.').Groups[1].Value
        if ($null -eq $expectedHash) {
            $expectedHash = $hash
        } elseif ($hash -ne $expectedHash) {
            throw "Replay divergence in '$($mode.Name)': expected $expectedHash, received $hash."
        }
        Write-Host ("{0,-9} hash {1}" -f $mode.Name, $hash)
    }

    Write-Host "Replay verification passed across all presentation modes."
}
finally {
    Pop-Location
}
