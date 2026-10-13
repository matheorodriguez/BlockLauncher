# Génère src/icon.ico à partir du même dessin que MainWindow.cpp::makeAppIcon()
# (un cube isométrique bleu), via System.Drawing — aucune dépendance externe.
Add-Type -AssemblyName System.Drawing

function New-CubeBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $s = $size / 128.0
    $pt = { param($x, $y) New-Object System.Drawing.PointF ($x * $s), ($y * $s) }

    $top   = & $pt 64 14
    $left  = & $pt 14 40
    $right = & $pt 114 40
    $mid   = & $pt 64 66
    $bl    = & $pt 14 92
    $br    = & $pt 114 92
    $bot   = & $pt 64 118

    $brush1 = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml("#9be3ff"))
    $brush2 = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml("#3aa0d8"))
    $brush3 = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml("#1d6fa5"))

    $g.FillPolygon($brush1, @($top, $right, $mid, $left))
    $g.FillPolygon($brush2, @($left, $mid, $bot, $bl))
    $g.FillPolygon($brush3, @($mid, $right, $br, $bot))

    $g.Dispose()
    return $bmp
}

$sizes = @(16, 32, 48, 256)
$bitmaps = $sizes | ForEach-Object { New-CubeBitmap $_ }

$outDir = "C:\Users\mathe\Desktop\BlockLauncher\BlockLauncher\src"
$icoPath = Join-Path $outDir "icon.ico"

# Écrit un .ico multi-résolution (PNG-compressé pour la frame 256, BMP pour les autres),
# format ICONDIR standard supporté par l'Explorateur Windows.
$fs = [System.IO.File]::Open($icoPath, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter $fs

$bw.Write([UInt16]0)      # reserved
$bw.Write([UInt16]1)      # type = icon
$bw.Write([UInt16]$bitmaps.Count)

$imageDatas = @()
foreach ($bmp in $bitmaps) {
    $ms = New-Object System.IO.MemoryStream
    if ($bmp.Width -ge 256) {
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    } else {
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    $imageDatas += ,($ms.ToArray())
}

$offset = 6 + (16 * $bitmaps.Count)
for ($i = 0; $i -lt $bitmaps.Count; $i++) {
    $w = $bitmaps[$i].Width
    $h = $bitmaps[$i].Height
    $bw.Write([byte]($(if ($w -ge 256) { 0 } else { $w })))
    $bw.Write([byte]($(if ($h -ge 256) { 0 } else { $h })))
    $bw.Write([byte]0)   # palette
    $bw.Write([byte]0)   # reserved
    $bw.Write([UInt16]1) # color planes
    $bw.Write([UInt16]32) # bits per pixel
    $bw.Write([UInt32]$imageDatas[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $imageDatas[$i].Length
}
foreach ($data in $imageDatas) { $bw.Write($data) }

$bw.Flush()
$bw.Close()
$fs.Close()
foreach ($bmp in $bitmaps) { $bmp.Dispose() }

Write-Host "Icone generee : $icoPath"
