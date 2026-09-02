[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$assetRoot = Join-Path $projectRoot 'art\production\main-character-v2\strips'
$alphaThreshold = 16

Get-ChildItem -LiteralPath $assetRoot -Filter 'player-v2-*.png' | Sort-Object Name | ForEach-Object {
    $imagePath = $_.FullName
    $temporaryPath = Join-Path $assetRoot ($_.BaseName + '.alpha-normalized.png')
    $bitmap = [System.Drawing.Bitmap]::new($imagePath)
    $locked = $null
    try {
        $rectangle = [System.Drawing.Rectangle]::new(0, 0, $bitmap.Width, $bitmap.Height)
        $locked = $bitmap.LockBits(
            $rectangle,
            [System.Drawing.Imaging.ImageLockMode]::ReadWrite,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $byteCount = [Math]::Abs($locked.Stride) * $locked.Height
        $pixels = [byte[]]::new($byteCount)
        [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $pixels, 0, $byteCount)
        $cleared = 0
        for ($offset = 3; $offset -lt $byteCount; $offset += 4) {
            if ($pixels[$offset] -eq 0 -or $pixels[$offset] -ge $alphaThreshold) {
                continue
            }
            $pixels[$offset - 3] = 0
            $pixels[$offset - 2] = 0
            $pixels[$offset - 1] = 0
            $pixels[$offset] = 0
            $cleared++
        }
        [System.Runtime.InteropServices.Marshal]::Copy($pixels, 0, $locked.Scan0, $byteCount)
        $bitmap.UnlockBits($locked)
        $locked = $null
        $bitmap.Save($temporaryPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        if ($null -ne $locked) {
            $bitmap.UnlockBits($locked)
        }
        $bitmap.Dispose()
    }
    Move-Item -LiteralPath $temporaryPath -Destination $imagePath -Force
    Write-Output "$($_.Name): cleared $cleared low-alpha pixels"
}
