Add-Type -AssemblyName System.Drawing
foreach ($n in 'app','caps_on','caps_off') {
    $ico = New-Object System.Drawing.Icon("res\$n.ico")
    $bmp = $ico.ToBitmap()
    $sz = $bmp.Width
    Write-Output "=== $n (actual $($bmp.Width)x$($bmp.Height)) ==="
    for ($y=0; $y -lt $bmp.Height; $y++) {
        $line = ""
        for ($x=0; $x -lt $bmp.Width; $x++) {
            $c = $bmp.GetPixel($x,$y)
            if ($c.B -gt 150 -and $c.B -gt $c.R + 40) { $line += "." }
            elseif ($c.R -gt 180 -and $c.G -gt 180 -and $c.B -gt 180) { $line += "W" }
            elseif ($c.R -gt 200 -and $c.G -gt 150 -and $c.B -lt 120) { $line += "Y" }
            elseif ($c.R -gt 160 -and $c.G -gt 160) { $line += "g" }
            else { $line += "+" }
        }
        Write-Output $line
    }
    $bmp.Dispose(); $ico.Dispose()
}
