$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$resDir = Join-Path $root 'res'
New-Item -ItemType Directory -Force -Path $resDir | Out-Null

function New-LogoBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    $rect = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $radius = [Math]::Max(2.0, $size * 0.22)
    $d = $radius * 2.0
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($size - $d, 0, $d, $d, 270, 90)
    $path.AddArc($size - $d, $size - $d, $d, $d, 0, 90)
    $path.AddArc(0, $size - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        $rect,
        [System.Drawing.Color]::FromArgb(255, 139, 92, 246),
        [System.Drawing.Color]::FromArgb(255, 34, 211, 238),
        45.0)
    $g.FillPath($brush, $path)

    $borderWidth = [Math]::Max(1.0, $size * 0.012)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(45, 255, 255, 255), $borderWidth)
    $g.DrawPath($pen, $path)

    $triangle = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF([float]($size * 0.355), [float]($size * 0.265))),
        (New-Object System.Drawing.PointF([float]($size * 0.355), [float]($size * 0.735))),
        (New-Object System.Drawing.PointF([float]($size * 0.745), [float]($size * 0.500)))
    )
    $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPolygon($white, $triangle)

    if ($size -ge 48) {
        $sparkle = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(150, 255, 255, 255))
        $sparkSize = $size * 0.055
        $g.FillEllipse($sparkle, $size * 0.70, $size * 0.20, $sparkSize, $sparkSize)
        $sparkle.Dispose()
    }

    $white.Dispose()
    $pen.Dispose()
    $brush.Dispose()
    $path.Dispose()
    $g.Dispose()
    return $bmp
}

function Get-IconPayload([System.Drawing.Bitmap]$bmp) {
    $w = $bmp.Width
    $h = $bmp.Height
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)

    $bw.Write([UInt32]40)
    $bw.Write([Int32]$w)
    $bw.Write([Int32]($h * 2))
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]1)
    $bw.Write([UInt32]($w * $h * 4))
    $bw.Write([Int32]0)
    $bw.Write([Int32]0)
    $bw.Write([UInt32]0)
    $bw.Write([UInt32]0)

    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $buffer = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buffer, 0, $buffer.Length)
    $bmp.UnlockBits($data)

    for ($y = $h - 1; $y -ge 0; $y--) {
        $bw.Write($buffer, $y * $stride, $w * 4)
    }
    $maskStride = [int]([Math]::Floor(($w + 31) / 32) * 4)
    $mask = New-Object byte[] ($maskStride * $h)
    $bw.Write($mask)
    $bw.Flush()
    $result = $ms.ToArray()
    $bw.Dispose()
    $ms.Dispose()
    return $result
}

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$entries = @()
foreach ($size in $sizes) {
    $bmp = New-LogoBitmap $size
    $payload = Get-IconPayload $bmp
    $entries += [pscustomobject]@{ Size = $size; Data = $payload }
    if ($size -eq 256) {
        $bmp.Save((Join-Path $resDir 'icon-preview.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    $bmp.Dispose()
}

$icoPath = Join-Path $resDir 'nebula.ico'
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($entry in $entries) {
    $dim = if ($entry.Size -ge 256) { 0 } else { $entry.Size }
    $bw.Write([byte]$dim)
    $bw.Write([byte]$dim)
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$entry.Data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $entry.Data.Length
}
foreach ($entry in $entries) {
    $bytes = [byte[]]$entry.Data
    $bw.Write($bytes, 0, $bytes.Length)
}
$bw.Flush()
$bw.Dispose()
$fs.Dispose()
Write-Host "Icono creado: $icoPath"
