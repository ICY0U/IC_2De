[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

public static class FuseEnemyImporter
{
    private static readonly Color[] Palette = new Color[] {
        Color.FromArgb(255, 8, 9, 10), Color.FromArgb(255, 15, 17, 18),
        Color.FromArgb(255, 23, 26, 26), Color.FromArgb(255, 32, 37, 36),
        Color.FromArgb(255, 43, 49, 46), Color.FromArgb(255, 24, 43, 42),
        Color.FromArgb(255, 32, 59, 55), Color.FromArgb(255, 43, 76, 68),
        Color.FromArgb(255, 57, 94, 80), Color.FromArgb(255, 76, 113, 92),
        Color.FromArgb(255, 100, 137, 108), Color.FromArgb(255, 129, 158, 125),
        Color.FromArgb(255, 45, 32, 27), Color.FromArgb(255, 66, 45, 31),
        Color.FromArgb(255, 89, 60, 35), Color.FromArgb(255, 116, 80, 42),
        Color.FromArgb(255, 149, 106, 53), Color.FromArgb(255, 183, 139, 69),
        Color.FromArgb(255, 218, 179, 91), Color.FromArgb(255, 51, 28, 49),
        Color.FromArgb(255, 76, 36, 70), Color.FromArgb(255, 105, 46, 91),
        Color.FromArgb(255, 139, 63, 111), Color.FromArgb(255, 102, 27, 12),
        Color.FromArgb(255, 151, 40, 10), Color.FromArgb(255, 200, 57, 8),
        Color.FromArgb(255, 238, 82, 7), Color.FromArgb(255, 255, 117, 9),
        Color.FromArgb(255, 255, 158, 18), Color.FromArgb(255, 255, 203, 48),
        Color.FromArgb(255, 255, 235, 123), Color.FromArgb(255, 255, 250, 211),
        Color.FromArgb(255, 70, 62, 65), Color.FromArgb(255, 104, 91, 94),
        Color.FromArgb(255, 141, 123, 122), Color.FromArgb(255, 179, 155, 147),
        Color.FromArgb(255, 111, 70, 105), Color.FromArgb(255, 145, 91, 137)
    };

    private static bool BackgroundCandidate(Color colour)
    {
        if (colour.A < 16) return true;
        int maximum = Math.Max(colour.R, Math.Max(colour.G, colour.B));
        int minimum = Math.Min(colour.R, Math.Min(colour.G, colour.B));
        return minimum >= 222 && maximum - minimum <= 20;
    }

    private static bool[] BuildBackground(Bitmap bitmap)
    {
        int width = bitmap.Width;
        int height = bitmap.Height;
        var candidate = new bool[width * height];
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                candidate[y * width + x] = BackgroundCandidate(bitmap.GetPixel(x, y));

        var background = new bool[candidate.Length];
        var queue = new int[candidate.Length];
        int head = 0;
        int tail = 0;
        Action<int> enqueue = delegate(int index) {
            if (candidate[index] && !background[index]) {
                background[index] = true;
                queue[tail++] = index;
            }
        };
        for (int x = 0; x < width; ++x) {
            enqueue(x);
            enqueue((height - 1) * width + x);
        }
        for (int y = 0; y < height; ++y) {
            enqueue(y * width);
            enqueue(y * width + width - 1);
        }
        while (head < tail) {
            int current = queue[head++];
            int x = current % width;
            int y = current / width;
            if (x > 0) enqueue(current - 1);
            if (x + 1 < width) enqueue(current + 1);
            if (y > 0) enqueue(current - width);
            if (y + 1 < height) enqueue(current + width);
        }
        return background;
    }

    private static int PaletteIndex(Color colour)
    {
        int best = 0;
        int bestDistance = Int32.MaxValue;
        for (int index = 0; index < Palette.Length; ++index) {
            int dr = colour.R - Palette[index].R;
            int dg = colour.G - Palette[index].G;
            int db = colour.B - Palette[index].B;
            int distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
        return best;
    }

    private sealed class ForegroundComponent
    {
        public Rectangle Bounds;
        public int Area;
        public double CentreX;
        public double CentreY;
    }

    private static Rectangle[] IsolatePoseBounds(
        Bitmap source,
        bool[] background,
        Rectangle[] slots)
    {
        int width = source.Width;
        int height = source.Height;
        var foreground = new bool[width * height];
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int index = y * width + x;
                foreground[index] = !background[index] && source.GetPixel(x, y).A >= 192;
            }
        }

        var visited = new bool[foreground.Length];
        var queue = new int[foreground.Length];
        var components = new List<ForegroundComponent>();
        for (int start = 0; start < foreground.Length; ++start) {
            if (!foreground[start] || visited[start]) continue;
            int head = 0;
            int tail = 0;
            queue[tail++] = start;
            visited[start] = true;
            int minimumX = width;
            int minimumY = height;
            int maximumX = -1;
            int maximumY = -1;
            int area = 0;
            long sumX = 0;
            long sumY = 0;
            while (head < tail) {
                int current = queue[head++];
                int x = current % width;
                int y = current / width;
                minimumX = Math.Min(minimumX, x);
                minimumY = Math.Min(minimumY, y);
                maximumX = Math.Max(maximumX, x);
                maximumY = Math.Max(maximumY, y);
                ++area;
                sumX += x;
                sumY += y;
                for (int offsetY = -1; offsetY <= 1; ++offsetY) {
                    int nextY = y + offsetY;
                    if (nextY < 0 || nextY >= height) continue;
                    for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                        if (offsetX == 0 && offsetY == 0) continue;
                        int nextX = x + offsetX;
                        if (nextX < 0 || nextX >= width) continue;
                        int next = nextY * width + nextX;
                        if (foreground[next] && !visited[next]) {
                            visited[next] = true;
                            queue[tail++] = next;
                        }
                    }
                }
            }
            if (area >= 4) {
                components.Add(new ForegroundComponent {
                    Bounds = Rectangle.FromLTRB(
                        minimumX, minimumY, maximumX + 1, maximumY + 1),
                    Area = area,
                    CentreX = (double)sumX / area,
                    CentreY = (double)sumY / area,
                });
            }
        }

        var anchors = new ForegroundComponent[slots.Length];
        var anchorSet = new HashSet<ForegroundComponent>();
        for (int frame = 0; frame < slots.Length; ++frame) {
            Rectangle slot = slots[frame];
            ForegroundComponent best = null;
            foreach (ForegroundComponent component in components) {
                if (component.CentreX < slot.Left || component.CentreX >= slot.Right ||
                    component.CentreY < slot.Top || component.CentreY >= slot.Bottom) {
                    continue;
                }
                if (best == null || component.Area > best.Area) best = component;
            }
            if (best == null || !anchorSet.Add(best)) {
                throw new InvalidOperationException(
                    "Could not isolate one foreground pose per source slot.");
            }
            anchors[frame] = best;
        }

        var result = new Rectangle[slots.Length];
        for (int frame = 0; frame < result.Length; ++frame)
            result[frame] = anchors[frame].Bounds;

        foreach (ForegroundComponent component in components) {
            if (anchorSet.Contains(component)) continue;
            int nearest = -1;
            double nearestDistance = Double.MaxValue;
            for (int frame = 0; frame < anchors.Length; ++frame) {
                double deltaX = component.CentreX - anchors[frame].CentreX;
                double deltaY = component.CentreY - anchors[frame].CentreY;
                double distance = deltaX * deltaX + deltaY * deltaY;
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearest = frame;
                }
            }
            Rectangle ownerSlot = slots[nearest];
            double maximumXDistance = ownerSlot.Width * 0.65;
            double maximumYDistance = ownerSlot.Height * 0.65;
            if (Math.Abs(component.CentreX - anchors[nearest].CentreX) <= maximumXDistance &&
                Math.Abs(component.CentreY - anchors[nearest].CentreY) <= maximumYDistance) {
                result[nearest] = Rectangle.Union(result[nearest], component.Bounds);
            }
        }

        for (int frame = 0; frame < result.Length; ++frame) {
            Rectangle padded = Rectangle.Inflate(result[frame], 2, 2);
            result[frame] = Rectangle.Intersect(
                padded, new Rectangle(0, 0, width, height));
        }
        return result;
    }

    public static Bitmap[] ImportFrames(
        string path,
        int columns,
        int rows,
        int frameCount,
        int cellSize,
        int rootY,
        double targetStandingHeight,
        bool isolatePoses)
    {
        using (var loaded = new Bitmap(path))
        using (var source = loaded.Clone(
                   new Rectangle(0, 0, loaded.Width, loaded.Height),
                   PixelFormat.Format32bppArgb)) {
            bool[] background = BuildBackground(source);
            var slots = new Rectangle[frameCount];
            var bounds = new Rectangle[frameCount];
            for (int frame = 0; frame < frameCount; ++frame) {
                int column = frame % columns;
                int row = frame / columns;
                if (row >= rows)
                    throw new InvalidOperationException(path + " does not contain the requested frame grid.");
                int left = column * source.Width / columns;
                int right = (column + 1) * source.Width / columns;
                int top = row * source.Height / rows;
                int bottom = (row + 1) * source.Height / rows;
                slots[frame] = new Rectangle(left, top, right - left, bottom - top);

                int minimumX = right;
                int minimumY = bottom;
                int maximumX = -1;
                int maximumY = -1;
                for (int y = top; y < bottom; ++y) {
                    for (int x = left; x < right; ++x) {
                        if (background[y * source.Width + x]) continue;
                        Color colour = source.GetPixel(x, y);
                        if (colour.A < 16) continue;
                        minimumX = Math.Min(minimumX, x);
                        minimumY = Math.Min(minimumY, y);
                        maximumX = Math.Max(maximumX, x);
                        maximumY = Math.Max(maximumY, y);
                    }
                }
                if (maximumX < minimumX || maximumY < minimumY)
                    throw new InvalidOperationException(path + " frame " + frame + " has no foreground.");
                bounds[frame] = Rectangle.FromLTRB(
                    minimumX, minimumY, maximumX + 1, maximumY + 1);
            }

            if (isolatePoses) {
                bounds = IsolatePoseBounds(source, background, slots);
            }

            double scale = targetStandingHeight / Math.Max(1, bounds[0].Height);
            int maximumWidth = 1;
            int maximumHeight = 1;
            foreach (Rectangle bound in bounds) {
                maximumWidth = Math.Max(maximumWidth, bound.Width);
                maximumHeight = Math.Max(maximumHeight, bound.Height);
            }
            scale = Math.Min(scale, (cellSize - 6.0) / maximumWidth);
            scale = Math.Min(scale, (rootY - 3.0) / maximumHeight);

            var frames = new Bitmap[frameCount];
            for (int frame = 0; frame < frameCount; ++frame) {
                Rectangle bound = bounds[frame];
                using (var crop = new Bitmap(bound.Width, bound.Height, PixelFormat.Format32bppArgb)) {
                    for (int y = 0; y < bound.Height; ++y) {
                        for (int x = 0; x < bound.Width; ++x) {
                            int sourceX = bound.X + x;
                            int sourceY = bound.Y + y;
                            if (background[sourceY * source.Width + sourceX]) continue;
                            Color colour = source.GetPixel(sourceX, sourceY);
                            if (colour.A >= 16) crop.SetPixel(x, y, colour);
                        }
                    }

                    var output = new Bitmap(cellSize, cellSize, PixelFormat.Format32bppArgb);
                    int width = Math.Max(1, (int)Math.Round(bound.Width * scale));
                    int height = Math.Max(1, (int)Math.Round(bound.Height * scale));
                    int destinationX = (cellSize - width) / 2;
                    int destinationY = rootY - height;
                    using (Graphics graphics = Graphics.FromImage(output)) {
                        graphics.CompositingMode = CompositingMode.SourceCopy;
                        graphics.CompositingQuality = CompositingQuality.HighSpeed;
                        graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
                        graphics.PixelOffsetMode = PixelOffsetMode.Half;
                        graphics.SmoothingMode = SmoothingMode.None;
                        graphics.DrawImage(
                            crop,
                            new Rectangle(destinationX, destinationY, width, height),
                            new Rectangle(0, 0, crop.Width, crop.Height),
                            GraphicsUnit.Pixel);
                    }
                    for (int y = 0; y < cellSize; ++y) {
                        for (int x = 0; x < cellSize; ++x) {
                            Color colour = output.GetPixel(x, y);
                            if (colour.A < 64) {
                                output.SetPixel(x, y, Color.Transparent);
                            } else {
                                output.SetPixel(x, y, Palette[PaletteIndex(colour)]);
                            }
                        }
                    }
                    frames[frame] = output;
                }
            }
            return frames;
        }
    }
}
'@ -ReferencedAssemblies System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$reviewRoot = Join-Path $projectRoot 'art\review'
$assetRoot = Join-Path $projectRoot 'game\assets\runtime'
$fixedHz = 60.0

