param([switch]$AddressSanitizer)
$ErrorActionPreference = "Stop"
$llvm = "C:\Program Files\LLVM\bin\clang++.exe"
$out = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\out\tests"))
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $out $(if ($AddressSanitizer) { "audiodec_test_asan.exe" } else { "audiodec_test.exe" })
$flags = @("-std=c++17", "-DAUDIODEC_HOST_TEST", "-D_CRT_SECURE_NO_WARNINGS", "-Wall", "-Wextra", "-Werror",
           "-fno-exceptions", "-fno-rtti", "-g", "-O1")
if ($AddressSanitizer) { $flags += @("-fsanitize=address", "-fno-omit-frame-pointer") }
& $llvm @flags (Join-Path $PSScriptRoot "audiodec_test.cpp") (Join-Path $PSScriptRoot "..\src\audiodec.cpp") -o $exe
if ($LASTEXITCODE -ne 0) { throw "audiodec host test build failed" }

$fixtures = [IO.Path]::GetFullPath((Join-Path $out ("audiodec-fixtures-" + [Guid]::NewGuid().ToString("N"))))
if (-not $fixtures.StartsWith($out + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "fixture directory is outside the test output directory"
}
New-Item -ItemType Directory $fixtures | Out-Null
$savedPath = $env:PATH
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $PSScriptRoot "data\PhiPack.zip"))
    try {
        foreach ($name in @("click.ogg", "drag.ogg", "flick.ogg")) {
            $entries = @($archive.Entries | Where-Object { $_.Name -ceq $name })
            if ($entries.Count -ne 1) { throw "expected exactly one $name in PhiPack.zip" }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], (Join-Path $fixtures $name))
        }
    } finally {
        $archive.Dispose()
    }
    if ($AddressSanitizer) {
        $runtime = & $llvm --print-runtime-dir
        if ($LASTEXITCODE -ne 0) { throw "cannot locate LLVM sanitizer runtime" }
        if (-not (Test-Path (Join-Path $runtime "clang_rt.asan_dynamic-x86_64.dll"))) {
            $resource = & $llvm -print-resource-dir
            $runtime = Join-Path $resource "lib\windows"
        }
        $env:PATH = "$runtime;$savedPath"
    }
    & $exe $fixtures
    $testExit = $LASTEXITCODE
    if ($testExit -ne 0) { throw "audiodec tests failed ($testExit failures)" }
} finally {
    $env:PATH = $savedPath
    Remove-Item -LiteralPath $fixtures -Recurse -Force
}
