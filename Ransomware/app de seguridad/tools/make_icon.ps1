# Convierte una imagen PNG en un icono .ico con varios tamanos.
# Uso: powershell -ExecutionPolicy Bypass -File tools\make_icon.ps1

param(
    [string]$Source = 'app.ico.png',
    [string]$Destination = 'src\app.ico'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$destinationPath = [System.IO.Path]::GetFullPath((Join-Path (Join-Path $PSScriptRoot '..') $Destination))
$destinationDirectory = [System.IO.Path]::GetDirectoryName($destinationPath)
if (-not (Test-Path -LiteralPath $destinationDirectory)) {
    New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
}

$sourceImage = [System.Drawing.Image]::FromFile($sourcePath)
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$images = New-Object System.Collections.Generic.List[byte[]]

try {
    foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

            $scale = [Math]::Min($size / $sourceImage.Width, $size / $sourceImage.Height)
            $width = [Math]::Max(1, [int][Math]::Round($sourceImage.Width * $scale))
            $height = [Math]::Max(1, [int][Math]::Round($sourceImage.Height * $scale))
            $x = [int](($size - $width) / 2)
            $y = [int](($size - $height) / 2)

            $destinationRect = New-Object System.Drawing.Rectangle $x, $y, $width, $height
            $graphics.DrawImage(
                $sourceImage,
                $destinationRect,
                0, 0, $sourceImage.Width, $sourceImage.Height,
                [System.Drawing.GraphicsUnit]::Pixel
            )
        }
        finally {
            $graphics.Dispose()
        }

        $stream = New-Object System.IO.MemoryStream
        try {
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $images.Add($stream.ToArray())
        }
        finally {
            $stream.Dispose()
            $bitmap.Dispose()
        }
    }
}
finally {
    $sourceImage.Dispose()
}

$output = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $output
try {
    $writer.Write([UInt16]0)              # reserved
    $writer.Write([UInt16]1)              # type: icon
    $writer.Write([UInt16]$images.Count)  # number of images

    $offset = 6 + (16 * $images.Count)
    for ($i = 0; $i -lt $images.Count; $i++) {
        $size = $sizes[$i]
        $data = $images[$i]
        $dimension = if ($size -ge 256) { 0 } else { $size }

        $writer.Write([byte]$dimension)   # width
        $writer.Write([byte]$dimension)   # height
        $writer.Write([byte]0)            # color count
        $writer.Write([byte]0)            # reserved
        $writer.Write([UInt16]1)          # planes
        $writer.Write([UInt16]32)         # bits per pixel
        $writer.Write([UInt32]$data.Length)
        $writer.Write([UInt32]$offset)
        $offset += $data.Length
    }

    foreach ($data in $images) {
        $writer.Write($data)
    }

    $writer.Flush()
    [System.IO.File]::WriteAllBytes($destinationPath, $output.ToArray())
}
finally {
    $writer.Dispose()
    $output.Dispose()
}

Write-Host "Icono generado: $destinationPath ($($sizes.Count) tamanos)"
