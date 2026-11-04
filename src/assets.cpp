// Font files carried inside the mod (mod/assets/fonts), embedded with .incbin. The build scripts write
// pm_assets.h (PM_FONTS_DIR = that folder's absolute path) into their output folder.
//   Source Serif 4 Regular / Bold    the English UI (Adobe, SIL OFL 1.1)
//   PhiMeCJK Regular / Bold   the CJK characters: Noto Sans SC 思源黑体 (SIL OFL 1.1), cut down by
//     assets/fonts/make_cjk_subset.py (Regular: GB2312 + JIS X 0208 + kana + the mod's characters; Bold: the
//     mod's own characters only, anything else is drawn as Regular + bold). The script can make LXGW WenKai too.
#include "mod.h"
#include "pm_assets.h"

#if defined(__APPLE__)
#define PM_ASM_SECT ".section __TEXT,__const\n"
#define PM_ASM_SYM(n) "_" n
#define PM_ASM_HIDE(n) ".private_extern _" n "\n"
#else
#define PM_ASM_SECT ".section .rodata.phime_fonts,\"a\"\n"
#define PM_ASM_SYM(n) n
#define PM_ASM_HIDE(n) ".hidden " n "\n"
#endif
#define PM_EMBED(name, file)                                                                                   \
    __asm__(PM_ASM_SECT ".p2align 4\n" ".globl " PM_ASM_SYM(#name) "\n" PM_ASM_HIDE(#name) PM_ASM_SYM(#name) ":\n" \
            ".incbin \"" PM_FONTS_DIR "/" file "\"\n" ".globl " PM_ASM_SYM(#name "_end") "\n"              \
            PM_ASM_HIDE(#name "_end") PM_ASM_SYM(#name "_end") ":\n" ".byte 0\n" ".text\n");

PM_EMBED(pm_font_serif_r, "SourceSerif4-Regular.ttf")
PM_EMBED(pm_font_serif_b, "SourceSerif4-Bold.ttf")
PM_EMBED(pm_font_cjk_r, "PhiMeCJK-Regular.otf")
PM_EMBED(pm_font_cjk_b, "PhiMeCJK-Bold.otf")

extern "C" {
extern const unsigned char pm_font_serif_r[], pm_font_serif_r_end[], pm_font_serif_b[], pm_font_serif_b_end[],
    pm_font_cjk_r[], pm_font_cjk_r_end[], pm_font_cjk_b[], pm_font_cjk_b_end[];
}

namespace unity {
bool embeddedFont(int which, const unsigned char** data, int* size, const char** name) {
    static const unsigned char* const b[] = {pm_font_serif_r, pm_font_serif_b, pm_font_cjk_r, pm_font_cjk_b};
    static const unsigned char* const e[] = {pm_font_serif_r_end, pm_font_serif_b_end, pm_font_cjk_r_end,
                                             pm_font_cjk_b_end};
    static const char* const n[] = {"SourceSerif4-Regular.ttf", "SourceSerif4-Bold.ttf",
                                    "PhiMeCJK-Regular.otf", "PhiMeCJK-Bold.otf"};
    if (which < 0 || which > 3) return false;
    *data = b[which];
    *size = (int)(e[which] - b[which]);
    *name = n[which];
    return *size > 1000;
}
}  // namespace unity
