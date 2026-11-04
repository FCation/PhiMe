# Day-to-day Android update: build the mod, push it to the phone, restart the game.
# Needs the repacked APK (repack.ps1) installed once.
#   powershell -ExecutionPolicy Bypass -File mod\android\push.ps1          # build + push + restart
#   powershell -ExecutionPolicy Bypass -File mod\android\push.ps1 -Log     # ...then follow the mod's log
param([switch]$Log, [switch]$NoBuild, [string]$Package = "com.PigeonGames.Phigros")
$ErrorActionPreference = "Stop"
$modRoot = Split-Path $PSScriptRoot

$adb = (Get-Command adb -ErrorAction SilentlyContinue).Source
if (-not $adb) { $adb = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet" -Recurse -Filter adb.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName }
if (-not $adb) { throw "adb not found (winget install Google.PlatformTools)" }

if (-not $NoBuild) {
  & powershell -ExecutionPolicy Bypass -File (Join-Path $modRoot "build_android.ps1")
  if ($LASTEXITCODE -ne 0) { throw "android build failed" }
}
$so = Join-Path $modRoot "out\android\libPhiMeMod.so"
$dest = "/sdcard/Android/data/$Package/files/PhiMeMod"

& $adb shell mkdir -p $dest
& $adb push $so "$dest/libPhiMeMod.so"
if ($LASTEXITCODE -ne 0) { throw "adb push failed (phone connected? USB debugging on?)" }
& $adb shell am force-stop $Package
& $adb logcat -c
& $adb shell monkey -p $Package -c android.intent.category.LAUNCHER 1 | Out-Null
Write-Host "pushed and restarted $Package"
if ($Log) { & $adb logcat -s PhiMeMod }