function New-Sequence {
    param(
        [string]$Path,
        [int]$Columns,
        [int]$Rows,
        [int]$Frames,
        [int]$CellSize,
        [int]$RootY,
        [double]$StandingHeight,
        [bool]$IsolatePoses
    )
    return [FuseEnemyImporter]::ImportFrames(
        $Path, $Columns, $Rows, $Frames, $CellSize, $RootY, $StandingHeight,
        $IsolatePoses)
}

function Add-ClipTag {
    param(
        [System.Collections.Generic.List[object]]$FrameRecords,
        [System.Collections.Generic.List[object]]$Tags,
        [string]$Name,
        [object[]]$Cells,
        [int[]]$Durations,
        [bool]$FlipX = $false,
        [int[]]$EventFrames = @(),
        [string]$EventName = 'npc_step',
        [ValidateSet('loop','once')]
        [string]$LoopMode = 'loop'
    )
    $from = $FrameRecords.Count
    for ($index = 0; $index -lt $Cells.Count; $index++) {
        $cell = $Cells[$index]
        $record = [ordered]@{
            filename = "$Name-$($index + 1)"
            frame = [ordered]@{
                x = [int]$cell.X
                y = [int]$cell.Y
                w = [int]$cell.Size
                h = [int]$cell.Size
            }
            rotated = $false
            trimmed = $false
            duration = [int]$Durations[$index]
        }
        if ($FlipX) { $record.ic2d_flip_x = $true }
        if ($EventFrames -contains $index) { $record.ic2d_events = @($EventName) }
        $FrameRecords.Add($record)
    }
    $tag = [ordered]@{
        name = $Name
        from = $from
        to = $FrameRecords.Count - 1
        direction = 'forward'
    }
    if ($LoopMode -eq 'once') { $tag.ic2d_loop_mode = 'once' }
    $Tags.Add($tag)
}

