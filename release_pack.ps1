# Packages one PhiMe version twice, into <repo parent>\PhiMe-v<version>\:
#   share-SourceSerif4\   Source Serif 4 (SIL OFL): the one to hand out
#   private-<font>\       your own font from assets/fonts/serif.local.txt (see serif_font.ps1): keep it to yourself
#                         unless its license allows sharing (skipped when there is no such font)
# each with Phigros-PhiMe-v<ver>-<tag>.ipa / .apk and PhiMeMod-v<ver>-<tag>.dylib / libPhiMeMod-v<ver>-<tag>.so.
# The private set is built last, so mod\out ends up with your font (the everyday builds).
#   powershell -ExecutionPolicy Bypass -File mod\release_pack.ps1 [-Ipa x.ipa] [-Apk x.apk] [-OutDir dir]
param(
  [string]$Ipa = "",
  [string]$Apk = "",
  [string]$OutDir = ""
)
$ErrorActionPreference = "Stop"
$modRoot = $PSScriptRoot
$top = Split-Path $modRoot
if (-not $Ipa) { $Ipa = Join-Path $top "games.Pigeon.Phigros-4.0.1.2.ipa" }
if (-not $Apk) { $Apk = Join-Path $top "com.PigeonGames.Phigros.apk" }
$modH = [IO.File]::ReadAllText((Join-Path $modRoot "src\mod.h"))
if ($modH -notmatch '#define PM_VERSION "([^"]+)"') { throw "PM_VERSION not found in mod.h" }
$ver = $matches[1]
if (-not $OutDir) { $OutDir = Join-Path $top "PhiMe-v$ver" }

. (Join-Path $modRoot "serif_font.ps1")
$fontsDir = (Join-Path $modRoot "assets\fonts").Replace('\', '/')
$local = pmSerifFiles $fontsDir "Local"
$kinds = @(@{ Serif = "Source"; Tag = "share"; Dir = "share-SourceSerif4" })
if ($local[0] -ne "SourceSerif4-Regular.ttf") {
  $name = ($local[2] -replace '[^A-Za-z0-9]', '')
  $kinds += @{ Serif = "Local"; Tag = "private-$name"; Dir = "private-$name" }
}
foreach ($k in $kinds) {
  $dir = Join-Path $OutDir $k.Dir
  New-Item -ItemType Directory -Force $dir | Out-Null
  Write-Host "=== $($k.Dir) ==="
  $ipaOut = Join-Path $dir "Phigros-PhiMe-v$ver-$($k.Tag).ipa"
  & powershell -ExecutionPolicy Bypass -File (Join-Path $modRoot "ios\package_ipa.ps1") -Ipa $Ipa -Out $ipaOut -Serif $k.Serif
  if ($LASTEXITCODE -ne 0) { throw "ipa ($($k.Serif)) failed" }
  Copy-Item (Join-Path $modRoot "out\PhiMeMod.dylib") (Join-Path $dir "PhiMeMod-v$ver-$($k.Tag).dylib") -Force
  $apkOut = Join-Path $dir "Phigros-PhiMe-v$ver-$($k.Tag).apk"
  & powershell -ExecutionPolicy Bypass -File (Join-Path $modRoot "android\repack.ps1") -Apk $Apk -Out $apkOut -Serif $k.Serif
  if ($LASTEXITCODE -ne 0) { throw "apk ($($k.Serif)) failed" }
  Copy-Item (Join-Path $modRoot "out\android\libPhiMeMod.so") (Join-Path $dir "libPhiMeMod-v$ver-$($k.Tag).so") -Force
}
# the install guide of the last packaged version, in every folder
$guide = Get-ChildItem $top -Directory -Filter "PhiMe-v*" | Where-Object { $_.FullName -ne $OutDir } |
  Sort-Object LastWriteTime | ForEach-Object { Get-ChildItem $_.FullName -Filter *.txt } | Select-Object -Last 1
if ($guide) { foreach ($k in $kinds) { Copy-Item $guide.FullName (Join-Path $OutDir $k.Dir) -Force } }
Write-Host "packaged v$ver into $OutDir"
Get-ChildItem $OutDir -Recurse -File | ForEach-Object { "{0,14:N0}  {1}" -f $_.Length, $_.FullName.Substring($OutDir.Length + 1) }
