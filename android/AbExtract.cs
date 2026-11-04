// Extract one file from an Android `adb backup` archive (.ab): text header + zlib-compressed tar.
using System;
using System.IO;
using System.IO.Compression;
using System.Text;

public static class AbExtract {
    static string ReadLine(Stream s) {
        var sb = new StringBuilder();
        int c;
        while ((c = s.ReadByte()) >= 0 && c != '\n') sb.Append((char)c);
        return sb.ToString();
    }

    static bool ReadFull(Stream s, byte[] buf, int n) {
        int got = 0;
        while (got < n) {
            int r = s.Read(buf, got, n - got);
            if (r <= 0) return false;
            got += r;
        }
        return true;
    }

    // Returns true and writes `outFile` when an entry whose path ends with `suffix` is found.
    public static bool Extract(string abFile, string suffix, string outFile) {
        using (var fs = File.OpenRead(abFile)) {
            if (ReadLine(fs) != "ANDROID BACKUP") throw new Exception("not an adb backup file");
            ReadLine(fs);                       // format version
            bool compressed = ReadLine(fs) == "1";
            string enc = ReadLine(fs);
            if (enc != "none") throw new Exception("backup is password protected; redo it and leave the password empty");
            Stream tar = fs;
            if (compressed) {
                fs.ReadByte(); fs.ReadByte();   // zlib header
                tar = new DeflateStream(fs, CompressionMode.Decompress);
            }
            byte[] hdr = new byte[512];
            string longName = null;
            while (ReadFull(tar, hdr, 512)) {
                if (hdr[0] == 0) break;         // end of archive
                string name = Encoding.UTF8.GetString(hdr, 0, 100).TrimEnd('\0');
                string prefix = Encoding.UTF8.GetString(hdr, 345, 155).TrimEnd('\0');
                if (prefix.Length > 0) name = prefix + "/" + name;
                string sizeStr = Encoding.ASCII.GetString(hdr, 124, 12).Trim('\0', ' ');
                long size = sizeStr.Length > 0 ? Convert.ToInt64(sizeStr, 8) : 0;
                char type = (char)hdr[156];
                byte[] data = new byte[size];
                if (!ReadFull(tar, data, (int)size)) break;
                int pad = (int)((512 - size % 512) % 512);
                if (pad > 0) ReadFull(tar, new byte[pad], pad);
                if (type == 'L') { longName = Encoding.UTF8.GetString(data).TrimEnd('\0'); continue; }
                if (longName != null) { name = longName; longName = null; }
                if (name.EndsWith(suffix)) {
                    File.WriteAllBytes(outFile, data);
                    return true;
                }
            }
        }
        return false;
    }
}