function Export-FuseAtlas {
    param(
        [string]$Family,
        [int]$CellSize,
        [hashtable[]]$Definitions,
        [scriptblock]$DefineTags
    )
    $columnCount = 10
    $atlas = [System.Drawing.Bitmap]::new(
        $columnCount * $CellSize,
        $Definitions.Count * $CellSize,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $cellsByName = @{}
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($atlas)
        try {
            for ($row = 0; $row -lt $Definitions.Count; $row++) {
                $definition = $Definitions[$row]
                $frames = New-Sequence @definition
                $cells = [System.Collections.Generic.List[object]]::new()
                try {
                    for ($column = 0; $column -lt $frames.Count; $column++) {
                        $graphics.DrawImageUnscaled($frames[$column], $column * $CellSize, $row * $CellSize)
                        $cells.Add([pscustomobject]@{
                            X = $column * $CellSize
                            Y = $row * $CellSize
                            Size = $CellSize
                        })
                    }
                }
                finally {
                    foreach ($frame in $frames) { $frame.Dispose() }
                }
                $cellsByName[$definition.Name] = $cells.ToArray()
            }
        }
        finally {
            $graphics.Dispose()
        }

        $imageName = "$Family-atlas.png"
        $metadataName = "$Family-atlas.json"
        $atlas.Save((Join-Path $assetRoot $imageName), [System.Drawing.Imaging.ImageFormat]::Png)

        $frameRecords = [System.Collections.Generic.List[object]]::new()
        $tags = [System.Collections.Generic.List[object]]::new()
        & $DefineTags $frameRecords $tags $cellsByName
        $metadata = [ordered]@{
            frames = $frameRecords
            meta = [ordered]@{
                app = 'IC_2DE Fuse enemy native-grid importer'
                version = '1.0'
                image = $imageName
                format = 'RGBA8888'
                size = [ordered]@{ w = $atlas.Width; h = $atlas.Height }
                scale = '1'
                frameTags = $tags
            }
        }
        $metadata | ConvertTo-Json -Depth 12 |
            Set-Content -LiteralPath (Join-Path $assetRoot $metadataName) -Encoding utf8
        Write-Output "${Family}: atlas=$($atlas.Width)x$($atlas.Height) clips=$($tags.Count) frames=$($frameRecords.Count)"
    }
    finally {
        $atlas.Dispose()
    }
}

