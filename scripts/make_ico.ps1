Add-Type -AssemblyName System.Drawing
function Make-Ico($OutFile, $Char, $ColorR, $ColorG, $ColorB) {
    $sizes = @(16, 32)
    $allPixels = @{}  # size -> pixels
    $totalImg = 0
    foreach ($s in $sizes) {
        $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.SmoothingMode = 'AntiAlias'
        $g.Clear([System.Drawing.Color]::Transparent)
        $d = [Math]::Max(4, $s / 4)
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddArc(1, 1, $d, $d, 180, 90)
        $path.AddArc($s-1-$d, 1, $d, $d, 270, 90)
        $path.AddArc($s-1-$d, $s-1-$d, $d, $d, 0, 90)
        $path.AddArc(1, $s-1-$d, $d, $d, 90, 90)
        $path.CloseFigure()
        $bg = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0, 120, 215))
        $g.FillPath($bg, $path)
        $fs = [Math]::Max(8, [int]($s * 0.62))
        $font = New-Object System.Drawing.Font('Segoe UI', $fs, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb([int]255, [int]$ColorR, [int]$ColorG, [int]$ColorB))
        $sf = New-Object System.Drawing.StringFormat; $sf.Alignment = 'Center'; $sf.LineAlignment = 'Center'
        $g.DrawString($Char, $font, $brush, (New-Object System.Drawing.RectangleF(0, 0, $s, $s)), $sf)
        $g.Dispose()
        $pixels = New-Object System.Collections.Generic.List[byte]
        for ($y = $s - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $s; $x++) {
                $c = $bmp.GetPixel($x, $y)
                $pixels.Add($c.B); $pixels.Add($c.G); $pixels.Add($c.R); $pixels.Add($c.A)
            }
        }
        $xor = $s * $s * 4; $and = $s * $s / 8; $img = 40 + $xor + $and
        $allPixels[$s] = @{px=$pixels.ToArray(); xor=$xor; and=$and; img=$img}
        $bmp.Dispose()
        $totalImg += $img
    }
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    $bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    foreach ($s in $sizes) {
        $info = $allPixels[$s]
        $bw.Write([Byte]$s); $bw.Write([Byte]$s); $bw.Write([Byte]0); $bw.Write([Byte]0)
        $bw.Write([UInt16]1); $bw.Write([UInt16]32)
        $bw.Write([UInt32]$info.img); $bw.Write([UInt32]$offset)
        $offset += $info.img
    }
    foreach ($s in $sizes) {
        $info = $allPixels[$s]
        $bw.Write([UInt32]40); $bw.Write([Int32]$s); $bw.Write([Int32]($s * 2))
        $bw.Write([UInt16]1); $bw.Write([UInt16]32); $bw.Write([UInt32]0)
        $bw.Write([UInt32]$info.xor); $bw.Write([Int32]0); $bw.Write([Int32]0)
        $bw.Write([UInt32]0); $bw.Write([UInt32]0)
        $bw.Write($info.px)
        $ms.Write(([byte[]]::new($info.and)), 0, $info.and)
    }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes($OutFile, $ms.ToArray())
    $bw.Dispose(); $ms.Dispose()
    Write-Output ("  " + $OutFile + ": " + (Get-Item $OutFile).Length + " bytes, " + $sizes.Count + " sizes")
}
Write-Output "Generating multi-size icons..."
Make-Ico 'res\app.ico'       'A'  255 255 255
Make-Ico 'res\caps_on.ico'   'A'  255 210  70
Make-Ico 'res\caps_off.ico'  'a'  235 235 235
Write-Output "Done."