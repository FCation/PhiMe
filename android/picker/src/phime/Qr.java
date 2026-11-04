package phime;

/**
 * A small QR code encoder (byte mode, error correction M, versions 1..10), enough for the TapTap login link.
 * Follows the ISO 18004 steps as Project Nayuki's reference encoder does. Result: "N:" + N*N chars of '0' / '1',
 * row by row from the top, '1' = dark; no quiet zone.
 */
final class Qr {
    // error correction level M, index = version
    static final int[] ECC_PER_BLOCK = {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
    static final int[] BLOCKS = {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
    static final int FORMAT_M = 0;

    int ver, size;
    boolean[][] dark, fixed;

    static String encode(String text) {
        byte[] data;
        try {
            data = text.getBytes("UTF-8");
        } catch (java.io.UnsupportedEncodingException e) {
            return null;
        }
        for (int v = 1; v <= 10; v++) {
            int cap = dataCodewords(v) * 8;
            int need = 4 + (v <= 9 ? 8 : 16) + data.length * 8;
            if (need <= cap) return new Qr(v).build(data).toString();
        }
        return null;
    }

    Qr(int v) {
        ver = v;
        size = v * 4 + 17;
        dark = new boolean[size][size];
        fixed = new boolean[size][size];
    }

    static int rawModules(int v) {
        int r = (16 * v + 128) * v + 64;
        if (v >= 2) {
            int n = v / 7 + 2;
            r -= (25 * n - 10) * n - 55;
            if (v >= 7) r -= 36;
        }
        return r;
    }

    static int dataCodewords(int v) {
        return rawModules(v) / 8 - ECC_PER_BLOCK[v] * BLOCKS[v];
    }

    Qr build(byte[] data) {
        // ---- the bit stream ----
        int cap = dataCodewords(ver);
        byte[] cw = new byte[cap];
        int[] pos = {0};
        put(cw, pos, 4, 4);
        put(cw, pos, data.length, ver <= 9 ? 8 : 16);
        for (byte b : data) put(cw, pos, b & 0xFF, 8);
        int left = cap * 8 - pos[0];
        put(cw, pos, 0, Math.min(4, left));
        pos[0] = (pos[0] + 7) / 8 * 8;
        for (int i = pos[0] / 8, p = 0; i < cap; i++, p ^= 1) cw[i] = (byte) (p == 0 ? 0xEC : 0x11);

        // ---- function patterns ----
        for (int i = 0; i < size; i++) {
            set(6, i, i % 2 == 0);
            set(i, 6, i % 2 == 0);
        }
        finder(3, 3);
        finder(size - 4, 3);
        finder(3, size - 4);
        int[] al = alignPositions();
        int n = al.length;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++)
                        set(al[i] + dx, al[j] + dy, Math.max(Math.abs(dx), Math.abs(dy)) != 1);
            }
        format(0);  // reserves the area; drawn again with the chosen mask
        version();

        // ---- codewords ----
        byte[] all = interleave(cw);
        int i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size; vert++)
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    boolean up = ((right + 1) & 2) == 0;
                    int y = up ? size - 1 - vert : vert;
                    if (!fixed[y][x] && i < all.length * 8) {
                        dark[y][x] = ((all[i >>> 3] >>> (7 - (i & 7))) & 1) != 0;
                        i++;
                    }
                }
        }

        // ---- mask: the one with the lowest penalty ----
        int best = 0;
        long bestScore = Long.MAX_VALUE;
        for (int m = 0; m < 8; m++) {
            mask(m);
            format(m);
            long s = penalty();
            if (s < bestScore) {
                bestScore = s;
                best = m;
            }
            mask(m);  // undo (xor)
        }
        mask(best);
        format(best);
        return this;
    }

    static void put(byte[] out, int[] pos, int val, int bits) {
        for (int i = bits - 1; i >= 0; i--) {
            if (((val >>> i) & 1) != 0) out[pos[0] >>> 3] |= (byte) (0x80 >>> (pos[0] & 7));
            pos[0]++;
        }
    }

    void set(int x, int y, boolean d) {
        dark[y][x] = d;
        fixed[y][x] = true;
    }

    void finder(int cx, int cy) {
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= size || y >= size) continue;
                int d = Math.max(Math.abs(dx), Math.abs(dy));
                set(x, y, d != 2 && d != 4);
            }
    }

    int[] alignPositions() {
        if (ver == 1) return new int[0];
        int n = ver / 7 + 2;
        int step = (ver * 8 + n * 3 + 5) / (n * 4 - 4) * 2;
        int[] r = new int[n];
        r[0] = 6;
        for (int i = n - 1, p = size - 7; i >= 1; i--, p -= step) r[i] = p;
        return r;
    }

    void format(int m) {
        int data = FORMAT_M << 3 | m, rem = data;
        for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412;
        for (int i = 0; i <= 5; i++) set(8, i, bit(bits, i));
        set(8, 7, bit(bits, 6));
        set(8, 8, bit(bits, 7));
        set(7, 8, bit(bits, 8));
        for (int i = 9; i < 15; i++) set(14 - i, 8, bit(bits, i));
        for (int i = 0; i < 8; i++) set(size - 1 - i, 8, bit(bits, i));
        for (int i = 8; i < 15; i++) set(8, size - 15 + i, bit(bits, i));
        set(8, size - 8, true);
    }

    void version() {
        if (ver < 7) return;
        int rem = ver;
        for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >>> 11) * 0x1F25);
        int bits = ver << 12 | rem;
        for (int i = 0; i < 18; i++) {
            boolean b = bit(bits, i);
            int a = size - 11 + i % 3, c = i / 3;
            set(a, c, b);
            set(c, a, b);
        }
    }

    static boolean bit(int x, int i) {
        return ((x >>> i) & 1) != 0;
    }

    byte[] interleave(byte[] data) {
        int nb = BLOCKS[ver], ecc = ECC_PER_BLOCK[ver], raw = rawModules(ver) / 8;
        int nShort = nb - raw % nb, shortLen = raw / nb;
        byte[] div = rsDivisor(ecc);
        byte[][] blocks = new byte[nb][];
        for (int i = 0, k = 0; i < nb; i++) {
            int len = shortLen - ecc + (i < nShort ? 0 : 1);
            byte[] dat = new byte[len];
            System.arraycopy(data, k, dat, 0, len);
            k += len;
            byte[] e = rsRemainder(dat, div);
            byte[] b = new byte[shortLen + 1];
            System.arraycopy(dat, 0, b, 0, len);
            System.arraycopy(e, 0, b, b.length - ecc, ecc);
            blocks[i] = b;
        }
        byte[] out = new byte[raw];
        int o = 0;
        for (int i = 0; i < blocks[0].length; i++)
            for (int j = 0; j < nb; j++)
                if (i != shortLen - ecc || j >= nShort) out[o++] = blocks[j][i];
        return out;
    }

    static int mul(int x, int y) {
        int z = 0;
        for (int i = 7; i >= 0; i--) {
            z = (z << 1) ^ ((z >>> 7) * 0x11D);
            z ^= ((y >>> i) & 1) * x;
        }
        return z & 0xFF;
    }

    static byte[] rsDivisor(int degree) {
        byte[] r = new byte[degree];
        r[degree - 1] = 1;
        int root = 1;
        for (int i = 0; i < degree; i++) {
            for (int j = 0; j < degree; j++) {
                r[j] = (byte) mul(r[j] & 0xFF, root);
                if (j + 1 < degree) r[j] ^= r[j + 1];
            }
            root = mul(root, 0x02);
        }
        return r;
    }

    static byte[] rsRemainder(byte[] data, byte[] div) {
        byte[] r = new byte[div.length];
        for (byte b : data) {
            int f = (b ^ r[0]) & 0xFF;
            System.arraycopy(r, 1, r, 0, r.length - 1);
            r[r.length - 1] = 0;
            for (int i = 0; i < r.length; i++) r[i] ^= (byte) mul(div[i] & 0xFF, f);
        }
        return r;
    }

    void mask(int m) {
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                if (fixed[y][x]) continue;
                boolean inv;
                switch (m) {
                    case 0: inv = (x + y) % 2 == 0; break;
                    case 1: inv = y % 2 == 0; break;
                    case 2: inv = x % 3 == 0; break;
                    case 3: inv = (x + y) % 3 == 0; break;
                    case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
                    case 5: inv = x * y % 2 + x * y % 3 == 0; break;
                    case 6: inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
                    default: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                if (inv) dark[y][x] = !dark[y][x];
            }
    }

    // runs of 5+ in a row / column, 2x2 blocks and dark/light balance (the finder-look-alike rule is left out:
    // any mask decodes, this only picks a tidy one)
    long penalty() {
        long s = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int a = 0; a < size; a++) {
                int run = 0;
                boolean last = false;
                for (int b = 0; b < size; b++) {
                    boolean d = pass == 0 ? dark[a][b] : dark[b][a];
                    if (b > 0 && d == last) {
                        run++;
                        if (run == 5) s += 3;
                        else if (run > 5) s++;
                    } else {
                        run = 1;
                        last = d;
                    }
                }
            }
        int nDark = 0;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                if (dark[y][x]) nDark++;
                if (x + 1 < size && y + 1 < size) {
                    boolean d = dark[y][x];
                    if (d == dark[y][x + 1] && d == dark[y + 1][x] && d == dark[y + 1][x + 1]) s += 3;
                }
            }
        int total = size * size;
        int k = (Math.abs(nDark * 20 - total * 10) + total - 1) / total - 1;
        if (k > 0) s += k * 10;
        return s;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder(size * size + 4);
        sb.append(size).append(':');
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) sb.append(dark[y][x] ? '1' : '0');
        return sb.toString();
    }
}