$stalkerRoot = Join-Path $reviewRoot 'fuse-stalker-enemy-v1'
$stalkerClean = Join-Path $stalkerRoot 'clean'
$stalkerCell = 96
$stalkerDefinitions = @(
    @{ Name='turn'; Path=(Join-Path $stalkerClean '00-fuse-stalker-turntable-8-direction.png'); Columns=4; Rows=2; Frames=8; CellSize=$stalkerCell; RootY=88; StandingHeight=68; IsolatePoses=$true },
    @{ Name='idle'; Path=(Join-Path $stalkerRoot 'idle-south-6f.png'); Columns=6; Rows=1; Frames=6; CellSize=$stalkerCell; RootY=88; StandingHeight=68; IsolatePoses=$true },
    @{ Name='walk'; Path=(Join-Path $stalkerClean 'walk-east-8f.png'); Columns=8; Rows=1; Frames=8; CellSize=$stalkerCell; RootY=88; StandingHeight=68; IsolatePoses=$true },
    @{ Name='chase'; Path=(Join-Path $stalkerClean 'chase-east-8f.png'); Columns=4; Rows=2; Frames=8; CellSize=$stalkerCell; RootY=88; StandingHeight=68; IsolatePoses=$true },
    @{ Name='hurt'; Path=(Join-Path $stalkerClean 'hurt-south-5f.png'); Columns=5; Rows=1; Frames=5; CellSize=$stalkerCell; RootY=88; StandingHeight=68; IsolatePoses=$true },
    @{ Name='death'; Path=(Join-Path $stalkerClean 'death-south-10f.png'); Columns=10; Rows=1; Frames=10; CellSize=$stalkerCell; RootY=88; StandingHeight=68; IsolatePoses=$false },
    @{ Name='explode'; Path=(Join-Path $stalkerClean 'explode-south-10f.png'); Columns=10; Rows=1; Frames=10; CellSize=$stalkerCell; RootY=88; StandingHeight=68; IsolatePoses=$false }
)

