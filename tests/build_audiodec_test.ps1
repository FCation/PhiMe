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
    # MP3 / FLAC fixtures: a 440 Hz tone at half volume (the sine source is 1/8 loud, so x4), made with ffmpeg
    $ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
    if (-not $ffmpeg) { throw "ffmpeg is needed for the MP3 / FLAC fixtures (winget install ffmpeg)" }
    $tone = "sine=frequency=440:sample_rate={0}:duration={1},volume=4"
    $make = @(
        @("tone.mp3", 44100, 0.5, @("-ac", "1", "-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "3", "-metadata", "title=PhiMe")),
        @("tone_stereo.mp3", 48000, 0.5, @("-ac", "2", "-c:a", "libmp3lame", "-b:a", "192k", "-id3v2_version", "0", "-write_xing", "0")),
        @("tone.flac", 44100, 0.5, @("-ac", "2", "-c:a", "flac")),
        @("tone_51.flac", 48000, 0.5, @("-ac", "6", "-sample_fmt", "s32", "-bits_per_raw_sample", "24", "-c:a", "flac")),
        @("long.mp3", 44100, 25, @("-ac", "1", "-c:a", "libmp3lame", "-b:a", "64k")),
        @("long.flac", 44100, 25, @("-ac", "1", "-c:a", "flac"))
    )
    foreach ($m in $make) {
        $fa = @("-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", ($tone -f $m[1], $m[2])) + $m[3] + @((Join-Path $fixtures $m[0]))
        & $ffmpeg @fa
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg could not make $($m[0])" }
    }
    # FLAC behind an ID3v2 tag (some taggers write one): a 10-byte empty tag + a mono FLAC
    $mono = Join-Path $fixtures "mono.flac"
    & $ffmpeg -hide_banner -loglevel error -y -f lavfi -i ($tone -f 44100, 0.5) -ac 1 -c:a flac $mono
    $tag = [byte[]](0x49, 0x44, 0x33, 3, 0, 0, 0, 0, 0, 0)
    [IO.File]::WriteAllBytes((Join-Path $fixtures "tone_id3.flac"), $tag + [IO.File]::ReadAllBytes($mono))
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
