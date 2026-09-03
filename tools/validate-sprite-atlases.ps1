$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$assetRoot = Join-Path $projectRoot 'game\assets\runtime'
$atlasChecks = @(
    @{ Metadata = 'player-atlas.json'; MinimumBottomGap = 2; MaximumBottomSpread = 3 },
    @{ Metadata = 'player-diagonal-atlas.json'; MinimumBottomGap = 2; MaximumBottomSpread = 3 },
    @{ Metadata = 'player-v2-walk-south.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-walk-north.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-walk-east.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-walk-southeast.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-walk-northeast.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-dodge-south.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-dodge-north.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-dodge-east.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-dodge-southeast.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'player-v2-dodge-northeast.json'; MinimumBottomGap = 2; MaximumBottomSpread = 40; TransparentCorners = $true },
    @{ Metadata = 'tree-atlas.json'; MinimumBottomGap = 1; MaximumBottomSpread = 0 }
)
$atlasChecks += @(Get-ChildItem -LiteralPath $assetRoot -Filter 'player-v3-*.json' |
    Sort-Object Name |
    ForEach-Object {
        @{
            Metadata = $_.Name
            MinimumBottomGap = 2
            MaximumBottomSpread = 16
            TransparentCorners = $true
        }
    })
$atlasChecks += @(Get-ChildItem -LiteralPath $assetRoot -Filter 'fuse-*-atlas.json' |
    Sort-Object Name |
    ForEach-Object {
        @{
            Metadata = $_.Name
            MinimumBottomGap = 1
            # Death and explosion clips intentionally finish as elevated smoke
            # or debris; locomotion rows still share the importer-owned root.
            MaximumBottomSpread = 48
            TransparentCorners = $true
        }
    })

$failed = $false
$treeBottomGap = $null
$treeFrameHeight = $null

foreach ($atlasCheck in $atlasChecks) {
    $metadataPath = Join-Path $assetRoot $atlasCheck.Metadata
    $metadata = Get-Content -Raw -LiteralPath $metadataPath | ConvertFrom-Json
    $imagePath = Join-Path $assetRoot $metadata.meta.image
    $bitmap = [System.Drawing.Bitmap]::new($imagePath)

    try {
        if ($bitmap.Width -ne [int]$metadata.meta.size.w -or
            $bitmap.Height -ne [int]$metadata.meta.size.h) {
            Write-Error "$($atlasCheck.Metadata): metadata image size does not match the PNG."
            $failed = $true
            continue
        }

        if ($atlasCheck.ContainsKey('TransparentCorners') -and $atlasCheck.TransparentCorners) {
            $cornerAlpha = @(
                $bitmap.GetPixel(0, 0).A,
                $bitmap.GetPixel($bitmap.Width - 1, 0).A,
                $bitmap.GetPixel(0, $bitmap.Height - 1).A,
                $bitmap.GetPixel($bitmap.Width - 1, $bitmap.Height - 1).A
            )
            if (@($cornerAlpha | Where-Object { $_ -ne 0 }).Count -ne 0) {
                Write-Output "$($atlasCheck.Metadata): the production sheet does not have transparent corners."
                $failed = $true
            }
        }

        $bottomGaps = @()
        $boundaryFailures = 0
        $framingFailures = 0

        for ($frameIndex = 0; $frameIndex -lt $metadata.frames.Count; $frameIndex++) {
            $frame = $metadata.frames[$frameIndex].frame
            $frameX = [int]$frame.x
            $frameY = [int]$frame.y
            $frameWidth = [int]$frame.w
            $frameHeight = [int]$frame.h

            if ($frameX -lt 0 -or $frameY -lt 0 -or
                ($frameX + $frameWidth) -gt $bitmap.Width -or
                ($frameY + $frameHeight) -gt $bitmap.Height) {
                Write-Output "$($atlasCheck.Metadata): frame $frameIndex is outside the PNG."
                $boundaryFailures++
                continue
            }

            $edgePixelCount = 0
            $significantBottom = -1
            $visibleByColumn = [int[]]::new($frameWidth)

            for ($localY = 0; $localY -lt $frameHeight; $localY++) {
                $rowPixelCount = 0
                for ($localX = 0; $localX -lt $frameWidth; $localX++) {
                    $alpha = $bitmap.GetPixel($frameX + $localX, $frameY + $localY).A
                    if ($alpha -eq 0) {
                        continue
                    }

                    $rowPixelCount++
                    $visibleByColumn[$localX]++
                    if ($localX -eq 0 -or $localX -eq ($frameWidth - 1) -or
                        $localY -eq 0 -or $localY -eq ($frameHeight - 1)) {
                        $edgePixelCount++
                    }
                }

                # Ignore isolated compression/noise pixels when finding the feet or roots.
                if ($rowPixelCount -ge 4) {
                    $significantBottom = $localY
                }
            }

            if ($significantBottom -lt 0) {
                Write-Output "$($atlasCheck.Metadata): frame $frameIndex has no visible sprite."
                $boundaryFailures++
                continue
            }

            if ($edgePixelCount -gt 4) {
                Write-Output "$($atlasCheck.Metadata): frame $frameIndex touches a source boundary ($edgePixelCount pixels)."
                $boundaryFailures++
            }

            if ($atlasCheck.Metadata -like 'fuse-*-atlas.json' -and
                ($metadata.frames[$frameIndex].filename -like '*-idle-*' -or
                 $metadata.frames[$frameIndex].filename -like '*-move-*')) {
                $significantColumns = @(0..($frameWidth - 1) | Where-Object {
                    $visibleByColumn[$_] -ge 2
                })
                $maximumInternalGap = 0
                for ($column = 1; $column -lt $significantColumns.Count; $column++) {
                    $maximumInternalGap = [Math]::Max(
                        $maximumInternalGap,
                        $significantColumns[$column] - $significantColumns[$column - 1] - 1)
                }
                if ($maximumInternalGap -gt 16) {
                    Write-Output "$($atlasCheck.Metadata): frame $frameIndex has a disconnected framing fragment (column gap $maximumInternalGap)."
                    $framingFailures++
                }
            }

            $bottomGaps += ($frameHeight - 1) - $significantBottom
        }

        $minimumGap = ($bottomGaps | Measure-Object -Minimum).Minimum
        $maximumGap = ($bottomGaps | Measure-Object -Maximum).Maximum
        $bottomSpread = $maximumGap - $minimumGap
        Write-Output "$($atlasCheck.Metadata): frames=$($metadata.frames.Count) boundary-failures=$boundaryFailures framing-failures=$framingFailures bottom-gap=$minimumGap..$maximumGap"

        if ($boundaryFailures -ne 0 -or $framingFailures -ne 0 -or
            $minimumGap -lt $atlasCheck.MinimumBottomGap -or
            $bottomSpread -gt $atlasCheck.MaximumBottomSpread) {
            $failed = $true
        }

        if ($atlasCheck.Metadata -eq 'player-atlas.json') {
            $westTag = $metadata.meta.frameTags | Where-Object { $_.name -eq 'player-move-west' }
            $eastIdleTag = $metadata.meta.frameTags | Where-Object { $_.name -eq 'player-idle-east' }
            $westFrames = $metadata.frames[[int]$westTag.from..[int]$westTag.to]
            $eastIdleFrames = $metadata.frames[[int]$eastIdleTag.from..[int]$eastIdleTag.to]
            $westFramesFlipped = @($westFrames | Where-Object { $_.ic2d_flip_x -ne $true }).Count -eq 0
            $eastIdleFramesFlipped =
                @($eastIdleFrames | Where-Object { $_.ic2d_flip_x -ne $true }).Count -eq 0
            $movementDurations = $metadata.meta.frameTags |
                Where-Object { $_.name -like 'player-move-*' } |
                ForEach-Object { $tag = $_; $metadata.frames[[int]$tag.from..[int]$tag.to].duration }
            $minimumMovementDuration = ($movementDurations | Measure-Object -Minimum).Minimum
            Write-Output "player presentation: west-order=$($westTag.direction) west-flip=$westFramesFlipped east-idle-flip=$eastIdleFramesFlipped minimum-move-duration-ms=$minimumMovementDuration"
            if ($westTag.direction -ne 'forward' -or -not $westFramesFlipped -or
                -not $eastIdleFramesFlipped -or $minimumMovementDuration -lt 150) {
                $failed = $true
            }
        }
        elseif ($atlasCheck.Metadata -eq 'player-diagonal-atlas.json') {
            $minimumDiagonalDuration = ($metadata.frames.duration | Measure-Object -Minimum).Minimum
            Write-Output "diagonal presentation: minimum-move-duration-ms=$minimumDiagonalDuration"
            if ($minimumDiagonalDuration -lt 150) {
                $failed = $true
            }
        }
        elseif ($atlasCheck.Metadata -like 'player-v2-walk-*.json') {
            $moveTags = @($metadata.meta.frameTags | Where-Object { $_.name -like 'player-v2-move-*' })
            $moveDurations = @($moveTags | ForEach-Object {
                $tag = $_
                $metadata.frames[[int]$tag.from..[int]$tag.to].duration
            })
            $footstepFrames = @($metadata.frames | Where-Object {
                $_.PSObject.Properties.Name -contains 'ic2d_events' -and
                $_.ic2d_events -contains 'footstep'
            })
            Write-Output "player V2 walk: tags=$($moveTags.Count) footsteps=$($footstepFrames.Count) duration-ms=$(($moveDurations | Measure-Object -Sum).Sum / $moveTags.Count)"
            if ($moveTags.Count -lt 1 -or $footstepFrames.Count -ne (2 * $moveTags.Count) -or
                (($moveDurations | Measure-Object -Minimum).Minimum) -lt 83 -or
                (($moveDurations | Measure-Object -Maximum).Maximum) -gt 117) {
                $failed = $true
            }
        }
        elseif ($atlasCheck.Metadata -like 'player-v2-dodge-*.json') {
            $dodgeTags = @($metadata.meta.frameTags | Where-Object { $_.name -like 'player-v2-dodge-*' })
            $dodgeDurations = @($dodgeTags | ForEach-Object {
                $tag = $_
                $metadata.frames[[int]$tag.from..[int]$tag.to].duration
            })
            $launchFrames = @($metadata.frames | Where-Object {
                $_.PSObject.Properties.Name -contains 'ic2d_events' -and
                $_.ic2d_events -contains 'dodge_launch'
            })
            $recoverFrames = @($metadata.frames | Where-Object {
                $_.PSObject.Properties.Name -contains 'ic2d_events' -and
                $_.ic2d_events -contains 'dodge_recover'
            })
            Write-Output "player V2 dodge: tags=$($dodgeTags.Count) launches=$($launchFrames.Count) recovers=$($recoverFrames.Count) duration-ms=$(($dodgeDurations | Measure-Object -Sum).Sum / $dodgeTags.Count)"
            if ($dodgeTags.Count -lt 1 -or $launchFrames.Count -ne $dodgeTags.Count -or
                $recoverFrames.Count -ne $dodgeTags.Count -or
                (($dodgeDurations | Measure-Object -Sum).Sum / $dodgeTags.Count) -ne 200) {
                $failed = $true
            }
        }
        elseif ($atlasCheck.Metadata -like 'player-v3-*.json') {
            $tags = @($metadata.meta.frameTags)
            $frameCellsValid = @($metadata.frames | Where-Object {
                [int]$_.frame.w -ne 48 -or [int]$_.frame.h -ne 48
            }).Count -eq 0
            $timingsValid = $true
            foreach ($tag in $tags) {
                $duration = ($metadata.frames[[int]$tag.from..[int]$tag.to].duration |
                    ForEach-Object { [int][Math]::Round($_ * 60.0 / 1000.0) } |
                    Measure-Object -Sum).Sum
                $expected = if ($tag.name -like '*seated*') { 96 }
                    elseif ($tag.name -like '*dodge*') { 12 }
                    elseif ($tag.name -like '*shoot*') { 9 }
                    elseif ($tag.name -like '*move*') { 48 }
                    else { 72 }
                $timingsValid = $timingsValid -and $duration -eq $expected
            }
            $westTags = @($tags | Where-Object { $_.name -like '*-west' })
            $westFramesFlipped = $true
            $containsMirror = @($metadata.frames | Where-Object {
                $_.PSObject.Properties.Name -contains 'ic2d_flip_x'
            }).Count -gt 0
            if ($containsMirror) {
                foreach ($tag in $westTags) {
                    $westFramesFlipped = $westFramesFlipped -and
                        @($metadata.frames[[int]$tag.from..[int]$tag.to] | Where-Object {
                            $_.ic2d_flip_x -ne $true
                        }).Count -eq 0
                }
            }
            Write-Output "player V3: tags=$($tags.Count) cells=$frameCellsValid timings=$timingsValid west-flip=$westFramesFlipped"
            if (-not $frameCellsValid -or -not $timingsValid -or -not $westFramesFlipped) {
                $failed = $true
            }
        }
        elseif ($atlasCheck.Metadata -eq 'tree-atlas.json') {
            $minimumTreeDuration = ($metadata.frames.duration | Measure-Object -Minimum).Minimum
            $treeBottomGap = $minimumGap
            $treeFrameHeight = [int]$metadata.frames[0].frame.h
            Write-Output "tree presentation: minimum-sway-duration-ms=$minimumTreeDuration"
            if ($minimumTreeDuration -lt 200) {
                $failed = $true
            }
        }
        elseif ($atlasCheck.Metadata -like 'fuse-*-atlas.json') {
            $terminalTags = @($metadata.meta.frameTags | Where-Object {
                $_.name -like '*-hurt-*' -or $_.name -like '*-death-*' -or
                $_.name -like '*-explode-*'
            })
            $terminalTagsAreOnce = $terminalTags.Count -eq 3 -and
                @($terminalTags | Where-Object { $_.ic2d_loop_mode -ne 'once' }).Count -eq 0

            $explodeTag = $terminalTags | Where-Object { $_.name -like '*-explode-*' }
            $explodeFrames = @($metadata.frames[[int]$explodeTag.from..[int]$explodeTag.to])
            $displaySize = if ($atlasCheck.Metadata -like '*stalker*') { 68.0 } else { 96.0 }
            $effectiveBlastWidths = @()
            foreach ($explodeFrame in $explodeFrames[5..8]) {
                $frame = $explodeFrame.frame
                $minimumX = [int]$frame.w
                $maximumX = -1
                for ($localY = 0; $localY -lt [int]$frame.h; $localY++) {
                    for ($localX = 0; $localX -lt [int]$frame.w; $localX++) {
                        if ($bitmap.GetPixel(
                                [int]$frame.x + $localX,
                                [int]$frame.y + $localY).A -eq 0) {
                            continue
                        }
                        $minimumX = [Math]::Min($minimumX, $localX)
                        $maximumX = [Math]::Max($maximumX, $localX)
                    }
                }
                $presentationScale = if (
                    $explodeFrame.PSObject.Properties.Name -contains 'ic2d_scale'
                ) { [double]$explodeFrame.ic2d_scale } else { 1.0 }
                $sourceWidth = if ($maximumX -ge $minimumX) {
                    $maximumX - $minimumX + 1
                } else { 0 }
                $effectiveBlastWidths +=
                    $sourceWidth * $displaySize / [double]$frame.w * $presentationScale
            }
            $peakBlastWidth = ($effectiveBlastWidths | Measure-Object -Maximum).Maximum
            $readableBlastFrames = @($effectiveBlastWidths | Where-Object {
                $_ -ge ($displaySize * 0.7)
            }).Count
            $explodeDuration = ($explodeFrames.duration | Measure-Object -Sum).Sum
            Write-Output (
                "$($atlasCheck.Metadata): terminal-tags=$($terminalTags.Count) " +
                "once=$terminalTagsAreOnce explosion-peak=$([Math]::Round($peakBlastWidth, 1))px " +
                "readable-blast-frames=$readableBlastFrames duration=${explodeDuration}ms"
            )
            if (-not $terminalTagsAreOnce -or
                $peakBlastWidth -lt ($displaySize * 0.9) -or
                $readableBlastFrames -lt 2 -or $explodeDuration -lt 1000) {
                $failed = $true
            }
        }
    }
    finally {
        $bitmap.Dispose()
    }
}

$scenePath = Join-Path $assetRoot 'test_area.scene'
$sceneLines = Get-Content -LiteralPath $scenePath
$requiredFuseReactionBindings = @(
    'animation_binding=enemy|hurt_south|fuse-tyrant-hurt-south|false',
    'animation_binding=enemy|death_south|fuse-tyrant-death-south|false',
    'animation_binding=enemy|explode_south|fuse-tyrant-explode-south|false',
    'animation_binding=attacker|hurt_south|fuse-stalker-hurt-south|false',
    'animation_binding=attacker|death_south|fuse-stalker-death-south|false',
    'animation_binding=attacker|explode_south|fuse-stalker-explode-south|false'
)
$missingFuseReactionBindings = @($requiredFuseReactionBindings | Where-Object {
    $sceneLines -notcontains $_
})
Write-Output "Fuse reaction bindings: missing=$($missingFuseReactionBindings.Count)"
if ($missingFuseReactionBindings.Count -ne 0) {
    $failed = $true
}
$treePrefabLine = Get-Content -LiteralPath $scenePath |
    Where-Object { $_ -like 'prefab=tree|*' } |
    Select-Object -First 1
if ($null -eq $treePrefabLine -or $null -eq $treeBottomGap -or $null -eq $treeFrameHeight) {
    Write-Output 'Tree presentation: prefab or atlas grounding data is missing.'
    $failed = $true
}
else {
    $treePrefab = $treePrefabLine.Split('|')
    $invariantCulture = [System.Globalization.CultureInfo]::InvariantCulture
    $treeDisplayHeight = [float]::Parse($treePrefab[4], $invariantCulture)
    $treeOriginY = [float]::Parse($treePrefab[6], $invariantCulture)
    $rootContactOffset =
        ((1.0 - $treeOriginY) - ($treeBottomGap / [float]$treeFrameHeight)) * $treeDisplayHeight
    Write-Output ("tree presentation: origin-y={0:N3} root-contact-offset-px={1:N2}" -f
        $treeOriginY, $rootContactOffset)
    if ($rootContactOffset -lt 2.0 -or $rootContactOffset -gt 6.0) {
        $failed = $true
    }
}

if ($failed) {
    throw 'Sprite atlas animation/grounding validation failed.'
}

Write-Output 'Sprite atlas animation/grounding validation passed.'
