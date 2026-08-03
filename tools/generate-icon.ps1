param(
    [string]$SourcePath,
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if ([string]::IsNullOrWhiteSpace($SourcePath)) {
    $repositoryRoot = Split-Path -Parent $PSScriptRoot
    $SourcePath = Join-Path $repositoryRoot "static\Ya-logo.png"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    if (-not $repositoryRoot) {
        $repositoryRoot = Split-Path -Parent $PSScriptRoot
    }
    $OutputDirectory = Join-Path $repositoryRoot "resources"
}

function New-RoundedRectanglePath([int]$Size) {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $radius = [single]($Size * 0.22)
    $diameter = $radius * 2
    $edge = [single]($Size - 1)
    $path.AddArc(0, 0, $diameter, $diameter, 180, 90)
    $path.AddArc($edge - $diameter, 0, $diameter, $diameter, 270, 90)
    $path.AddArc($edge - $diameter, $edge - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc(0, $edge - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-IconBitmap([System.Drawing.Bitmap]$Source, [int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new(
        $Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $clip = New-RoundedRectanglePath $Size
    $graphics.SetClip($clip)
    $graphics.Clear([System.Drawing.Color]::FromArgb(255, 229, 241, 233))

    $sourceRect = [System.Drawing.Rectangle]::new(
        [int]($Source.Width * 0.11),
        [int]($Source.Height * 0.02),
        [int]($Source.Width * 0.78),
        [int]($Source.Height * 0.78))
    $destinationRect = [System.Drawing.Rectangle]::new(0, 0, $Size, $Size)
    $graphics.DrawImage(
        $Source,
        $destinationRect,
        $sourceRect.X,
        $sourceRect.Y,
        $sourceRect.Width,
        $sourceRect.Height,
        [System.Drawing.GraphicsUnit]::Pixel)

    $graphics.Dispose()
    $clip.Dispose()
    return $bitmap
}

function Convert-BitmapToPngBytes([System.Drawing.Bitmap]$Bitmap) {
    $stream = [System.IO.MemoryStream]::new()
    $Bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $stream.ToArray()
    $stream.Dispose()
    return ,$bytes
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$resolvedSourcePath = (Resolve-Path -LiteralPath $SourcePath).Path
$source = [System.Drawing.Bitmap]::FromFile($resolvedSourcePath)
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = @()

foreach ($size in $sizes) {
    $bitmap = New-IconBitmap $source $size
    $images += ,(Convert-BitmapToPngBytes $bitmap)
    if ($size -eq 256) {
        $bitmap.Save(
            (Join-Path $OutputDirectory "app_icon_preview.png"),
            [System.Drawing.Imaging.ImageFormat]::Png)
    }
    $bitmap.Dispose()
}
$source.Dispose()

$iconPath = Join-Path $OutputDirectory "app.ico"
$stream = [System.IO.File]::Open($iconPath, [System.IO.FileMode]::Create)
$writer = [System.IO.BinaryWriter]::new($stream)
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$images.Count)

$offset = 6 + 16 * $images.Count
for ($index = 0; $index -lt $images.Count; $index++) {
    $size = $sizes[$index]
    $writer.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
    $writer.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$index].Length)
    $writer.Write([uint32]$offset)
    $offset += $images[$index].Length
}
foreach ($image in $images) {
    $writer.Write($image)
}
$writer.Dispose()
$stream.Dispose()

Write-Host "Generated $iconPath"
