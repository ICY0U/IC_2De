[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$assetRoot = Join-Path $projectRoot 'game\assets\runtime'
$padding = 12
$walkDurationsMs = @(117, 83, 100, 100, 117, 83, 100, 100)
$dodgeDurationsMs = @(17, 17, 33, 33, 33, 33, 17, 17)

$strips = @(
    @{ Name = 'player-v2-walk-south'; Kind = 'walk'; Direction = 'south' },
    @{ Name = 'player-v2-walk-north'; Kind = 'walk'; Direction = 'north' },
    @{ Name = 'player-v2-walk-east'; Kind = 'walk'; Direction = 'east'; Mirror = 'west' },
    @{ Name = 'player-v2-walk-southeast'; Kind = 'walk'; Direction = 'southeast'; Mirror = 'southwest' },
    @{ Name = 'player-v2-walk-northeast'; Kind = 'walk'; Direction = 'northeast'; Mirror = 'northwest' },
    @{ Name = 'player-v2-dodge-south'; Kind = 'dodge'; Direction = 'south' },
    @{ Name = 'player-v2-dodge-north'; Kind = 'dodge'; Direction = 'north' },
    @{ Name = 'player-v2-dodge-east'; Kind = 'dodge'; Direction = 'east'; Mirror = 'west' },
    @{ Name = 'player-v2-dodge-southeast'; Kind = 'dodge'; Direction = 'southeast'; Mirror = 'southwest' },
    @{ Name = 'player-v2-dodge-northeast'; Kind = 'dodge'; Direction = 'northeast'; Mirror = 'northwest' }
)

function Get-FrameRectangle {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$FrameIndex,
        [int]$MinimumLocalX,
        [int]$MaximumLocalX,
        [int]$MinimumY,
        [int]$MaximumY
    )

    $cellStart = [int][Math]::Floor($FrameIndex * $Bitmap.Width / 8.0)
    $cellEnd = [int][Math]::Floor(($FrameIndex + 1) * $Bitmap.Width / 8.0)
    $x = [Math]::Max($cellStart, $cellStart + $MinimumLocalX - $padding)
    $right = [Math]::Min($cellEnd, $cellStart + $MaximumLocalX + $padding + 1)
    return [ordered]@{
        x = $x
        y = [Math]::Max(0, $MinimumY - $padding)
        w = $right - $x
        h = [Math]::Min($Bitmap.Height, $MaximumY + $padding + 1) - [Math]::Max(0, $MinimumY - $padding)
    }
}

foreach ($strip in $strips) {
    $imageName = "$($strip.Name).png"
    $imagePath = Join-Path $assetRoot $imageName
    $bitmap = [System.Drawing.Bitmap]::new($imagePath)
    try {
        $minimumLocalX = [int]::MaxValue
        $maximumLocalX = -1
        $minimumY = [int]::MaxValue
        $maximumY = -1

        for ($frameIndex = 0; $frameIndex -lt 8; $frameIndex++) {
            $cellStart = [int][Math]::Floor($frameIndex * $bitmap.Width / 8.0)
            $cellEnd = [int][Math]::Floor(($frameIndex + 1) * $bitmap.Width / 8.0)
            for ($y = 0; $y -lt $bitmap.Height; $y++) {
                for ($x = $cellStart; $x -lt $cellEnd; $x++) {
                    if ($bitmap.GetPixel($x, $y).A -lt 16) {
                        continue
                    }
                    $minimumLocalX = [Math]::Min($minimumLocalX, $x - $cellStart)
                    $maximumLocalX = [Math]::Max($maximumLocalX, $x - $cellStart)
                    $minimumY = [Math]::Min($minimumY, $y)
                    $maximumY = [Math]::Max($maximumY, $y)
                }
            }
        }
        if ($maximumLocalX -lt 0 -or $maximumY -lt 0) {
            throw "$imageName contains no visible sprite pixels."
        }

        $durations = if ($strip.Kind -eq 'walk') { $walkDurationsMs } else { $dodgeDurationsMs }
        $frames = [System.Collections.Generic.List[object]]::new()
        for ($frameIndex = 0; $frameIndex -lt 8; $frameIndex++) {
            $events = @()
            if ($strip.Kind -eq 'walk' -and ($frameIndex -eq 0 -or $frameIndex -eq 4)) {
                $events = @('footstep')
            }
            elseif ($strip.Kind -eq 'dodge' -and $frameIndex -eq 1) {
                $events = @('dodge_launch')
            }
            elseif ($strip.Kind -eq 'dodge' -and $frameIndex -eq 5) {
                $events = @('dodge_recover')
            }

            $frame = [ordered]@{
                filename = "$($strip.Name)-$($frameIndex + 1)"
                frame = Get-FrameRectangle $bitmap $frameIndex $minimumLocalX $maximumLocalX $minimumY $maximumY
                rotated = $false
                trimmed = $false
                duration = $durations[$frameIndex]
            }
            if ($events.Count -gt 0) {
                $frame.ic2d_events = $events
            }
            $frames.Add($frame)
        }

        if ($strip.Mirror) {
            for ($frameIndex = 0; $frameIndex -lt 8; $frameIndex++) {
                $mirrored = [ordered]@{
                    filename = "$($strip.Name)-$($strip.Mirror)-$($frameIndex + 1)"
                    frame = Get-FrameRectangle $bitmap $frameIndex $minimumLocalX $maximumLocalX $minimumY $maximumY
                    rotated = $false
                    trimmed = $false
                    duration = $durations[$frameIndex]
                    ic2d_flip_x = $true
                }
                if ($frames[$frameIndex].Contains('ic2d_events')) {
                    $mirrored.ic2d_events = $frames[$frameIndex].ic2d_events
                }
                $frames.Add($mirrored)
            }
        }

        $tags = [System.Collections.Generic.List[object]]::new()
        if ($strip.Kind -eq 'walk') {
            $tags.Add([ordered]@{ name = "player-v2-idle-$($strip.Direction)"; from = 0; to = 0; direction = 'forward' })
            $tags.Add([ordered]@{ name = "player-v2-move-$($strip.Direction)"; from = 0; to = 7; direction = 'forward' })
            if ($strip.Mirror) {
                $tags.Add([ordered]@{ name = "player-v2-idle-$($strip.Mirror)"; from = 8; to = 8; direction = 'forward' })
                $tags.Add([ordered]@{ name = "player-v2-move-$($strip.Mirror)"; from = 8; to = 15; direction = 'forward' })
            }
        }
        else {
            $tags.Add([ordered]@{ name = "player-v2-dodge-$($strip.Direction)"; from = 0; to = 7; direction = 'forward' })
            if ($strip.Mirror) {
                $tags.Add([ordered]@{ name = "player-v2-dodge-$($strip.Mirror)"; from = 8; to = 15; direction = 'forward' })
            }
        }

        $metadata = [ordered]@{
            frames = $frames
            meta = [ordered]@{
                app = 'IC_2DE player V2 importer'
                version = '1.0'
                image = $imageName
                format = 'RGBA8888'
                size = [ordered]@{ w = $bitmap.Width; h = $bitmap.Height }
                scale = '1'
                frameTags = $tags
            }
        }
        $jsonPath = Join-Path $assetRoot "$($strip.Name).json"
        $metadata | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding utf8
        Write-Output "$($strip.Name): $($bitmap.Width)x$($bitmap.Height), frames=$($frames.Count), tags=$($tags.Count)"
    }
    finally {
        $bitmap.Dispose()
    }
}
