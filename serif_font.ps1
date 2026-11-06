# Shared by build_ios.ps1 / build_android.ps1: pm_assets.h, with the Latin UI font chosen by -Serif.
#   Local:  a font of your own, named in assets/fonts/serif.local.txt (not in the repo): three lines, the Regular
#           file, the Bold file, the font's display name. For your own builds; mind its license before sharing.
#           Without that file (or its fonts): Source Serif 4.
#   Source: SourceSerif4-Regular.ttf / -Bold.ttf (SIL OFL 1.1): the builds that are shared.
function pmSerifFiles([string]$fontsDir, [string]$serif) {
  $source = @("SourceSerif4-Regular.ttf", "SourceSerif4-Bold.ttf", "Source Serif 4")
  if ($serif -ne "Local") { return $source }
  $cfg = "$fontsDir/serif.local.txt"
  if (-not (Test-Path $cfg)) { return $source }
  $l = @(Get-Content -Encoding UTF8 $cfg | ForEach-Object { $_.Trim() } | Where-Object { $_ })
  if ($l.Count -lt 3 -or -not (Test-Path "$fontsDir/$($l[0])") -or -not (Test-Path "$fontsDir/$($l[1])")) {
    Write-Host "serif.local.txt incomplete or its fonts missing: building with Source Serif 4"
    return $source
  }
  return @($l[0], $l[1], $l[2])
}
function pmAssetsHeader([string]$fontsDir, [string]$serif) {
  $f = pmSerifFiles $fontsDir $serif
  Write-Host "serif font: $($f[2])"
  return "#pragma once`n#define PM_FONTS_DIR `"$fontsDir`"`n#define PM_SERIF_R `"$($f[0])`"`n#define PM_SERIF_B `"$($f[1])`"`n#define PM_SERIF_NAME `"$($f[2])`"`n"
}
