# Android emulator for testing the mod on this PC (official Android Emulator, x86_64 Android 13 image with Google's
# ARM translation, so the arm64 game and mod run). Everything is driven through adb: screenshots, taps, logcat.
#   powershell -ExecutionPolicy Bypass -File mod\android\emu.ps1             # create (once) + boot, headless
#   powershell -ExecutionPolicy Bypass -File mod\android\emu.ps1 -Install    # ...and install the repacked APK
#   powershell -ExecutionPolicy Bypass -File mod\android\emu.ps1 -Show       # with a window
#   powershell -ExecutionPolicy Bypass -File mod\android\emu.ps1 -Stop
# Then mod updates are the usual push.ps1 (it talks to the emulator when no phone is plugged in).
param([switch]$Install, [switch]$Show, [switch]$Stop, [string]$Gpu = "host")
$ErrorActionPreference = "Stop"
$modRoot = Split-Path $PSScriptRoot
$sdk = Join-Path $env:LOCALAPPDATA "PhiMeTools\android-sdk"
$avdName = "PhiMe"
$image = "system-images;android-33;google_apis;x86_64"
$env:ANDROID_SDK_ROOT = $sdk
$env:ANDROID_HOME = $sdk
$env:ANDROID_AVD_HOME = Join-Path $sdk "avd"
$jdk = Get-ChildItem "C:\Program Files\Microsoft" -Directory -Filter "jdk-*" | Sort-Object Name | Select-Object -Last 1
$env:JAVA_HOME = $jdk.FullName

$adb = (Get-Command adb -ErrorAction SilentlyContinue).Source  # the same adb as push.ps1 (one adb server)
if (-not $adb) { $adb = Join-Path $sdk "platform-tools\adb.exe" }
function Emu { (& $adb devices) -match '^emulator-\d+\s+device' | ForEach-Object { ($_ -split '\s+')[0] } | Select-Object -First 1 }

if ($Stop) {
  $e = Emu
  if ($e) { & $adb -s $e emu kill | Out-Null; Write-Host "stopped $e" } else { Write-Host "no emulator running" }
  return
}

# ---- the virtual phone, made once: 1080 x 2400, 6 GB RAM, 24 GB data (the game alone is 2.4 GB) ----
$avdDir = Join-Path $env:ANDROID_AVD_HOME "$avdName.avd"
if (-not (Test-Path $avdDir)) {
  New-Item -ItemType Directory -Force $env:ANDROID_AVD_HOME | Out-Null
  "no" | & (Join-Path $sdk "cmdline-tools\latest\bin\avdmanager.bat") create avd -n $avdName -k $image -d pixel_6 --force | Out-Null
  $cfg = Join-Path $avdDir "config.ini"
  $lines = Get-Content $cfg | Where-Object { $_ -notmatch '^(hw\.ramSize|disk\.dataPartition\.size|hw\.keyboard|hw\.gpu\.enabled|hw\.gpu\.mode|vm\.heapSize)=' }
  $lines += "hw.ramSize=6144", "disk.dataPartition.size=24G", "hw.keyboard=yes", "hw.gpu.enabled=yes", "hw.gpu.mode=$Gpu", "vm.heapSize=512"
  Set-Content -Encoding ascii $cfg $lines
  Write-Host "created AVD $avdName"
}

# ---- boot ----
$e = Emu
if (-not $e) {
  $emuArgs = @("-avd", $avdName, "-no-audio", "-no-boot-anim", "-gpu", $Gpu, "-no-snapshot-save")
  if (-not $Show) { $emuArgs += "-no-window" }
  $log = Join-Path $env:TEMP "phime-emulator.log"
  Start-Process -FilePath (Join-Path $sdk "emulator\emulator.exe") -ArgumentList $emuArgs -WindowStyle Hidden `
    -RedirectStandardOutput $log -RedirectStandardError "$log.err"
  Write-Host "booting (log $log) ..."
  $sw = [Diagnostics.Stopwatch]::StartNew()
  do {
    Start-Sleep -Seconds 3
    $e = Emu
    $booted = $e -and ((& $adb -s $e shell getprop sys.boot_completed 2>$null) -match '1')
    if ($sw.Elapsed.TotalSeconds -gt 600) { throw "the emulator did not boot in 10 minutes (see $log)" }
  } until ($booted)
  Write-Host "booted $e in $([int]$sw.Elapsed.TotalSeconds)s"
  & $adb -s $e shell settings put global window_animation_scale 0
  & $adb -s $e shell settings put global transition_animation_scale 0
  & $adb -s $e shell settings put global animator_duration_scale 0
} else {
  Write-Host "already running: $e"
}

if ($Install) {
  $apk = Join-Path $modRoot "out\android\Phigros-PhiMeMod.apk"
  Write-Host "installing $apk ($([math]::Round((Get-Item $apk).Length / 1GB, 2)) GB) ..."
  & $adb -s $e install -r -g $apk
  if ($LASTEXITCODE -ne 0) { throw "install failed" }
  & $adb -s $e shell mkdir -p /sdcard/Android/data/com.PigeonGames.Phigros/files/PhiMeMod
  Write-Host "installed"
}