Export-FuseAtlas -Family 'fuse-stalker' -CellSize $stalkerCell -Definitions $stalkerDefinitions -DefineTags {
    param($frames, $tags, $cells)
    $turnDirections = @('south','southeast','east','northeast','north','northwest','west','southwest')
    for ($index = 0; $index -lt $turnDirections.Count; $index++) {
        if ($turnDirections[$index] -eq 'south') { continue }
        Add-ClipTag $frames $tags "fuse-stalker-idle-$($turnDirections[$index])" @($cells.turn[$index]) @(1000)
    }
    Add-ClipTag $frames $tags 'fuse-stalker-idle-south' $cells.idle @(267,133,133,133,133,267)
    Add-ClipTag $frames $tags 'fuse-stalker-move-south' $cells.chase @(83,83,83,83,83,83,83,83) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-move-north' $cells.chase @(83,83,83,83,83,83,83,83) $true @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-move-east' $cells.chase @(83,83,83,83,83,83,83,83) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-move-northeast' $cells.chase @(83,83,83,83,83,83,83,83) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-move-southeast' $cells.chase @(83,83,83,83,83,83,83,83) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-move-west' $cells.chase @(83,83,83,83,83,83,83,83) $true @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-move-northwest' $cells.chase @(83,83,83,83,83,83,83,83) $true @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-move-southwest' $cells.chase @(83,83,83,83,83,83,83,83) $true @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-walk-east' $cells.walk @(100,100,100,100,100,100,100,100) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-stalker-hurt-south' $cells.hurt @(67,67,67,67,100) -LoopMode once
    Add-ClipTag $frames $tags 'fuse-stalker-death-south' $cells.death @(83,83,83,83,83,83,100,100,133,300) -LoopMode once
    Add-ClipTag $frames $tags 'fuse-stalker-explode-south' $cells.explode @(83,83,83,83,50,50,67,83,100,200) -LoopMode once
}

