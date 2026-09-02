[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;

public static class PlayerV2PoseDetector
{
    public static Rectangle[] Find(Bitmap bitmap, byte alphaThreshold)
    {
        int width = bitmap.Width;
        int height = bitmap.Height;
        bool[] visible = new bool[width * height];
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                visible[y * width + x] = bitmap.GetPixel(x, y).A >= alphaThreshold;
            }
        }

        bool[] visited = new bool[visible.Length];
        int[] queue = new int[visible.Length];
        var components = new List<Tuple<int, Rectangle>>();
        for (int seed = 0; seed < visible.Length; ++seed)
        {
            if (!visible[seed] || visited[seed])
            {
                continue;
            }
            int head = 0;
            int tail = 0;
            queue[tail++] = seed;
            visited[seed] = true;
            int minimumX = width;
            int minimumY = height;
            int maximumX = -1;
            int maximumY = -1;
            int count = 0;
            while (head < tail)
            {
                int current = queue[head++];
                int x = current % width;
                int y = current / width;
                minimumX = Math.Min(minimumX, x);
                minimumY = Math.Min(minimumY, y);
                maximumX = Math.Max(maximumX, x);
                maximumY = Math.Max(maximumY, y);
                ++count;
                for (int offsetY = -1; offsetY <= 1; ++offsetY)
                {
                    int neighbourY = y + offsetY;
                    if (neighbourY < 0 || neighbourY >= height)
                    {
                        continue;
                    }
                    for (int offsetX = -1; offsetX <= 1; ++offsetX)
                    {
                        int neighbourX = x + offsetX;
                        if ((offsetX == 0 && offsetY == 0) ||
                            neighbourX < 0 || neighbourX >= width)
                        {
                            continue;
                        }
                        int neighbour = neighbourY * width + neighbourX;
                        if (visible[neighbour] && !visited[neighbour])
                        {
                            visited[neighbour] = true;
                            queue[tail++] = neighbour;
                        }
                    }
                }
            }
            if (count >= 64)
            {
                components.Add(Tuple.Create(
                    count,
                    Rectangle.FromLTRB(minimumX, minimumY, maximumX + 1, maximumY + 1)));
            }
        }
        components.Sort((left, right) => right.Item1.CompareTo(left.Item1));
        if (components.Count > 8)
        {
            components.RemoveRange(8, components.Count - 8);
        }
        components.Sort((left, right) => left.Item2.Left.CompareTo(right.Item2.Left));
        var result = new Rectangle[components.Count];
        for (int index = 0; index < components.Count; ++index)
        {
            result[index] = components[index].Item2;
        }
        return result;
    }
}
'@ -ReferencedAssemblies System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $projectRoot 'art\production\main-character-v2\strips'
$assetRoot = Join-Path $projectRoot 'game\assets\runtime'
$alphaThreshold = 16
$cellPadding = 12

Get-ChildItem -LiteralPath $sourceRoot -Filter 'player-v2-*.png' | Sort-Object Name | ForEach-Object {
    $source = [System.Drawing.Bitmap]::new($_.FullName)
    try {
        $bounds = [PlayerV2PoseDetector]::Find($source, $alphaThreshold)
        if ($bounds.Count -ne 8) {
            throw "$($_.Name) must contain eight connected poses; found $($bounds.Count)."
        }
        $maximumWidth = 0
        $maximumHeight = 0
        foreach ($rectangle in $bounds) {
            $maximumWidth = [Math]::Max($maximumWidth, $rectangle.Width)
            $maximumHeight = [Math]::Max($maximumHeight, $rectangle.Height)
        }

        $cellWidth = $maximumWidth + (2 * $cellPadding)
        $cellHeight = $maximumHeight + (2 * $cellPadding)
        $packed = [System.Drawing.Bitmap]::new(
            $cellWidth * 8,
            $cellHeight,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($packed)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                for ($frameIndex = 0; $frameIndex -lt 8; $frameIndex++) {
                    $rectangle = $bounds[$frameIndex]
                    $frame = $source.Clone(
                        $rectangle,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
                    try {
                        $destinationX = $frameIndex * $cellWidth +
                            [int][Math]::Floor(($cellWidth - $frame.Width) / 2.0)
                        $destinationY = $cellHeight - $cellPadding - $frame.Height
                        $graphics.DrawImageUnscaled($frame, $destinationX, $destinationY)
                    }
                    finally {
                        $frame.Dispose()
                    }
                }
            }
            finally {
                $graphics.Dispose()
            }

            $destinationPath = Join-Path $assetRoot $_.Name
            $temporaryPath = Join-Path $assetRoot ($_.BaseName + '.packed.png')
            $packed.Save($temporaryPath, [System.Drawing.Imaging.ImageFormat]::Png)
            Move-Item -LiteralPath $temporaryPath -Destination $destinationPath -Force
            Write-Output "$($_.Name): poses=8 cell=$($cellWidth)x$($cellHeight) atlas=$($packed.Width)x$($packed.Height)"
        }
        finally {
            $packed.Dispose()
        }
    }
    finally {
        $source.Dispose()
    }
}
