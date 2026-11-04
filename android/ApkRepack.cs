// Streaming APK repacker: copies every entry's compressed bytes as-is (no recompression, constant memory),
// renames / drops / adds entries, aligns stored entries like zipalign, and drops the old signature.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

public static class ApkRepack {
    class Entry {
        public ushort VerMade, VerNeed, Flags, Method, Time, Date;
        public uint Crc, CSize, USize, ExtAttr, LocalOffset;
        public ushort IntAttr;
        public string Name;
        public byte[] CdExtra, Comment;
        public byte[] NewData;  // for added entries (already compressed)
    }

    static uint[] crcTable;
    static uint Crc32(byte[] d) {
        if (crcTable == null) {
            crcTable = new uint[256];
            for (uint i = 0; i < 256; i++) {
                uint c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                crcTable[i] = c;
            }
        }
        uint crc = 0xFFFFFFFFu;
        foreach (byte b in d) crc = crcTable[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return ~crc;
    }

    static bool IsOldSignature(string n) {
        if (!n.StartsWith("META-INF/")) return false;
        string u = n.ToUpperInvariant();
        return u == "META-INF/MANIFEST.MF" || u.EndsWith(".SF") || u.EndsWith(".RSA") || u.EndsWith(".DSA") || u.EndsWith(".EC");
    }

    // renames: "old=new" ("old=" drops the entry); adds: "zipPath=localFile" or "zipPath=localFile|755" (unix mode,
    // e.g. for executables in an iOS app). Also used for .ipa files (package_ipa.ps1).
    public static void Run(string src, string dst, string[] renames, string[] adds) {
        var ren = new Dictionary<string, string>();
        foreach (var r in renames) { var p = r.Split('='); ren[p[0]] = p[1]; }
        var entries = new List<Entry>();
        using (var fs = new FileStream(src, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20))
        using (var br = new BinaryReader(fs)) {
            // end of central directory
            long len = fs.Length;
            int tail = (int)Math.Min(len, 65557);
            fs.Position = len - tail;
            byte[] t = br.ReadBytes(tail);
            int eocd = -1;
            for (int i = tail - 22; i >= 0; i--)
                if (t[i] == 0x50 && t[i + 1] == 0x4b && t[i + 2] == 5 && t[i + 3] == 6) { eocd = i; break; }
            if (eocd < 0) throw new Exception("no end of central directory");
            ushort count = BitConverter.ToUInt16(t, eocd + 10);
            uint cdOff = BitConverter.ToUInt32(t, eocd + 16);
            if (count == 0xFFFF || cdOff == 0xFFFFFFFF) throw new Exception("zip64 not supported");
            fs.Position = cdOff;
            for (int i = 0; i < count; i++) {
                if (br.ReadUInt32() != 0x02014b50) throw new Exception("bad central directory");
                var e = new Entry();
                e.VerMade = br.ReadUInt16(); e.VerNeed = br.ReadUInt16(); e.Flags = br.ReadUInt16();
                e.Method = br.ReadUInt16(); e.Time = br.ReadUInt16(); e.Date = br.ReadUInt16();
                e.Crc = br.ReadUInt32(); e.CSize = br.ReadUInt32(); e.USize = br.ReadUInt32();
                ushort nl = br.ReadUInt16(), xl = br.ReadUInt16(), cl = br.ReadUInt16();
                br.ReadUInt16(); e.IntAttr = br.ReadUInt16(); e.ExtAttr = br.ReadUInt32(); e.LocalOffset = br.ReadUInt32();
                e.Name = Encoding.UTF8.GetString(br.ReadBytes(nl));
                e.CdExtra = br.ReadBytes(xl); e.Comment = br.ReadBytes(cl);
                Zip64(e);
                entries.Add(e);
            }
            entries.Sort((a, b) => a.LocalOffset.CompareTo(b.LocalOffset));

            var outList = new List<Entry>();
            using (var os = new FileStream(dst, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
            using (var bw = new BinaryWriter(os)) {
                byte[] buf = new byte[1 << 20];
                foreach (var e in entries) {
                    if (IsOldSignature(e.Name)) continue;
                    // source data position
                    fs.Position = e.LocalOffset + 26;
                    ushort lnl = br.ReadUInt16(), lxl = br.ReadUInt16();
                    long dataPos = e.LocalOffset + 30 + lnl + lxl;
                    string name = ren.ContainsKey(e.Name) ? ren[e.Name] : e.Name;
                    if (name.Length == 0) continue;  // dropped (replaced by an added entry)
                    var ne = Clone(e, name);
                    WriteLocal(bw, ne);
                    fs.Position = dataPos;
                    long left = e.CSize;
                    while (left > 0) {
                        int n = br.Read(buf, 0, (int)Math.Min(buf.Length, left));
                        if (n <= 0) throw new Exception("short read: " + e.Name);
                        bw.Write(buf, 0, n);
                        left -= n;
                    }
                    outList.Add(ne);
                }
                foreach (var a in adds) {
                    var p = a.Split(new[] { '=' }, 2);
                    string file = p[1];
                    uint mode = 0;
                    int bar = file.LastIndexOf('|');
                    if (bar > 0) { mode = Convert.ToUInt32(file.Substring(bar + 1), 8); file = file.Substring(0, bar); }
                    byte[] raw = File.ReadAllBytes(file);
                    var ne = new Entry {
                        VerMade = 20, VerNeed = 20, Method = 8, Time = 0, Date = 0x21, Crc = Crc32(raw),
                        USize = (uint)raw.Length, Name = p[0], CdExtra = new byte[0], Comment = new byte[0]
                    };
                    if (mode != 0) {  // made by unix, regular file with that mode
                        ne.VerMade = 0x0314;
                        ne.ExtAttr = (0x8000u | mode) << 16;
                    }
                    using (var ms = new MemoryStream()) {
                        using (var ds = new DeflateStream(ms, CompressionMode.Compress, true)) ds.Write(raw, 0, raw.Length);
                        ne.NewData = ms.ToArray();
                    }
                    ne.CSize = (uint)ne.NewData.Length;
                    WriteLocal(bw, ne);
                    bw.Write(ne.NewData);
                    outList.Add(ne);
                }
                // central directory
                long cdStart = os.Position;
                foreach (var e in outList) {
                    byte[] nb = Encoding.UTF8.GetBytes(e.Name);
                    bw.Write(0x02014b50u); bw.Write(e.VerMade); bw.Write(e.VerNeed); bw.Write(e.Flags);
                    bw.Write(e.Method); bw.Write(e.Time); bw.Write(e.Date); bw.Write(e.Crc); bw.Write(e.CSize);
                    bw.Write(e.USize); bw.Write((ushort)nb.Length); bw.Write((ushort)e.CdExtra.Length);
                    bw.Write((ushort)e.Comment.Length); bw.Write((ushort)0); bw.Write(e.IntAttr); bw.Write(e.ExtAttr);
                    bw.Write(e.LocalOffset); bw.Write(nb); bw.Write(e.CdExtra); bw.Write(e.Comment);
                }
                long cdEnd = os.Position;
                if (cdEnd > uint.MaxValue) throw new Exception("output too large for zip32");
                bw.Write(0x06054b50u); bw.Write((ushort)0); bw.Write((ushort)0);
                bw.Write((ushort)outList.Count); bw.Write((ushort)outList.Count);
                bw.Write((uint)(cdEnd - cdStart)); bw.Write((uint)cdStart); bw.Write((ushort)0);
            }
        }
    }

    // Some zips (e.g. ipa files) use the zip64 extra field (0x0001) for values that still fit in 32 bits: the
    // 0xFFFFFFFF fields are read from it, and the field is dropped (the output stays a plain zip32 file).
    static void Zip64(Entry e) {
        var keep = new MemoryStream();
        int p = 0;
        while (p + 4 <= e.CdExtra.Length) {
            ushort id = BitConverter.ToUInt16(e.CdExtra, p), sz = BitConverter.ToUInt16(e.CdExtra, p + 2);
            if (p + 4 + sz > e.CdExtra.Length) break;
            if (id == 1) {
                int q = p + 4;
                Func<ulong> next = () => { ulong v = BitConverter.ToUInt64(e.CdExtra, q); q += 8; return v; };
                if (e.USize == 0xFFFFFFFF) e.USize = Checked(next(), e.Name);
                if (e.CSize == 0xFFFFFFFF) e.CSize = Checked(next(), e.Name);
                if (e.LocalOffset == 0xFFFFFFFF) e.LocalOffset = Checked(next(), e.Name);
            } else {
                keep.Write(e.CdExtra, p, 4 + sz);
            }
            p += 4 + sz;
        }
        e.CdExtra = keep.ToArray();
    }
    static uint Checked(ulong v, string name) {
        if (v >= 0xFFFFFFFF) throw new Exception("zip64 value too large: " + name);
        return (uint)v;
    }

    static Entry Clone(Entry e, string name) {
        return new Entry {
            VerMade = e.VerMade, VerNeed = e.VerNeed, Flags = (ushort)(e.Flags & ~8), Method = e.Method, Time = e.Time,
            Date = e.Date, Crc = e.Crc, CSize = e.CSize, USize = e.USize, ExtAttr = e.ExtAttr, IntAttr = e.IntAttr,
            Name = name, CdExtra = e.CdExtra, Comment = e.Comment
        };
    }

    // Local header; stored entries get an alignment extra field (0xD935, same as zipalign/apksigner):
    // 4 bytes for normal files, 16 KiB for uncompressed .so files.
    static void WriteLocal(BinaryWriter bw, Entry e) {
        long pos = bw.BaseStream.Position;
        if (pos > uint.MaxValue) throw new Exception("output too large for zip32");
        e.LocalOffset = (uint)pos;
        byte[] nb = Encoding.UTF8.GetBytes(e.Name);
        byte[] extra = new byte[0];
        if (e.Method == 0) {
            int align = e.Name.EndsWith(".so") ? 16384 : 4;
            long dataStart = pos + 30 + nb.Length;
            int pad = (int)((align - (dataStart + 6) % align) % align);
            extra = new byte[6 + pad];
            BitConverter.GetBytes((ushort)0xD935).CopyTo(extra, 0);
            BitConverter.GetBytes((ushort)(2 + pad)).CopyTo(extra, 2);
            BitConverter.GetBytes((ushort)align).CopyTo(extra, 4);
        }
        bw.Write(0x04034b50u); bw.Write(e.VerNeed); bw.Write(e.Flags); bw.Write(e.Method); bw.Write(e.Time);
        bw.Write(e.Date); bw.Write(e.Crc); bw.Write(e.CSize); bw.Write(e.USize); bw.Write((ushort)nb.Length);
        bw.Write((ushort)extra.Length); bw.Write(nb); bw.Write(extra);
    }
}
