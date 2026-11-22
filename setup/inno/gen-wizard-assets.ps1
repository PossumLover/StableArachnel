#Requires -Version 5.1
# Render the launcher artwork at twice the wizard's standard bitmap resolution.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$resources = Join-Path $PSScriptRoot "..\..\resources"
$outDir = Join-Path $PSScriptRoot "assets"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$logo = [Drawing.Image]::FromFile((Join-Path $resources "icons\png\256.png"))
$hills = [Drawing.Image]::FromFile((Join-Path $resources "art\hills-light.png"))
$panel = [Drawing.Bitmap]::new(328, 628)
$g = [Drawing.Graphics]::FromImage($panel)
$g.InterpolationMode = "HighQualityBicubic"
$g.Clear([Drawing.ColorTranslator]::FromHtml("#FCF8EE"))
$g.DrawImage($logo, 68, 128, 192, 192)
$format = [Drawing.StringFormat]::new()
$format.Alignment = "Center"
$font = [Drawing.Font]::new("Segoe UI Semibold", 36, [Drawing.GraphicsUnit]::Pixel)
$ink = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml("#35584B"))
$g.DrawString("Sprout", $font, $ink, [Drawing.RectangleF]::new(0, 336, 328, 56), $format)
# Crop around the glasshouse, keeping the original landscape proportions.
$g.DrawImage($hills, [Drawing.Rectangle]::new(0, 468, 328, 160),
    [Drawing.Rectangle]::new(390, 0, 615, 300), [Drawing.GraphicsUnit]::Pixel)
$panel.Save((Join-Path $outDir "wizard-image.bmp"), [Drawing.Imaging.ImageFormat]::Bmp)
$g.Dispose(); $panel.Dispose(); $hills.Dispose(); $font.Dispose(); $ink.Dispose(); $format.Dispose()

$small = [Drawing.Bitmap]::new(110, 110)
$g = [Drawing.Graphics]::FromImage($small)
$g.Clear([Drawing.Color]::White)
$g.InterpolationMode = "HighQualityBicubic"
$g.DrawImage($logo, 5, 5, 100, 100)
$small.Save((Join-Path $outDir "wizard-small.bmp"), [Drawing.Imaging.ImageFormat]::Bmp)
$g.Dispose(); $small.Dispose(); $logo.Dispose()
