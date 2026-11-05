# Build PhiMeMod.dylib (arm64 iOS) with the Windows LLVM toolchain. No Xcode / SDK needed.
#   powershell -ExecutionPolicy Bypass -File mod\build_ios.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$llvm = "C:\Program Files\LLVM\bin"
$out = Join-Path $root "out"
New-Item -ItemType Directory -Force $out | Out-Null

$src = Get-ChildItem (Join-Path $root "src") -Filter *.cpp | ForEach-Object { $_.FullName }
# fonts embedded with .incbin (src/assets.cpp) need the assets folder's absolute path
$fontsDir = (Join-Path $root "assets\fonts").Replace('\', '/')
[IO.File]::WriteAllText((Join-Path $out "pm_assets.h"), "#pragma once`n#define PM_FONTS_DIR `"$fontsDir`"`n")
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