$tyrantRoot = Join-Path $reviewRoot 'fuse-tyrant-boss-v1'
$tyrantClean = Join-Path $tyrantRoot 'clean'
$tyrantCell = 128
$tyrantDefinitions = @(
    @{ Name='turn'; Path=(Join-Path $tyrantClean '00-fuse-tyrant-turntable-8-direction.png'); Columns=4; Rows=2; Frames=8; CellSize=$tyrantCell; RootY=120; StandingHeight=96; IsolatePoses=$true },
    @{ Name='idle'; Path=(Join-Path $tyrantClean 'idle-south-6f.png'); Columns=6; Rows=1; Frames=6; CellSize=$tyrantCell; RootY=120; StandingHeight=96; IsolatePoses=$true },
    @{ Name='walk'; Path=(Join-Path $tyrantClean 'walk-east-8f.png'); Columns=8; Rows=1; Frames=8; CellSize=$tyrantCell; RootY=120; StandingHeight=96; IsolatePoses=$true },
    @{ Name='chase'; Path=(Join-Path $tyrantRoot 'chase-east-8f.png'); Columns=8; Rows=1; Frames=8; CellSize=$tyrantCell; RootY=120; StandingHeight=96; IsolatePoses=$true },
    @{ Name='hurt'; Path=(Join-Path $tyrantClean 'hurt-south-5f.png'); Columns=5; Rows=1; Frames=5; CellSize=$tyrantCell; RootY=120; StandingHeight=96; IsolatePoses=$true },
    @{ Name='death'; Path=(Join-Path $tyrantRoot 'death-south-10f.png'); Columns=10; Rows=1; Frames=10; CellSize=$tyrantCell; RootY=120; StandingHeight=96; IsolatePoses=$false },
    @{ Name='explode'; Path=(Join-Path $tyrantRoot 'explode-south-10f.png'); Columns=10; Rows=1; Frames=10; CellSize=$tyrantCell; RootY=120; StandingHeight=96; IsolatePoses=$false }
)

Export-FuseAtlas -Family 'fuse-tyrant' -CellSize $tyrantCell -Definitions $tyrantDefinitions -DefineTags {
    param($frames, $tags, $cells)
    $turnDirections = @('south','southeast','east','northeast','north','northwest','west','southwest')
    for ($index = 0; $index -lt $turnDirections.Count; $index++) {
        if ($turnDirections[$index] -eq 'south') { continue }
        Add-ClipTag $frames $tags "fuse-tyrant-idle-$($turnDirections[$index])" @($cells.turn[$index]) @(1000)
    }
    Add-ClipTag $frames $tags 'fuse-tyrant-idle-south' $cells.idle @(267,133,133,133,133,267)
    Add-ClipTag $frames $tags 'fuse-tyrant-move-south' $cells.chase @(100,100,100,100,100,100,100,100) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-move-north' $cells.chase @(100,100,100,100,100,100,100,100) $true @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-move-east' $cells.chase @(100,100,100,100,100,100,100,100) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-move-northeast' $cells.chase @(100,100,100,100,100,100,100,100) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-move-southeast' $cells.chase @(100,100,100,100,100,100,100,100) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-move-west' $cells.chase @(100,100,100,100,100,100,100,100) $true @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-move-northwest' $cells.chase @(100,100,100,100,100,100,100,100) $true @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-move-southwest' $cells.chase @(100,100,100,100,100,100,100,100) $true @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-walk-east' $cells.walk @(133,133,133,133,133,133,133,133) $false @(0,4)
    Add-ClipTag $frames $tags 'fuse-tyrant-hurt-south' $cells.hurt @(83,83,83,83,133) -LoopMode once
    Add-ClipTag $frames $tags 'fuse-tyrant-death-south' $cells.death @(100,100,100,100,100,100,133,133,167,400) -LoopMode once
    Add-ClipTag $frames $tags 'fuse-tyrant-explode-south' $cells.explode @(100,100,100,100,67,67,83,100,133,267) -LoopMode once
}

Write-Output 'Fuse Stalker and Fuse Tyrant runtime atlases generated.'
