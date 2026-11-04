# mod/disabled：不参与编译的源码

构建脚本只编译 `mod/src/*.cpp`，这里的文件不会进 dylib / so。

## 发布版（0.34.0）去掉的功能

这些是 0.33.0 时的**完整原文件**，没有删改：

| 文件 | 内容 |
|---|---|
| `unlock/unlock.cpp` | 解锁全部歌曲：运行时打开所有章节、歌曲和 IN/AT（不写存档） |
| `saves/savedata.cpp` | 存档页的全部功能：官方存档导出 / 导入、PhiMe 存档导出 / 导入、全解锁空存档（运行时生成）、首次引导里的全解锁选项，以及还保留的部分（对话框、云存档里的 phime.json、安卓登录提示） |
| `saves/cloud.cpp` | 云端拉取（安卓）：扫码或粘贴凭证后下载最新云存档，再走官方存档导入；以及还保留的 TapTap 登录 |
| `saves/panel.cpp` | 设置面板（0.33.0）：「存档」页的各行按钮、「其他」页的「解锁全部歌曲」开关 |

### 现在还保留的

- 安卓的 TapTap 扫码登录（`src/cloud.cpp` + `src/account.cpp`）：游戏自己的同步、自动同步照常用。
- 云同步时夹带的 `phime.json`（判定详情和课题成绩），换设备下载云存档时合并回来（`src/savedata.cpp`）。
- 对话框、首次引导（改成了说明设置入口和协议）、安卓登录提示。
- Java 部分（`android/picker/src/phime/Cloud.java`）里的拉取任务 `OP_PULL` / `OP_PASTE` 没删，只是不再调用。
- 安卓加载器里用电脑 + adb 搬运官方版本地存档的路子（`android/import-save.ps1`、`android/loader.cpp`）没有动。

### 想恢复时

1. 把 `unlock/unlock.cpp` 放回 `src/`；`saves/savedata.cpp`、`saves/cloud.cpp` 覆盖 `src/` 里的同名文件。
2. `src/mod.h`：
   - `ModConfig` 加回 `bool unlockAll = false;`，以及 `#define PM_HAS_UNLOCK 1`、`bool unlockInstall();`
   - `SAVE_*` 枚举换回旧的：`SAVE_EXPORT_OFFICIAL, SAVE_IMPORT_OFFICIAL, SAVE_EXPORT_PHIME, SAVE_IMPORT_PHIME, SAVE_UNLOCKED, SAVE_CLOUD, SAVE_CLOUD_PASTE, SAVE_OPEN_FOLDER, SAVE_SKIN_IMPORT, SAVE_MASK_PREVIEW`（新的 `SAVE_LICENSE` 留着的话，要在旧 savedata.cpp 的 `saveAction` 里补上它的处理，见现在的 `openLicense`）
   - 加回 `void saveImportCloud(...)` 和 `void cloudPull(bool fromClipboard);`
3. `src/autoplay.cpp` 的 `configLoad` / `configSave` 加回 `PhiMeMod.unlockAll`。
4. `src/main.cpp` 在 `resultInstall()` 后加回 `unlockInstall()`（要在 `savedataInstall()` 之前：两者都 hook `ChapterSelector.Start`）。
5. `src/panel.cpp`：从 `saves/panel.cpp` 把「存档」页（`g_tab == 3` 那段）和「其他」页的解锁开关搬回来，注意现在的页序是 游玩 / 显示 / 其他 / 皮肤 / 关于。

## 更早的

| 文件 | 内容 |
|---|---|
| `wave/` | 练习时间轴的波形（0.16.0 加入，0.17.0 去掉） |
