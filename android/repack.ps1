# One-time repack of the Android APK with the PhiMe loader. Needed again only when the game itself updates;
# mod updates afterwards are just `push.ps1`.
#   powershell -ExecutionPolicy Bypass -File mod\android\repack.ps1 -Apk path\to\original.apk
param(
  [Parameter(Mandatory = $true)][string]$Apk,
  [string]$Out = ""
)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$modRoot = Split-Path $here
if (-not $Out) { $Out = Join-Path $modRoot "out\android\Phigros-PhiMeMod.apk" }

# ---- tools ----
$jdk = Get-ChildItem "C:\Program Files\Microsoft" -Directory -Filter "jdk-*" -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
if (-not $jdk) { throw "JDK not found (winget install Microsoft.OpenJDK.21)" }
$env:PATH = "$($jdk.FullName)\bin;$env:PATH"
$bt = Get-ChildItem "$env:LOCALAPPDATA\PhiMeTools\bt" -Directory | Select-Object -First 1
if (-not $bt) { throw "Android build-tools not found in %LOCALAPPDATA%\PhiMeTools\bt" }
$apksigner = Join-Path $bt.FullName "apksigner.bat"
$zipalign = Join-Path $bt.FullName "zipalign.exe"

# ---- build the libraries ----
& powershell -ExecutionPolicy Bypass -File (Join-Path $modRoot "build_android.ps1")
if ($LASTEXITCODE -ne 0) { throw "android build failed" }
$libDir = Join-Path $modRoot "out\android"

# ---- signing key: created once, then reused so new builds install as updates ----
$ks = Join-Path $here "phimemod.keystore"
$pwFile = Join-Path $here "keystore.local.txt"
if (-not (Test-Path $ks)) {
  $pw = -join ((48..57) + (65..90) + (97..122) | Get-Random -Count 24 | ForEach-Object { [char]$_ })
  Set-Content -Encoding ascii $pwFile $pw
  $ErrorActionPreference = "Continue"  # keytool prints progress on stderr
  & keytool -genkeypair -keystore $ks -storetype PKCS12 -alias phimemod -keyalg RSA -keysize 2048 -validity 36500 `
    -storepass $pw -keypass $pw -dname "CN=PhiMeMod" 2>$null | Out-Null
  $ErrorActionPreference = "Stop"
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $ks)) { throw "keytool failed" }
  Write-Host "created signing key $ks (password in keystore.local.txt; keep both files)"
}

# ---- repack (streaming, no recompression) ----
Add-Type -ReferencedAssemblies System.IO.Compression -Path (Join-Path $here "ApkRepack.cs")
$unsigned = "$Out.unsigned"
Write-Host "repacking $Apk ..."
$sw = [Diagnostics.Stopwatch]::StartNew()
[ApkRepack]::Run($Apk, $unsigned,
  @("lib/arm64-v8a/libmain.so=lib/arm64-v8a/libmain_orig.so"),
  @("lib/arm64-v8a/libmain.so=$libDir\libmain.so", "lib/arm64-v8a/libPhiMeMod.so=$libDir\libPhiMeMod.so"))
Write-Host "repacked in $([int]$sw.Elapsed.TotalSeconds)s"

& $zipalign -c 4 $unsigned | Out-Null
if ($LASTEXITCODE -ne 0) { throw "alignment check failed" }

# ---- sign ----
Write-Host "signing ..."
$env:PHIME_KS_PW = (Get-Content $pwFile -Raw).Trim()
& $apksigner sign --ks $ks --ks-key-alias phimemod --ks-pass env:PHIME_KS_PW --out $Out $unsigned
Remove-Item Env:PHIME_KS_PW
if ($LASTEXITCODE -ne 0) { throw "apksigner failed" }
Remove-Item $unsigned
& $apksigner verify $Out
if ($LASTEXITCODE -ne 0) { throw "signature verification failed" }
Remove-Item "$Out.idsig" -ErrorAction SilentlyContinue
Write-Host "done: $Out ($([math]::Round((Get-Item $Out).Length / 1GB, 2)) GB)"
