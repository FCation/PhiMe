# Builds a ready-to-sideload .ipa: the original (decrypted) Phigros ipa with PhiMeMod.dylib inside the app's
# Frameworks folder and an LC_LOAD_DYLIB for it in the main executable. Sign it with your usual tool (Sideloadly,
# AltStore, ESign, Feather, TrollStore...); they re-sign every binary, so no injection step is needed there.
#   powershell -ExecutionPolicy Bypass -File mod\ios\package_ipa.ps1 -Ipa path\to\original.ipa
param(
  [Parameter(Mandatory = $true)][string]$Ipa,
  [string]$Out = ""
)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$modRoot = Split-Path $here
if (-not $Out) { $Out = Join-Path $modRoot "out\ios\Phigros-PhiMeMod.ipa" }
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null

# ---- build the dylib ----
& powershell -ExecutionPolicy Bypass -File (Join-Path $modRoot "build_ios.ps1")
if ($LASTEXITCODE -ne 0) { throw "ios build failed" }
$dylib = Join-Path $modRoot "out\PhiMeMod.dylib"

# ---- find the app and its executable ----
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::OpenRead($Ipa)
try {
  $plist = $z.Entries | Where-Object { $_.FullName -match '^Payload/[^/]+\.app/Info\.plist$' } | Select-Object -First 1
  if (-not $plist) { throw "no Payload/*.app/Info.plist in $Ipa" }
  $app = $plist.FullName.Substring(0, $plist.FullName.Length - "Info.plist".Length)
  $r = New-Object IO.StreamReader($plist.Open())
  $text = $r.ReadToEnd(); $r.Close()
  if ($text -notmatch '<key>CFBundleExecutable</key>\s*<string>([^<]+)</string>') { throw "CFBundleExecutable not found (binary plist?)" }
  $exeName = $matches[1]
  # file sharing on, so the app's Documents (PhiMeMod.log, saves) shows in the Files app (the panel's 打开 button)
  $plistText = $text
  foreach ($k in @("UIFileSharingEnabled", "LSSupportsOpeningDocumentsInPlace")) {
    if ($plistText -match "<key>$k</key>\s*<(true|false)\s*/>") {
      $plistText = $plistText -replace "(<key>$k</key>\s*)<false\s*/>", '$1<true/>'
    } else {
      $i = $plistText.LastIndexOf("</dict>")
      $plistText = $plistText.Substring(0, $i) + "  <key>$k</key>`n  <true/>`n" + $plistText.Substring($i)
    }
  }
  $exeEntry = $z.GetEntry("$app$exeName")
  if (-not $exeEntry) { throw "executable $app$exeName not in the ipa" }
  $tmp = Join-Path $env:TEMP "phime_ipa_$PID"
  New-Item -ItemType Directory -Force $tmp | Out-Null
  $exe = Join-Path $tmp $exeName
  [IO.Compression.ZipFileExtensions]::ExtractToFile($exeEntry, $exe, $true)
  $already = $z.GetEntry("${app}Frameworks/PhiMeMod.dylib")
} finally { $z.Dispose() }
if ($already) { throw "this ipa already contains PhiMeMod.dylib: start from the original ipa" }

