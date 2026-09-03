[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public sealed class PlayerV3ImportResult : IDisposable
{
    public Bitmap Atlas { get; set; }
    public int SourceBaseline { get; set; }
    public int VisiblePixels { get; set; }

    public void Dispose()
    {
        if (Atlas != null)
        {
            Atlas.Dispose();
            Atlas = null;
        }
    }
}

public static class PlayerV3Importer
{
    private const int CellSize = 48;
    private const int RootX = 24;
    private const int RootY = 40;
    private const int MinimumComponentArea = 48;

    // Deliberately small shared palette: outline, beanie, skin, patchwork cloth,
    // leather/brass, muzzle warmth, and two pale eye/highlight ramps.
    private static readonly Color[] Palette = new Color[] {
        Color.FromArgb(255, 13, 10, 18), Color.FromArgb(255, 24, 17, 30),
        Color.FromArgb(255, 40, 18, 49), Color.FromArgb(255, 59, 22, 72),
        Color.FromArgb(255, 82, 28, 99), Color.FromArgb(255, 111, 38, 132),
        Color.FromArgb(255, 148, 56, 174), Color.FromArgb(255, 193, 91, 216),
        Color.FromArgb(255, 226, 151, 239),
        Color.FromArgb(255, 12, 45, 43), Color.FromArgb(255, 17, 63, 57),
        Color.FromArgb(255, 24, 84, 72), Color.FromArgb(255, 31, 108, 84),
        Color.FromArgb(255, 47, 137, 91), Color.FromArgb(255, 75, 166, 87),
        Color.FromArgb(255, 112, 193, 78), Color.FromArgb(255, 158, 215, 75),
        Color.FromArgb(255, 80, 66, 18), Color.FromArgb(255, 126, 100, 18),
        Color.FromArgb(255, 179, 143, 18), Color.FromArgb(255, 222, 187, 27),
        Color.FromArgb(255, 245, 220, 43), Color.FromArgb(255, 255, 240, 83),
        Color.FromArgb(255, 48, 37, 25), Color.FromArgb(255, 83, 59, 29),
        Color.FromArgb(255, 126, 88, 34), Color.FromArgb(255, 177, 129, 44),
        Color.FromArgb(255, 216, 174, 61),
        Color.FromArgb(255, 171, 55, 49), Color.FromArgb(255, 222, 83, 48),
        Color.FromArgb(255, 255, 126, 23), Color.FromArgb(255, 255, 190, 31),
        Color.FromArgb(255, 255, 239, 166), Color.FromArgb(255, 248, 246, 223),
        Color.FromArgb(255, 255, 255, 255)
    };

    private sealed class SourceData : IDisposable
    {
        public Bitmap Bitmap;
        public byte[] Pixels;
        public bool[] Kept;
        public int Width;
        public int Height;
        public int[] FrameBottoms;
        public int[] FrameHeights;
        public int[] FrameStarts;
        public int[] FrameEnds;
        public double[] FrameCentres;

        public void Dispose()
        {
            if (Bitmap != null)
            {
                Bitmap.Dispose();
                Bitmap = null;
            }
        }
    }

    private static bool BackgroundCandidate(byte r, byte g, byte b)
    {
        return r <= 58 && g <= 126 && b <= 130 &&
               g >= r + 6 && b >= r + 6 && Math.Abs(g - b) <= 30;
    }

    private static SourceData Load(string path, int frameCount)
    {
        using (var input = new Bitmap(path))
        {
            var bitmap = input.Clone(
                new Rectangle(0, 0, input.Width, input.Height),
                PixelFormat.Format32bppArgb);
            int width = bitmap.Width;
            int height = bitmap.Height;
            var rectangle = new Rectangle(0, 0, width, height);
            BitmapData locked = bitmap.LockBits(
                rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            byte[] pixels = new byte[Math.Abs(locked.Stride) * height];
            Marshal.Copy(locked.Scan0, pixels, 0, pixels.Length);
            bitmap.UnlockBits(locked);

            bool[] candidate = new bool[width * height];
            for (int y = 0; y < height; ++y)
            {
                int row = y * locked.Stride;
                for (int x = 0; x < width; ++x)
                {
                    int pixel = row + x * 4;
                    candidate[y * width + x] = pixels[pixel + 3] < 16 ||
                        BackgroundCandidate(
                            pixels[pixel + 2], pixels[pixel + 1], pixels[pixel]);
                }
            }

            bool[] background = new bool[candidate.Length];
            int[] queue = new int[candidate.Length];
            int head = 0;
            int tail = 0;
            Action<int> enqueue = delegate(int index) {
                if (candidate[index] && !background[index])
                {
                    background[index] = true;
                    queue[tail++] = index;
                }
            };
            for (int x = 0; x < width; ++x)
            {
                enqueue(x);
                enqueue((height - 1) * width + x);
            }
            for (int y = 0; y < height; ++y)
            {
                enqueue(y * width);
                enqueue(y * width + width - 1);
            }
            while (head < tail)
            {
                int current = queue[head++];
                int x = current % width;
                int y = current / width;
                if (x > 0) enqueue(current - 1);
                if (x + 1 < width) enqueue(current + 1);
                if (y > 0) enqueue(current - width);
                if (y + 1 < height) enqueue(current + width);
            }

            var components = new List<List<int>>();
            bool[] visited = new bool[background.Length];
            for (int seed = 0; seed < background.Length; ++seed)
            {
                if (background[seed] || visited[seed]) continue;
                head = 0;
                tail = 0;
                queue[tail++] = seed;
                visited[seed] = true;
                var component = new List<int>();
                while (head < tail)
                {
                    int current = queue[head++];
                    component.Add(current);
                    int x = current % width;
                    int y = current / width;
                    for (int offsetY = -1; offsetY <= 1; ++offsetY)
                    {
                        int neighbourY = y + offsetY;
                        if (neighbourY < 0 || neighbourY >= height) continue;
                        for (int offsetX = -1; offsetX <= 1; ++offsetX)
                        {
                            int neighbourX = x + offsetX;
                            if ((offsetX == 0 && offsetY == 0) ||
                                neighbourX < 0 || neighbourX >= width) continue;
                            int neighbour = neighbourY * width + neighbourX;
                            if (!background[neighbour] && !visited[neighbour])
                            {
                                visited[neighbour] = true;
                                queue[tail++] = neighbour;
                            }
                        }
                    }
                }
                if (component.Count >= MinimumComponentArea) components.Add(component);
            }

            if (components.Count < frameCount)
                throw new InvalidOperationException(path + " contains only " + components.Count +
                                                    " usable foreground components.");

            components.Sort((left, right) => right.Count.CompareTo(left.Count));
            var mainComponents = components.GetRange(0, frameCount);
            mainComponents.Sort((left, right) => {
                double leftCentre = 0.0;
                foreach (int point in left) leftCentre += point % width;
                double rightCentre = 0.0;
                foreach (int point in right) rightCentre += point % width;
                return (leftCentre / left.Count).CompareTo(rightCentre / right.Count);
            });

            double[] frameCentres = new double[frameCount];
            for (int frame = 0; frame < frameCount; ++frame)
            {
                double centre = 0.0;
                foreach (int point in mainComponents[frame]) centre += point % width;
                frameCentres[frame] = centre / mainComponents[frame].Count;
            }
            int[] frameStarts = new int[frameCount];
            int[] frameEnds = new int[frameCount];
            for (int frame = 0; frame < frameCount; ++frame)
            {
                frameStarts[frame] = frame == 0
                    ? 0
                    : (int)Math.Floor((frameCentres[frame - 1] + frameCentres[frame]) * 0.5);
                frameEnds[frame] = frame + 1 == frameCount
                    ? width
                    : (int)Math.Floor((frameCentres[frame] + frameCentres[frame + 1]) * 0.5);
            }

            bool[] kept = new bool[background.Length];
            int[] frameBottoms = new int[frameCount];
            int[] frameHeights = new int[frameCount];
            foreach (List<int> component in components)
            {
                double componentCentre = 0.0;
                foreach (int point in component) componentCentre += point % width;
                componentCentre /= component.Count;
                int owner = 0;
                while (owner + 1 < frameCount && componentCentre >= frameEnds[owner]) ++owner;
                foreach (int point in component) kept[point] = true;
            }
            for (int frame = 0; frame < frameCount; ++frame)
            {
                int minimumY = height;
                int maximumY = -1;
                for (int y = 0; y < height; ++y)
                {
                    for (int x = frameStarts[frame]; x < frameEnds[frame]; ++x)
                    {
                        if (!kept[y * width + x]) continue;
                        minimumY = Math.Min(minimumY, y);
                        maximumY = Math.Max(maximumY, y);
                    }
                }
                if (maximumY < 0)
                    throw new InvalidOperationException(path + " frame " + frame + " has no retained sprite pixels.");
                frameBottoms[frame] = maximumY;
                frameHeights[frame] = maximumY - minimumY + 1;
            }

            return new SourceData {
                Bitmap = bitmap,
                Pixels = pixels,
                Kept = kept,
                Width = width,
                Height = height,
                FrameBottoms = frameBottoms,
                FrameHeights = frameHeights,
                FrameStarts = frameStarts,
                FrameEnds = frameEnds,
                FrameCentres = frameCentres
            };
        }
    }

    private static int Median(int[] values)
    {
        int[] sorted = (int[])values.Clone();
        Array.Sort(sorted);
        return sorted[sorted.Length / 2];
    }

    public static int MeasureStandingHeight(string path, int frameCount)
    {
        using (SourceData source = Load(path, frameCount))
            return Median(source.FrameHeights);
    }

    public static int MeasureFrameHeight(string path, int frameCount, int frameIndex)
    {
        using (SourceData source = Load(path, frameCount))
            return source.FrameHeights[frameIndex];
    }

    public static double MeasureMaximumScale(
        string path,
        int frameCount,
        bool suppressBelowRootEffects)
    {
        using (SourceData source = Load(path, frameCount))
        {
            int baseline = Median(source.FrameBottoms);
            double maximumLeft = 1.0;
            double maximumRight = 1.0;
            double maximumTop = 1.0;
            double maximumBottom = 1.0;
            for (int frame = 0; frame < frameCount; ++frame)
            {
                for (int y = 0; y < source.Height; ++y)
                {
                    if (suppressBelowRootEffects && y > baseline + 24) continue;
                    for (int x = source.FrameStarts[frame]; x < source.FrameEnds[frame]; ++x)
                    {
                        if (!source.Kept[y * source.Width + x]) continue;
                        maximumLeft = Math.Max(maximumLeft, source.FrameCentres[frame] - x);
                        maximumRight = Math.Max(maximumRight, x - source.FrameCentres[frame]);
                        maximumTop = Math.Max(maximumTop, baseline - y);
                        maximumBottom = Math.Max(maximumBottom, y - baseline);
                    }
                }
            }
            const int padding = 1;
            return Math.Min(
                Math.Min((RootX - padding) / maximumLeft,
                         (CellSize - 1 - padding - RootX) / maximumRight),
                Math.Min((RootY - padding) / maximumTop,
                         (CellSize - 1 - padding - RootY) / maximumBottom));
        }
    }

    private static int PaletteIndex(byte r, byte g, byte b)
    {
        int best = 0;
        int bestDistance = Int32.MaxValue;
        for (int index = 0; index < Palette.Length; ++index)
        {
            int dr = r - Palette[index].R;
            int dg = g - Palette[index].G;
            int db = b - Palette[index].B;
            int distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = index;
            }
        }
        return best;
    }

    public static PlayerV3ImportResult Import(
        string path,
        int frameCount,
        double scale,
        bool suppressBelowRootEffects)
    {
        using (SourceData source = Load(path, frameCount))
        {
            int baseline = Median(source.FrameBottoms);
            var atlas = new Bitmap(CellSize * frameCount, CellSize, PixelFormat.Format32bppArgb);
            int visiblePixels = 0;
            for (int frame = 0; frame < frameCount; ++frame)
            {
                double centreX = source.FrameCentres[frame];
                int frameStart = source.FrameStarts[frame];
                int frameEnd = source.FrameEnds[frame];
                for (int targetY = 0; targetY < CellSize; ++targetY)
                {
                    double sourceTop = baseline + (targetY - RootY - 0.5) / scale;
                    double sourceBottom = baseline + (targetY - RootY + 0.5) / scale;
                    int sampleTop = Math.Max(0, (int)Math.Floor(sourceTop));
                    int sampleBottom = Math.Min(source.Height - 1, (int)Math.Ceiling(sourceBottom));
                    for (int targetX = 0; targetX < CellSize; ++targetX)
                    {
                        double sourceLeft = centreX + (targetX - RootX - 0.5) / scale;
                        double sourceRight = centreX + (targetX - RootX + 0.5) / scale;
                        int sampleLeft = Math.Max(frameStart, (int)Math.Floor(sourceLeft));
                        int sampleRight = Math.Min(frameEnd - 1, (int)Math.Ceiling(sourceRight));
                        int samples = 0;
                        int foreground = 0;
                        int[] paletteCounts = new int[Palette.Length];
                        for (int y = sampleTop; y <= sampleBottom; ++y)
                        {
                            int pixelRow = y * source.Width;
                            for (int x = sampleLeft; x <= sampleRight; ++x)
                            {
                                ++samples;
                                if (!source.Kept[pixelRow + x] ||
                                    (suppressBelowRootEffects && y > baseline + 24)) continue;
                                ++foreground;
                                int pixel = (y * source.Width + x) * 4;
                                int palette = PaletteIndex(
                                    source.Pixels[pixel + 2],
                                    source.Pixels[pixel + 1],
                                    source.Pixels[pixel]);
                                ++paletteCounts[palette];
                            }
                        }
                        if (samples == 0 || foreground * 10 < samples) continue;
                        int selected = 0;
                        for (int palette = 1; palette < paletteCounts.Length; ++palette)
                            if (paletteCounts[palette] > paletteCounts[selected]) selected = palette;
                        atlas.SetPixel(frame * CellSize + targetX, targetY, Palette[selected]);
                        ++visiblePixels;
                    }
                }
            }
            return new PlayerV3ImportResult {
                Atlas = atlas,
                SourceBaseline = baseline,
                VisiblePixels = visiblePixels
            };
        }
    }
}
'@ -ReferencedAssemblies System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$reviewRoot = Join-Path $projectRoot 'art\review\main-character-v3-motion-study'
$v2SourceRoot = Join-Path $projectRoot 'art\production\main-character-v2\strips'
$assetRoot = Join-Path $projectRoot 'game\assets\runtime'
$fixedHz = 60.0
$cellSize = 48

$definitions = @(
    @{ SourceRoot = 'v2'; Source = 'player-v2-walk-south.png'; Output = 'player-v3-move-south'; Frames = 8; Tag = 'player-v3-move-south'; Kind = 'move'; Ticks = @(7,5,6,6,7,5,6,6) },
    @{ SourceRoot = 'v2'; Source = 'player-v2-walk-north.png'; Output = 'player-v3-move-north'; Frames = 8; Tag = 'player-v3-move-north'; Kind = 'move'; Ticks = @(7,5,6,6,7,5,6,6) },
    @{ SourceRoot = 'v2'; Source = 'player-v2-walk-east.png'; Output = 'player-v3-move-east'; Frames = 8; Tag = 'player-v3-move-east'; MirrorTag = 'player-v3-move-west'; Kind = 'move'; Ticks = @(7,5,6,6,7,5,6,6) },
    @{ SourceRoot = 'v2'; Source = 'player-v2-walk-southeast.png'; Output = 'player-v3-move-southeast'; Frames = 8; Tag = 'player-v3-move-southeast'; MirrorTag = 'player-v3-move-southwest'; Kind = 'move'; Ticks = @(7,5,6,6,7,5,6,6) },
    @{ SourceRoot = 'v2'; Source = 'player-v2-walk-northeast.png'; Output = 'player-v3-move-northeast'; Frames = 8; Tag = 'player-v3-move-northeast'; MirrorTag = 'player-v3-move-northwest'; Kind = 'move'; Ticks = @(7,5,6,6,7,5,6,6) },
    @{ SourceRoot = 'v2'; Source = 'player-v2-dodge-north.png'; Output = 'player-v3-dodge-north'; Frames = 8; Tag = 'player-v3-dodge-north'; Kind = 'dodge'; Ticks = @(1,1,2,2,2,2,1,1) },
    @{ Source = 'idle-south-6f.png'; Output = 'player-v3-idle-south'; Frames = 6; Tag = 'player-v3-idle-south'; Kind = 'idle'; Ticks = @(20,10,8,8,10,16) },
    @{ Source = 'idle-southwest-6f.png'; Output = 'player-v3-idle-southwest'; Frames = 6; Tag = 'player-v3-idle-southwest'; Kind = 'idle'; Ticks = @(20,10,8,8,10,16) },
    @{ Source = 'idle-west-6f.png'; Output = 'player-v3-idle-west'; Frames = 6; Tag = 'player-v3-idle-west'; Kind = 'idle'; Ticks = @(20,10,8,8,10,16) },
    @{ Source = 'idle-northwest-6f.png'; Output = 'player-v3-idle-northwest'; Frames = 6; Tag = 'player-v3-idle-northwest'; Kind = 'idle'; Ticks = @(20,10,8,8,10,16) },
    @{ Source = 'idle-north-6f.png'; Output = 'player-v3-idle-north'; Frames = 6; Tag = 'player-v3-idle-north'; Kind = 'idle'; Ticks = @(20,10,8,8,10,16) },
    @{ Source = 'idle-northeast-6f.png'; Output = 'player-v3-idle-northeast'; Frames = 6; Tag = 'player-v3-idle-northeast'; Kind = 'idle'; Ticks = @(20,10,8,8,10,16) },
    @{ Source = 'idle-east-6f.png'; Output = 'player-v3-idle-east'; Frames = 6; Tag = 'player-v3-idle-east'; Kind = 'idle'; Ticks = @(20,10,8,8,10,16) },
    @{ Source = 'idle-southeast-6f.png'; Output = 'player-v3-idle-southeast'; Frames = 6; Tag = 'player-v3-idle-southeast'; Kind = 'idle'; Ticks = @(20,10,8,8,10,16) },
    @{ Source = 'seated-idle-south-6f.png'; Output = 'player-v3-seated-south'; Frames = 6; Tag = 'player-v3-seated-south'; Kind = 'seated'; Ticks = @(24,12,12,12,16,20) },
    @{ Source = 'seated-idle-north-6f.png'; Output = 'player-v3-seated-north'; Frames = 6; Tag = 'player-v3-seated-north'; Kind = 'seated'; Ticks = @(24,12,12,12,16,20) },
    @{ Source = 'dodge-quick-sidestep-east-6f.png'; Output = 'player-v3-dodge-sidestep'; Frames = 6; Tag = 'player-v3-dodge-sidestep-east'; MirrorTag = 'player-v3-dodge-sidestep-west'; Kind = 'dodge'; Ticks = @(1,1,2,3,3,2) },
    @{ Source = 'dodge-shoulder-roll-east-8f.png'; Output = 'player-v3-dodge-roll'; Frames = 8; Tag = 'player-v3-dodge-roll-east'; MirrorTag = 'player-v3-dodge-roll-west'; Kind = 'dodge'; Ticks = @(1,1,2,2,2,2,1,1) },
    @{ Source = 'dodge-low-slide-east-8f.png'; Output = 'player-v3-dodge-slide'; Frames = 8; Tag = 'player-v3-dodge-slide-east'; MirrorTag = 'player-v3-dodge-slide-west'; Kind = 'dodge'; Ticks = @(1,1,1,2,3,2,1,1) },
    @{ Source = 'dodge-back-hop-south-7f.png'; Output = 'player-v3-dodge-back-hop-south'; Frames = 7; Tag = 'player-v3-dodge-back-hop-south'; Kind = 'dodge'; Ticks = @(1,1,2,3,2,2,1) },
    @{ Source = 'shoot-south-6f.png'; Output = 'player-v3-shoot-south'; Frames = 6; Tag = 'player-v3-shoot-south'; Kind = 'shoot'; Ticks = @(1,1,1,2,2,2) },
    @{ Source = 'shoot-north-6f.png'; Output = 'player-v3-shoot-north'; Frames = 6; Tag = 'player-v3-shoot-north'; Kind = 'shoot'; Ticks = @(1,1,1,2,2,2) },
    @{ Source = 'shoot-east-6f.png'; Output = 'player-v3-shoot-east'; Frames = 6; Tag = 'player-v3-shoot-east'; MirrorTag = 'player-v3-shoot-west'; Kind = 'shoot'; Ticks = @(1,1,1,2,2,2) }
)

foreach ($definition in $definitions) {
    $sourceRoot = if ($definition.ContainsKey('SourceRoot') -and
                      $definition.SourceRoot -eq 'v2') { $v2SourceRoot } else { $reviewRoot }
    $sourcePath = Join-Path $sourceRoot $definition.Source
    $targetHeight = if ($definition.Kind -eq 'seated') { 30.0 } else { 42.0 }
    $referenceHeight = if ($definition.Kind -eq 'dodge') {
        [PlayerV3Importer]::MeasureFrameHeight(
            $sourcePath, [int]$definition.Frames, 0)
    } else {
        [PlayerV3Importer]::MeasureStandingHeight(
            $sourcePath, [int]$definition.Frames)
    }
    $suppressBelowRootEffects = $definition.Kind -eq 'shoot'
    $desiredScale = $targetHeight / [double]$referenceHeight
    $maximumScale = [PlayerV3Importer]::MeasureMaximumScale(
        $sourcePath, [int]$definition.Frames, $suppressBelowRootEffects)
    $scale = [Math]::Min($desiredScale, $maximumScale)
    $result = [PlayerV3Importer]::Import(
        $sourcePath, [int]$definition.Frames, $scale, $suppressBelowRootEffects)
    try {
        $imageName = "$($definition.Output).png"
        $imagePath = Join-Path $assetRoot $imageName
        $result.Atlas.Save($imagePath, [System.Drawing.Imaging.ImageFormat]::Png)

        $frames = [System.Collections.Generic.List[object]]::new()
        for ($frameIndex = 0; $frameIndex -lt [int]$definition.Frames; $frameIndex++) {
            $events = @()
            if ($definition.Kind -eq 'dodge' -and $frameIndex -eq 0) {
                $events = @('dodge_launch')
            }
            elseif ($definition.Kind -eq 'dodge' -and
                    $frameIndex -eq ([int]$definition.Frames - 2)) {
                $events = @('dodge_recover')
            }
            elseif ($definition.Kind -eq 'shoot' -and $frameIndex -eq 0) {
                $events = @('shot_fired')
            }
            elseif ($definition.Kind -eq 'shoot' -and $frameIndex -eq 3) {
                $events = @('shot_recover')
            }
            elseif ($definition.Kind -eq 'move' -and
                    ($frameIndex -eq 0 -or $frameIndex -eq 4)) {
                $events = @('footstep')
            }
            $frame = [ordered]@{
                filename = "$($definition.Output)-$($frameIndex + 1)"
                frame = [ordered]@{ x = $frameIndex * $cellSize; y = 0; w = $cellSize; h = $cellSize }
                rotated = $false
                trimmed = $false
                duration = [int][Math]::Round([int]$definition.Ticks[$frameIndex] * 1000.0 / $fixedHz)
            }
            if ($events.Count -gt 0) { $frame.ic2d_events = $events }
            $frames.Add($frame)
        }

        $tags = [System.Collections.Generic.List[object]]::new()
        $tags.Add([ordered]@{
            name = $definition.Tag
            from = 0
            to = [int]$definition.Frames - 1
            direction = 'forward'
        })
        if ($definition.ContainsKey('MirrorTag')) {
            $originalCount = $frames.Count
            for ($frameIndex = 0; $frameIndex -lt [int]$definition.Frames; $frameIndex++) {
                $mirrored = [ordered]@{
                    filename = "$($definition.Output)-mirror-$($frameIndex + 1)"
                    frame = [ordered]@{ x = $frameIndex * $cellSize; y = 0; w = $cellSize; h = $cellSize }
                    rotated = $false
                    trimmed = $false
                    duration = [int][Math]::Round([int]$definition.Ticks[$frameIndex] * 1000.0 / $fixedHz)
                    ic2d_flip_x = $true
                }
                if ($frames[$frameIndex].Contains('ic2d_events')) {
                    $mirrored.ic2d_events = $frames[$frameIndex].ic2d_events
                }
                $frames.Add($mirrored)
            }
            $tags.Add([ordered]@{
                name = $definition.MirrorTag
                from = $originalCount
                to = $frames.Count - 1
                direction = 'forward'
            })
        }

        $loopDirection = if ($definition.Kind -eq 'idle' -or $definition.Kind -eq 'seated') {
            'forward'
        } else {
            'forward'
        }
        $metadata = [ordered]@{
            frames = $frames
            meta = [ordered]@{
                app = 'IC_2DE player V3 native-grid importer'
                version = '1.0'
                image = $imageName
                format = 'RGBA8888'
                size = [ordered]@{ w = $result.Atlas.Width; h = $result.Atlas.Height }
                scale = '1'
                frameTags = $tags
            }
        }
        $metadata | ConvertTo-Json -Depth 12 |
            Set-Content -LiteralPath (Join-Path $assetRoot "$($definition.Output).json") -Encoding utf8
        Write-Output ("{0}: frames={1} atlas={2}x{3} source-height={4} target-height={5} baseline={6} visible={7}" -f
            $definition.Output, $definition.Frames, $result.Atlas.Width,
            $result.Atlas.Height, $referenceHeight, $targetHeight,
            $result.SourceBaseline, $result.VisiblePixels)
    }
    finally {
        $result.Dispose()
    }
}

Write-Output 'Player V3 runtime atlases and metadata generated.'
