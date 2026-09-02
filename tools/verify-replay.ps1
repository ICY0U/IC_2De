[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$preset = "windows-$($Configuration.ToLowerInvariant())"
$executable = Join-Path $repoRoot "build\$preset\IC_2DE-Editor.exe"

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
        @{ Name = "30 Hz"; Args = @("--smoke-gameplay-replay", "--fps=30") },
        @{ Name = "60 Hz"; Args = @("--smoke-gameplay-replay", "--fps=60") },
        @{ Name = "120 Hz"; Args = @("--smoke-gameplay-replay", "--fps=120") },
        @{ Name = "monitor"; Args = @("--smoke-gameplay-replay", "--monitor-hz") },
        @{ Name = "uncapped"; Args = @("--smoke-gameplay-replay", "--uncapped") }
    )
    $expectedHash = $null
    $expectedSchema = $null
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
        $hashLine = $output | Where-Object { $_ -match 'Gameplay state digest v([0-9]+): ([0-9]+)\.' } |
            Select-Object -Last 1
        if (-not $hashLine) {
            $output | Write-Host
            throw "Replay mode '$($mode.Name)' did not report a state hash."
        }
        $match = [regex]::Match($hashLine, 'Gameplay state digest v([0-9]+): ([0-9]+)\.')
        $schema = $match.Groups[1].Value
        $hash = $match.Groups[2].Value
        if ($null -eq $expectedHash) {
            $expectedHash = $hash
            $expectedSchema = $schema
        } elseif ($hash -ne $expectedHash -or $schema -ne $expectedSchema) {
            throw "Replay divergence in '$($mode.Name)': expected v$expectedSchema/$expectedHash, received v$schema/$hash."
        }
        Write-Host ("{0,-9} digest v{1} {2}" -f $mode.Name, $schema, $hash)
    }

    Write-Host "Gameplay replay verification passed across all presentation modes."
}
finally {
    Pop-Location
}