# ---- add LC_LOAD_DYLIB @executable_path/Frameworks/PhiMeMod.dylib (thin arm64 Mach-O, room after the commands) ----
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public static class MachoInject {
    public static string Run(string path, string dylib) {
        byte[] b = File.ReadAllBytes(path);
        uint magic = BitConverter.ToUInt32(b, 0);
        if (magic != 0xFEEDFACF) throw new Exception("not a thin 64-bit Mach-O (magic " + magic.ToString("X") + ")");
        uint ncmds = BitConverter.ToUInt32(b, 16), sizeofcmds = BitConverter.ToUInt32(b, 20);
        long end = 32 + sizeofcmds, firstData = long.MaxValue;
        long p = 32;
        for (uint i = 0; i < ncmds; i++) {
            uint cmd = BitConverter.ToUInt32(b, (int)p), size = BitConverter.ToUInt32(b, (int)p + 4);
            if (cmd == 0x19) {  // LC_SEGMENT_64: sections' file offsets bound the free space
                uint nsects = BitConverter.ToUInt32(b, (int)p + 64);
                for (uint s = 0; s < nsects; s++) {
                    long sp = p + 72 + s * 80;
                    uint off = BitConverter.ToUInt32(b, (int)sp + 48);
                    if (off != 0 && off < firstData) firstData = off;
                }
            }
            if (cmd == 0x2C) {  // LC_ENCRYPTION_INFO_64
                if (BitConverter.ToUInt32(b, (int)p + 16) != 0) throw new Exception("the executable is encrypted (cryptid != 0): use a decrypted ipa");
            }
            if (cmd == 0xC || cmd == 0x18) {
                uint noff = BitConverter.ToUInt32(b, (int)p + 8);
                int n = (int)(p + noff), e = n;
                while (b[e] != 0) e++;
                if (Encoding.ASCII.GetString(b, n, e - n) == dylib) return "already loads " + dylib;
            }
            p += size;
        }
        byte[] name = Encoding.ASCII.GetBytes(dylib);
        uint cmdsize = (uint)((24 + name.Length + 1 + 7) & ~7);
        if (end + cmdsize > firstData) throw new Exception("no room for another load command");
        for (long i = end; i < end + cmdsize; i++) if (b[i] != 0) throw new Exception("header padding is not empty");
        int q = (int)end;
        BitConverter.GetBytes(0xCu).CopyTo(b, q);            // LC_LOAD_DYLIB
        BitConverter.GetBytes(cmdsize).CopyTo(b, q + 4);
        BitConverter.GetBytes(24u).CopyTo(b, q + 8);         // name offset
        BitConverter.GetBytes(2u).CopyTo(b, q + 12);         // timestamp
        BitConverter.GetBytes(0x10000u).CopyTo(b, q + 16);   // current version 1.0.0
        BitConverter.GetBytes(0x10000u).CopyTo(b, q + 20);   // compatibility version 1.0.0
        name.CopyTo(b, q + 24);
        BitConverter.GetBytes(ncmds + 1).CopyTo(b, 16);
        BitConverter.GetBytes(sizeofcmds + cmdsize).CopyTo(b, 20);
        File.WriteAllBytes(path, b);
        return "added LC_LOAD_DYLIB " + dylib + " (" + cmdsize + " bytes, " + (firstData - end - cmdsize) + " bytes of room left)";
    }
}
'@
Write-Host ([MachoInject]::Run($exe, "@executable_path/Frameworks/PhiMeMod.dylib"))
& 'C:\Program Files\LLVM\bin\llvm-objdump.exe' --macho --private-headers $exe | Select-String 'PhiMeMod' | ForEach-Object { Write-Host "check: $($_.Line.Trim())" }

# ---- repack: everything copied as-is, the executable replaced, the dylib added (both 0755) ----
Add-Type -ReferencedAssemblies System.IO.Compression -Path (Join-Path $modRoot "android\ApkRepack.cs")
Write-Host "repacking $Ipa ..."
$sw = [Diagnostics.Stopwatch]::StartNew()
$plistFile = Join-Path $tmp "Info.plist"
[IO.File]::WriteAllText($plistFile, $plistText, (New-Object Text.UTF8Encoding $false))
[ApkRepack]::Run($Ipa, $Out, @("$app$exeName=", "${app}Info.plist="),
  @("$app$exeName=$exe|755", "${app}Frameworks/PhiMeMod.dylib=$dylib|755", "${app}Info.plist=$plistFile|644"))
Remove-Item -Recurse -Force $tmp
Write-Host "repacked in $([int]$sw.Elapsed.TotalSeconds)s"

# ---- sanity check: it opens as a zip and has both files ----
$z = [IO.Compression.ZipFile]::OpenRead($Out)
try {
  foreach ($n in @("$app$exeName", "${app}Frameworks/PhiMeMod.dylib")) {
    $e = $z.GetEntry($n)
    if (-not $e) { throw "missing $n in the output" }
    $s = $e.Open(); $buf = New-Object byte[] 4; [void]$s.Read($buf, 0, 4); $s.Close()
    Write-Host ("ok: {0} ({1} bytes, mode {2})" -f $n, $e.Length, [Convert]::ToString(($e.ExternalAttributes -shr 16) -band 0x1FF, 8))
  }
} finally { $z.Dispose() }
Write-Host "done: $Out ($([math]::Round((Get-Item $Out).Length / 1GB, 2)) GB)"
