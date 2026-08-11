# AY2D_EngineDemo screenshot acceptance (CM-4, 2026-08-11).
#
# The demo renders its tilemap from a 16-entry palette whose LINEAR
# colors pass through PostProcessPass gamma=2.2 encoding on the way to
# the .tga file — so the expected colors below are the gamma-encoded
# values of the palette entries actually used by the map pattern
# (floor/grass/water/sand/hazard) plus the sprite fill (255,200,60).
# Derivation: enc(c) = round(255 * (c/255)^(1/2.2)).
#
# Usage (from the demo build dir or anywhere):
#   powershell -NoProfile -ExecutionPolicy Bypass -File check_screenshot.ps1 <frame_30.tga> [<frame_60.tga> ...]
# Exit 0 = PASS (non-black + all six palette colors present ±3), else 1.
param([string[]]$Files)

function Check-Tga([string]$path) {
    $data = [System.IO.File]::ReadAllBytes($path)
    $w = [BitConverter]::ToUInt16($data, 12)
    $h = [BitConverter]::ToUInt16($data, 14)
    $bpp = $data[16]
    $bytespp = $bpp / 8
    $n = [int]$w * [int]$h
    Write-Host ("== {0} == {1}x{2} bpp={3}" -f $path, $w, $h, $bpp)
    $nonblack = 0L
    $colors = @{}
    for ($i = 0; $i -lt $n; $i++) {
        $off = 18 + $i * $bytespp
        $b = $data[$off]; $g = $data[$off + 1]; $r = $data[$off + 2]
        if ($r -gt 8 -or $g -gt 8 -or $b -gt 8) { $nonblack++ }
        $colors[("{0},{1},{2}" -f $r, $g, $b)] = 1
    }
    $want = @{
        "floor(gray)"  = "163,163,170";  # enc(95,95,105)
        "grass(green)" = "127,178,127";  # enc(55,115,55)
        "water(blue)"  = "137,203,231";  # enc(65,155,205)
        "sand(tan)"    = "231,206,142";  # enc(205,160,70)
        "hazard(org)"  = "236,184,132";  # enc(215,125,60)
        "sprite(yel)"  = "255,228,132";  # enc(255,200,60)
    }
    $missing = @()
    foreach ($w in $want.Keys) {
        $e = $want[$w].Split(",") | ForEach-Object { [int]$_ }
        $hit = $false
        foreach ($k in $colors.Keys) {
            $p = $k.Split(",") | ForEach-Object { [int]$_ }
            if ([Math]::Abs($p[0] - $e[0]) -le 3 -and [Math]::Abs($p[1] - $e[1]) -le 3 -and [Math]::Abs($p[2] - $e[2]) -le 3) {
                $hit = $true
                break
            }
        }
        if (-not $hit) { $missing += $w }
    }
    $pct = 100.0 * $nonblack / $n
    Write-Host ("nonblack={0}/{1} ({2:F1}%) distinct_colors={3}" -f $nonblack, $n, $pct, $colors.Count)
    Write-Host ("missing_palette_colors=[{0}]" -f ($missing -join ", "))
    $ok = $nonblack -gt 50000 -and $missing.Count -eq 0
    Write-Host ("VERDICT: {0}" -f ($(if ($ok) { "PASS" } else { "FAIL" })))
    return $ok
}

if ($Files.Count -eq 0) {
    Write-Host "usage: check_screenshot.ps1 <frame_30.tga> [more...]"
    exit 2
}
$allOk = $true
foreach ($f in $Files) {
    $allOk = (Check-Tga $f) -and $allOk
}
if (-not $allOk) { exit 1 }
exit 0
