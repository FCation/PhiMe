# Make the CJK font PhiMe carries (Chinese / Japanese characters; Latin is always Source Serif 4), cut down to
#   Regular: GB2312 + JIS X 0208 + kana + punctuation + every character in the mod's own sources
#   Bold:    only the mod's own characters (titles / buttons); others are drawn as Regular + synthetic bold
# The output is always named PhiMeCJK-Regular.<ext> / PhiMeCJK-Bold.<ext> (what src/assets.cpp embeds).
#   sans   Noto Sans SC 思源黑体 (SIL OFL 1.1, github.com/notofonts/noto-cjk, Sans/SubsetOTF/SC)   <- in use
#   wenkai LXGW WenKai 霞鹜文楷 (SIL OFL 1.1, github.com/lxgw/LxgwWenKai)
# Usage: python -I make_cjk_subset.py <sans|wenkai> <dir with the source fonts> <mod dir> <out dir>
import sys, os
from fontTools import subset
from fontTools.ttLib import TTFont

FAMILIES = {
    "sans": ("NotoSansSC-Regular.otf", "NotoSansSC-Bold.otf"),
    "wenkai": ("LXGWWenKai-Regular.ttf", "LXGWWenKai-Medium.ttf"),
}
family, fonts, src, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
regular, bold = FAMILIES[family]

def source_chars():
    s = set()
    for root, _, files in os.walk(src):
        for f in files:
            if f.endswith((".cpp", ".h", ".java")):
                s.update(ord(c) for c in open(os.path.join(root, f), encoding="utf-8").read() if ord(c) > 0x7F)
    return s

def table(codec, into):
    for a in range(0x81, 0xFF):
        for b in range(0x40, 0xFF):
            try:
                s = bytes([a, b]).decode(codec)
            except UnicodeDecodeError:
                continue
            if len(s) == 1:
                into.add(ord(s))

own = source_chars()
own.update(range(0x20, 0x7F))
own.update(range(0x3000, 0x3040))
own.update(range(0xFF00, 0xFF60))
full = set(own)
for lo, hi in [(0xA0, 0xFF), (0x2000, 0x206F), (0x2190, 0x21FF), (0x2460, 0x24FF), (0x25A0, 0x25FF),
               (0x3040, 0x30FF), (0xFF00, 0xFFEF)]:
    full.update(range(lo, hi + 1))
table("gb2312", full)
table("shift_jis", full)
print(family, "characters:", len(full), "/ bold:", len(own))
os.makedirs(out, exist_ok=True)
for name, cs, dst in [(regular, full, "PhiMeCJK-Regular"), (bold, own, "PhiMeCJK-Bold")]:
    opts = subset.Options()
    opts.layout_features = ["*"]
    opts.name_IDs = ["*"]
    opts.hinting = False
    font = TTFont(os.path.join(fonts, name))
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=cs)
    sub.subset(font)
    path = os.path.join(out, dst + os.path.splitext(name)[1])
    font.save(path)
    print(path, os.path.getsize(path), "glyphs", font["maxp"].numGlyphs)
