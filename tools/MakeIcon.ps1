# Rebuild the multi-resolution ICO from the same geometry as assets/KeyLayoutSwitch.svg.
# Uses only Windows GDI+; no external packages or network calls.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
function New-RoundedPath([single]$X, [single]$Y, [single]$Width, [single]$Height, [single]$Radius) {
    $shape = [Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $Radius * 2
    $shape.AddArc($X,$Y,$diameter,$diameter,180,90)
    $shape.AddArc(($X+$Width-$diameter),$Y,$diameter,$diameter,270,90)
    $shape.AddArc(($X+$Width-$diameter),($Y+$Height-$diameter),$diameter,$diameter,0,90)
    $shape.AddArc($X,($Y+$Height-$diameter),$diameter,$diameter,90,90)
    $shape.CloseFigure()
    return ,$shape
}
$sizes = @(16,20,24,32,48,64,128,256)
$frames = @()
foreach ($size in $sizes) {
    $bitmap = [Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform(($size/256.0),($size/256.0))
    $blue = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#0067c0'))
    $white = [Drawing.SolidBrush]::new([Drawing.Color]::White)
    $mint = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#a7e8d8'))
    $outline = [Drawing.Pen]::new([Drawing.Color]::White,10)
    $shape = New-RoundedPath 12 12 232 232 48
    $graphics.FillPath($blue,$shape)
    $shape.Dispose()
    $shape = New-RoundedPath 44 72 168 116 16
    $graphics.DrawPath($outline,$shape)
    $shape.Dispose()
    foreach ($keyY in @(95,124)) { foreach ($keyX in @(65,100,135,170)) {
        $shape = New-RoundedPath $keyX $keyY 20 16 3
        $graphics.FillPath($white,$shape)
        $shape.Dispose()
    } }
    $shape = New-RoundedPath 92 154 72 13 4
    $graphics.FillPath($white,$shape)
    $shape.Dispose()
    $graphics.FillEllipse($mint,189,41,26,26)
    $stream = [IO.MemoryStream]::new()
    $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
    $frames += ,$stream.ToArray()
    $stream.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    $blue.Dispose(); $white.Dispose(); $mint.Dispose(); $outline.Dispose()
}
$iconPath = Join-Path $PSScriptRoot '..\assets\KeyLayoutSwitch.ico'
$iconStream = [IO.File]::Create($iconPath)
$writer = [IO.BinaryWriter]::new($iconStream)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Length)
    $offset = 6+16*$sizes.Length
    for ($index=0; $index -lt $sizes.Length; $index++) {
        $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$index].Length); $writer.Write([uint32]$offset)
        $offset += $frames[$index].Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
} finally { $writer.Dispose() }
Write-Output "Created: $iconPath"
