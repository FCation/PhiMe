# Build PhiMeMod.dylib (arm64 iOS) with the Windows LLVM toolchain. No Xcode / SDK needed.
#   powershell -ExecutionPolicy Bypass -File mod\build_ios.ps1 [-Serif Local|Source]
# -Serif: the Latin UI font. Local (default) = the font named in assets/fonts/serif.local.txt, for your own builds
# (see serif_font.ps1; without it: Source Serif 4); Source = Source Serif 4 (SIL OFL), for builds that are shared.
param([ValidateSet("Local", "Source")][string]$Serif = "Local")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$llvm = "C:\Program Files\LLVM\bin"
$out = Join-Path $root "out"
New-Item -ItemType Directory -Force $out | Out-Null

$src = Get-ChildItem (Join-Path $root "src") -Filter *.cpp | ForEach-Object { $_.FullName }
# fonts embedded with .incbin (src/assets.cpp) need the assets folder's absolute path
$fontsDir = (Join-Path $root "assets\fonts").Replace('\', '/')
. (Join-Path $root "serif_font.ps1")
[IO.File]::WriteAllText((Join-Path $out "pm_assets.h"), (pmAssetsHeader $fontsDir $Serif))
$dylib = Join-Path $out "PhiMeMod.dylib"

$flags = @(
  "--target=arm64-apple-ios14.0", "-std=c++17", "-O2", "-I$out", "-idirafter$(Join-Path $root 'src\third_party\libc')",
  "-fno-exceptions", "-fno-rtti", "-funwind-tables", "-fno-stack-protector", "-fno-threadsafe-statics",
  "-fvisibility=hidden", "-nostdlib", "-nostdinc++", "-Wall", "-Wno-unused-function",
  "-dynamiclib", "-fuse-ld=lld",
  "-Wl,-install_name,@rpath/PhiMeMod.dylib",
  "-Wl,-dead_strip",
  "-Wl,-adhoc_codesign"
)

& "$llvm\clang++.exe" @flags @src (Join-Path $root "ios\libSystem.tbd") -o $dylib
if ($LASTEXITCODE -ne 0) { throw "build failed" }
& "$llvm\llvm-strip.exe" -x $dylib
Write-Host "built $dylib ($((Get-Item $dylib).Length) bytes)"
