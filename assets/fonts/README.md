# 字体（不在仓库里）

构建时 `src/assets.cpp` 会把这个文件夹里的 4 个字体文件直接嵌进 dylib / so，缺了会编译失败。字体文件没有放进仓库，自己构建前请先准备好：

| 文件名 | 来源 | 协议 |
|---|---|---|
| `SourceSerif4-Regular.ttf` / `SourceSerif4-Bold.ttf` | Adobe Source Serif 4，[github.com/adobe-fonts/source-serif](https://github.com/adobe-fonts/source-serif)（`release` 分支的 `TTF/`） | SIL OFL 1.1 |
| `PhiMeCJK-Regular.otf` / `PhiMeCJK-Bold.otf` | 思源黑体 Noto Sans SC 的子集，[github.com/notofonts/noto-cjk](https://github.com/notofonts/noto-cjk)（`Sans/SubsetOTF/SC`），用本目录的 `make_cjk_subset.py` 生成 | SIL OFL 1.1 |

生成中文子集：

```
python -I make_cjk_subset.py sans <放 NotoSansSC-Regular.otf / NotoSansSC-Bold.otf 的文件夹> <mod 文件夹> <输出到本文件夹>
```

需要 `pip install fonttools`。子集里包含 GB2312、JIS X 0208、假名、标点，以及 mod 源码里出现的所有字符；改了界面文字后重新生成一次。
