# Move your own save from the official Phigros app into the modded build (no login needed).
#
# Step 1, with the OFFICIAL app installed (and your progress in it):
#   powershell -ExecutionPolicy Bypass -File mod\android\import-save.ps1 -Backup
#   -> confirm "Back up my data" on the phone, leave the password empty.
#   The save is kept on this PC in mod\android\save\ (also a handy backup).
# Step 2, after installing the modded APK:
#   powershell -ExecutionPolicy Bypass -File mod\android\import-save.ps1 -Import
#   -> pushes the save; the loader puts it in place on the next launch.
param([switch]$Backup, [switch]$Import, [string]$Package = "com.PigeonGames.Phigros")
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$saveDir = Join-Path $here "save"
$xmlName = "$Package.v2.playerprefs.xml"
$xml = Join-Path $saveDir $xmlName

$adb = (Get-Command adb -ErrorAction SilentlyContinue).Source
if (-not $adb) { $adb = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet" -Recurse -Filter adb.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName }
if (-not $adb) { throw "adb not found (winget install Google.PlatformTools)" }

if ($Backup) {
  New-Item -ItemType Directory -Force $saveDir | Out-Null
  $ab = Join-Path $saveDir "official.ab"
  Write-Host "Confirm the backup on the phone (leave the password empty)..."
  & $adb backup -f $ab -noapk $Package
  if (-not (Test-Path $ab) -or (Get-Item $ab).Length -lt 100) { throw "backup is empty: was it confirmed on the phone?" }
  Add-Type -Path (Join-Path $here "AbExtract.cs")
  if (-not [AbExtract]::Extract($ab, "/sp/$xmlName", $xml)) { throw "no PlayerPrefs found in the backup (is the official app installed and played?)" }
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  Copy-Item $xml (Join-Path $saveDir "$xmlName.$stamp.bak")
  Write-Host "save extracted: $xml ($((Get-Item $xml).Length) bytes)"
}

if ($Import) {
  if (-not (Test-Path $xml)) { throw "no extracted save yet; run with -Backup first" }
  $dest = "/sdcard/Android/data/$Package/files/PhiMeMod/import"
  & $adb shell mkdir -p $dest
  & $adb push $xml "$dest/$xmlName"
  if ($LASTEXITCODE -ne 0) { throw "adb push failed" }
  & $adb shell am force-stop $Package
  & $adb logcat -c
  & $adb shell monkey -p $Package -c android.intent.category.LAUNCHER 1 | Out-Null
  Start-Sleep -Seconds 4
  & $adb logcat -d -s PhiMeMod | Select-String "import"
  Write-Host "done: the game was restarted with your save"
}

if (-not $Backup -and -not $Import) { Write-Host "use -Backup (official app installed) or -Import (modded app installed)" }
