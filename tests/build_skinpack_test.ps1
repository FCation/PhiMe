param([switch]$AddressSanitizer)
$ErrorActionPreference = "Stop"
$llvm = "C:\Program Files\LLVM\bin\clang++.exe"
$out = Join-Path $PSScriptRoot "..\out\tests"
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $out $(if ($AddressSanitizer) { "skinpack_test_asan.exe" } else { "skinpack_test.exe" })
$flags = @("-std=c++17", "-DSKINPACK_HOST_TEST", "-Wall", "-Wextra", "-Werror",
           "-fno-exceptions", "-fno-rtti", "-g", "-O1")
if ($AddressSanitizer) { $flags += @("-fsanitize=address", "-fno-omit-frame-pointer") }
& $llvm @flags (Join-Path $PSScriptRoot "skinpack_test.cpp") (Join-Path $PSScriptRoot "..\src\skinpack.cpp") -o $exe
if ($LASTEXITCODE -ne 0) { throw "skinpack host test build failed" }
$savedPath = $env:PATH
if ($AddressSanitizer) {
    $runtime = & $llvm --print-runtime-dir
    if ($LASTEXITCODE -ne 0) { throw "cannot locate LLVM sanitizer runtime" }
    if (-not (Test-Path (Join-Path $runtime "clang_rt.asan_dynamic-x86_64.dll"))) {
        $resource = & $llvm -print-resource-dir
        $runtime = Join-Path $resource "lib\windows"
    }
    $env:PATH = "$runtime;$savedPath"
}
Push-Location $PSScriptRoot
try {
    & $exe
    $testExit = $LASTEXITCODE
} finally {
    Pop-Location
    $env:PATH = $savedPath
}
if ($testExit -ne 0) { throw "skinpack tests failed ($testExit failures)" }
