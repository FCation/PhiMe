# PhiMe Mod（Phigros 4.0.1.2）

一个注入式 mod。目前有：

- **自动游玩**：所有音符走游戏自己的 Perfect 判定，打击音、特效、连击都是原生的
- **自动游玩时屏蔽手动判定**：手指点击和划动都不会产生 Good/Bad，暂停按钮照常可用
- **自动游玩的结算**：结算界面照常显示，但成绩不保存到本地，也不上传云端
  - 评级字母（φ）倒过来显示，上面趴着一只 Clawd：这张谱是 Clawd 帮你打的
  - 原来显示「同步成功 / 同步失败」的地方改为 `Not Saved`
- **选歌界面开关**：原来的 Mirror 按钮换成难度条下方的一排深色半透明控件（和游戏的深色条同一种质感，开着的用 Claude 橙）：「镜像」「练习」「自动」三个开关、「普通 | 课题」分段切换（浅色滑块滑到选中的一边）、带 Clawd 的「PhiMe 小助手」入口（橙色描边）。全部同一高度，居中排在难度条正下方；放不下一行时在三组之间换行（让最宽的一行尽量窄），每行居中。「镜像」调用游戏自己的 `ToggleChartMirror`，「练习」「自动」和「PhiMe 小助手」面板里的开关是同一个设置
- **课题模式判定**（选歌界面「普通 | 课题」切换，默认普通）：普通谱面用游戏课题模式的判定和计分
  - 判定区间：Perfect ±40 ms、Good ±90 ms、Bad ±140 ms，各加半个平均帧时间（和游戏课题模式完全一样）；分数 = 准度 × 1000000，没有连击分
  - 同时开了区分大小 P 时，大 P 区间是这时 Perfect 区间的一半（约 ±20 ms）
  - 结算界面分数上方显示 `COURSE MODE`，BEST 一行显示课题模式自己的最高分；同步那里显示 `Saved, Uploads Next Sync`
  - 选歌界面切到「课题」时，左边歌曲列表的评级（B / A / S …）也按课题模式成绩重画；打课题模式时连击数下面的 COMBO 显示为 COURSE
  - 课题模式成绩存在 `PhiMeMod.course.<歌曲 id>.<难度>`，不进入普通成绩；游戏云同步时随 phime.json 一起上传；选歌界面切到「课题」时显示课题模式成绩，切回「普通」显示普通成绩
  - 课题模式那一局的关卡和结算都在存档沙盒里：普通存档的写入（包括这一局的其他存档变化）都不会保存
  - 游戏自己的课题模式（挑战）不受影响
- **练习模式**：练习时连击数下方显示 PRACTICE，连击不到 3 也一直显示
  - 进入谱面后直接打开选段界面
  - 练习的谱面永远不会结算：不选段直接玩（选段界面点「取消」后继续）到曲末，会回到选段界面
  - 开启后，谱面没在播放时（暂停、开局前、单次选段放完后）屏幕下方中间会出现「选段练习」按钮，点开是选段界面；播放中自动隐藏，避免误触（这个按钮和选段界面的按钮都是直接读触摸，不依赖 EventSystem）
  - 时间轴像剪辑软件一样操作：双指缩放；单指按住 A/B 端点拖动，按住空白处左右平移。拖动端点时背景和面板会渐变淡出，只剩时间轴隐约可见，谱面会跳到端点所在的时间
  - 时间轴里画着这首歌的**波形**：深色主体是响度（RMS），浅色外沿是峰值（按峰值和响度的比例加宽，鼓点会凸出来）。高度按整首歌定标（这首歌第 99 百分位的响度画到约八成高），并用稍微放大差别的曲线，段落和拍子更分明；每一列至少取 12 ms 的响度，相邻几列再平滑一次，边缘不会毛糙。怎么缩放同一段的高矮都不变。第一次打开某首歌时在后台解码（手机上一两秒，边解边显示），之后存在 PhiMeWave 文件夹里秒开。两首加密的特殊曲目没有波形
  - 速度 0.05x–2x，步进 0.05（可以拖滑条，也可以按 −/+）
  - 「AB 循环」开启后无限循环选段；关闭时播完一遍回到选段界面
  - 「变速不变调」默认开启
  - 每次开始、每次循环，分数和判定计数都从零算起；在选段界面点「取消」会把整首谱面重置回开头并回到暂停菜单，练习进度不会带进正常游玩
  - 练习时的成绩不会保存
- **继续前回放谱面**（默认开，设置面板里可关）：暂停后点继续，游戏原本固定等 3 秒、谱面静止不动。开启后改用 mod 自己的「准备时间」（0–3 秒、步进 0.1 秒，默认 3 秒）：这段时间里谱面从「暂停点前 N 秒」开始回放，正好在音乐响起时走到暂停点，方便抓准接下来的第一个音符
  - 准备时间为 0 秒时，点继续立刻开始播放
  - 回放就是真正的播放：音乐也一起倒回去播（从轻到响渐入），谱面全程跟着音乐走，走到暂停点时只是重新打开判定，衔接处不卡
  - 只是回放，不影响判定：还没判定的真实音符原地不动（只是离判定线远一些），回放期间不会被判定；暂停前已经打过的音符临时生成副本，打完自己消失
  - 副本会显示 Perfect 特效和打击音，但分数、连击、判定计数在每次判定后都会还原，不会多出 Perfect 或 Miss
  - 回放期间不判定触摸，暂停按钮照常可用，暂停会立刻结束回放
  - 准备期间盖着半透明遮罩，中间只有按 0.1 秒走的倒计时数字（3.0、2.9 …）；游戏自带的 3 秒倒计时动画会被换掉
  - 开局不到 N 秒就暂停时，只从谱面开头（0 秒）回放到暂停点
  - 练习变速时，回放速度也跟着倍速
  - 关闭这个开关就完全是游戏原版的 3 秒继续
- **准度显示**（默认关，设置面板「显示」页）：打歌时右上角分数下面多一行小字显示 Accuracy，字体用游戏自己的分数字体
  - 计算方式「实时」：只按目前已判定的音符算，`(Perfect + 0.65 × Good) ÷ 已判定数`，还没判定时是 100.00%
  - 计算方式「最高」：之后全部 Perfect 能达到的准度，即 100% 减去已经丢掉的部分，`(物量 − 0.35 × Good − Bad − Miss) ÷ 物量`
  - 两位小数，向下取整（只差一点点也不会显示成 100.00%）
  - 「准度显示为大字」：准度换到原来分数的位置，分数改为下方的小字
- **判定详情**（默认关，同一页）：连击（COMBO 字样）下方一行小字，依次是 Perfect 数（黄）、Good 数（蓝）、Bad + Miss 数（灰），一直显示
- **区分大小 P**（默认关，设置面板「其他」页）：判定完全是游戏原本的；判定偏差在 Perfect 区间一半以内的 Perfect 算大 P（Critical），其余 Perfect 是小 P。普通 ±80 ms → 大 P ±40 ms；课题模式（mod 的和游戏自己的都算）±40 ms + 半帧 → 大 P ±20 ms + 四分之一帧
  - 准度不区分；打歌时显示的分数每个大 P 多 1 分（全大 P 为 1000000 + 物量）。结算、存档、云端里的分数还是游戏原本的，不影响成绩和排名
  - 结算界面的 Perfect 数拆成两个：金色的大 P 数，后面跟一个小一号的浅橘色小 P 数
  - 打击特效：大 P 是在游戏原本金黄特效上调亮的一份，飞出去的方块粒子换成四角星；小 P 是浅橘黄色的一份，Good 还是蓝色。每个音符各自对应，Hold 从头判那一下就按头判偏差变色；Drag / Flick 没有时间偏差，算大 P
  - 判定详情变成四个数：大 P（金黄）、小 P（橘黄）、Good（蓝）、Bad + Miss（灰）
  - 判定线（游戏的 AP / FC 指示开着时）：全大 P 和大 P 特效同色，出现小 P 但没有 Good / Bad / Miss 和小 P 特效同色（特效颜色 = Perfect 特效原色 × 色调，按最亮通道拉到 1），出现 Good 但没有 Bad / Miss 蓝色，之后照游戏原样
- **成绩详情**（选歌界面，一直开着）
  - 游戏原来的「分数 + 准度」那一行换成 mod 自己画的：分数居中在原来那一行的位置，下面一行小字是判定数；准度挪到**当前曲绘的正上方**，用和分数一样的字体、一样大的字显示，旁边一个「详情」按钮（Source Serif 4，垫一块深色胶囊底，「详情」在胶囊右端，四周留同样的边、弧度同心，亮曲绘上也看得清；整组对准曲绘上边的正中（曲绘是斜的平行四边形）；滑动换歌时跟着曲绘走，不会顶到顶栏）；分数居中在难度条右端和评级图标之间。这些画在分数所在的那张画布里，按屏幕像素换算位置；只有摆放成功后才隐藏游戏原来的两段文字，失败时原样保留（日志里 details: song select layout 有原因）
  - 准度显示到四位小数（如 `99.8765%`，截断不四舍五入，不到 100% 不会显示成 100.0000%）。直接读游戏存档里的准度（它本来就是 float），云存档拉下来的成绩一样精确；mod 不改存档
  - 分数下方的小字是这个难度「最高分那一局」的判定数，颜色和打歌时的判定详情一样：Perfect（黄）、Good（蓝）、Bad + Miss（灰）；那一局开着区分大小 P 时是四个数（大 P 金、小 P 橘）
  - 点「详情」或分数打开「成绩详情」卡片（Claude 风格，Clawd 站在卡片上）：顶部切换 EZ / HD / IN / AT；最高分和 AP / FC 标记（全大 P 的 AP 显示 AP+）、最高准度、单曲 RKS、定数；判定详情（大 P / 小 P / Good / Bad / Miss）、Early / Late、最大连击、本机游玩次数
  - 判定数游戏自己不保存（存档里每个难度只有分数、准度、FC 三项，云端也只有这些），所以 mod 在结算时把每个难度「最高分那一局」（同分比准度，再比大 P）的判定记进本机的 JSON：iOS `Documents/PhiMeMod.records.json`，安卓 `/sdcard/Android/media/com.PigeonGames.Phigros/PhiMeMod/records.json`。大 P 数也在里面。云同步时会作为 phime.json 夹带在游戏的云存档里一起上传，换设备下载云存档时会合并回来
  - 当前最高分来自云存档或装 mod 之前（和本机记录对不上）时，小字不显示，卡片会注明，并给出本机打出的最好一局
  - mod 课题模式的成绩单独记录（选歌切到「课题」时显示课题的）；自动游玩、练习、游戏自己的课题模式不记录
- **登录 TapTap（仅安卓）**：改过的包被 TapTap SDK 拒绝登录（`signature not match`，是手机上检查包签名报的错，账号服务器本身不管）。账号页的「登录」换成 PhiMe 的 TapTap 扫码登录，拿到的凭证交给游戏自己的 SDK（`TDSUser.BecomeWithSessionToken`），之后同步、自动同步、冲突提醒都是游戏原本的逻辑（`src/account.cpp` + `src/cloud.cpp`）
  - 第一次会弹出二维码，用另一台设备上的 TapTap 扫码并确认；只有一台手机时点「用 TapTap 打开」或「浏览器」，确认后回到游戏
  - 凭证（sessionToken）只存在游戏私有目录（`files/PhiMeCloud.txt`），下次登录不用再扫码；游戏里「登出」会一起删掉
  - 网络部分是 `android/picker/src/phime/Cloud.java`（和文件选择器在同一个 dex 里），二维码由 `Qr.java` 生成（版本 1–10、纠错 M，已用 zxing 解码验证）。走的是社区工具（PhiLogin、PhigrosLibrary）用的同一套接口，不是官方公开的，TapTap 改了就会失效
  - iOS 没有签名检查，用游戏原本的登录
- **首次引导**：第一次进入章节选择界面时弹出欢迎卡片：设置入口在哪、免费开源禁止商用、支持正版；只出现一次。安卓之后还会提示一次怎么登录
- **发布版去掉的功能**（0.34.0）：存档导入导出、全解锁空存档、解锁全部歌曲、云端拉取。源码原样留在 `mod/disabled/`，见那里的 README
- **快速加载**（默认开，设置面板「其他」页）：进出曲包时那个随机曲绘 + tips 的加载界面不再干等，下一个界面加载好就进入。进入歌曲时的加载不受影响
- **跳过新手教程**：游戏在标题界面点击后，如果 SaveManagement 的 int `IsFirstRun` 还是 0 就进教程（名字是反的，教程打完结算时才写成 1，游戏里跳不过）。mod 在标题界面出现时把 0 改成 1，新装的游戏也不会进教程
- **设置面板**：游戏「设置」列表最下面多出一个「PhiMe 小助手」按钮，点开是 mod 自己的面板。配色跟 Claude 桌面版一致（奶白卡片、细分隔线、Claude 橙点缀），列表式布局方便以后加功能，卡片顶边站着一只会蹦的 Clawd。顶部分「游玩」「显示」「其他」「皮肤」「关于」五页
  - 「关于」：Mod 文件夹（日志在这里）、开源协议 CC BY-NC-SA 4.0（三条条款的说明，「原文」按钮用 `Application.OpenURL` 打开 Creative Commons 的协议页）、尊重官方版权、只为推分不作弊、第三方代码
  - 选歌界面那排「镜像 / 练习 / 自动」下面还有一个更大的「PhiMe 小助手」主入口（奶白底、Claude 橙描边、带 Clawd），点开是同一个面板；在面板里改了练习 / 自动，关掉后上面的按钮会跟着更新
- **语言**（设置面板「其他」页「语言 / Language」）：mod 自己的界面可以切成中文或 English，面板、选歌界面的按钮和「详情」当场切换；游戏设置列表里的「PhiMe 小助手」入口下次进设置时换。英文界面用 Source Serif 4（Adobe，SIL OFL 1.1），含中文的文字自动换成中文字体
- **字体**：mod 的文字由 mod 自己画（`src/glyphs.cpp`，用公有领域的 stb_truetype），不经过 Unity 的字体系统：iPad 上系统中文字体会缺字，从文件打开的字体又是非动态的、画不出字。每个字先用 Source Serif 4（Adobe，SIL OFL 1.1），没有的字（中文、日文）用思源黑体 Noto Sans SC（SIL OFL 1.1，`assets/fonts/make_cjk_subset.py sans` 裁成 GB2312 + JIS X 0208 + 假名 + 源码用到的字，约 2.5 MB，嵌入名 `PhiMeCJK-*`；粗体只含 mod 自己的字，其余用常规体加粗。脚本也能用 `wenkai` 做霞鹜文楷）。一段文字画成一张贴图，用挂在 Text 下的 RawImage 显示，Text 本身保留但内容为空，原来的定位、上色、淡入淡出都照常；一行放不下时自动缩小（最低 55%），对话框正文自动换行。改用游戏字体的文字（打歌 HUD、结算）还是 Unity 自己画。字体用 `.incbin` 编进 dylib / so（`src/assets.cpp`）
- **标题界面**：「点击开始」那一屏的游戏版本号后面加上 `· PhiMe Mod v版本号`
- 开启自动游玩时，游玩界面下方会显示一个小小的 Clawd 和 `CLAWD PLAYING…` 标识

## 构建（Windows）

需要 LLVM（`winget install LLVM.LLVM`），不需要 Xcode 和 iOS SDK：

```bash
powershell -ExecutionPolicy Bypass -File mod\build_ios.ps1
```

产物：`mod/out/PhiMeMod.dylib`（arm64，已 ad-hoc 签名）

## 装进 ipa

用常用的签名工具（Sideloadly、ESign、Feather 等）的「注入 dylib」功能，把 `PhiMeMod.dylib` 注入到 ipa 再签名安装。
不需要越狱：mod 只改数据（IL2CPP 的 `MethodInfo` 指针和 icall 缓存槽），不改代码段，也不依赖 CydiaSubstrate 或 ElleKit。

## 日志

- iOS：`<App 沙盒>/Documents/PhiMeMod.log`，系统控制台里也能搜到 `[PhiMeMod]`
- 安卓：`/sdcard/Android/media/com.PigeonGames.Phigros/PhiMeMod/PhiMeMod.log`（每次启动重写，用 MT 直接打开），或者 `adb logcat -s PhiMeMod`。加载器自己的几行（`loader: ...`）只在 logcat 里

正常启动应能看到：

```
hooked icall UnityEngine.PlayerPrefs::TrySetSetString(...)   ← 存档沙盒就绪
init: unity=1 autoplay=1 panel=1 result=1
settings entry added                         ← 打开设置页后
hook alive: NoteUpdateManager.Update         ← 进入一首歌后
autoplay: dropping save writes (SetString)   ← 自动游玩结算时，成绩写入被拦下
result: rating flipped, Clawd added
save sandbox ended (N writes were dropped)   ← 回到选歌等其他界面后
```

如果看到 `ERROR: save sandbox unavailable`，说明存档拦截没装上，先别用自动游玩，把日志发回来。

## 原理（方便以后跟版本）

- 所有类、方法、字段都在运行时按名字查找（`il2cpp_class_from_name` 等）。游戏更新后只要这些名字没变就能继续用，不依赖偏移。
- Hook 方式：替换 `MethodInfo->methodPointer`。Unity 调 `Update/Start`、跑协程时走 `il2cpp_runtime_invoke`，每次都会现读这个指针。
- 回调：伪造一个 `MethodInfo` 交给委托的构造函数，得到能回调原生函数的正常委托。
  - 按钮点击用 `UnityAction`
  - 每帧回调用 `Canvas.willRenderCanvases`（在动画之后、渲染之前触发）
- 自动游玩：在 `NoteUpdateManager.Update` 里，赶在各音符执行 `Judge()` 之前置上游戏自己的判定标志：
  - Click / Hold 头：`ChartNote.isJudged`
  - Hold 持续：`_safeFrame`
  - Drag：`DragControl.isJudged`
  - Flick：`ChartNote.isJudgedForFlick`
- 屏蔽手动判定：`JudgeControl.Update` 会把「刚按下」的手指交给 `CheckPause` 和 `CheckNote`，把划动中的手指交给 `CheckFlick`。
  自动游玩时先替它调用 `CheckPause`，再临时把手指的 `phase` 和 `isNewFlick` 藏起来，原函数跑完马上还原。
- 存档沙盒：游戏的所有存档（成绩、解锁、数据）都经 `SaveManagement` 加密后写进 `PlayerPrefs`，云同步上传的也是这份数据。
  - 拦截点：`PlayerPrefs` 的写入 icall（`TrySetSetString` / `TrySetInt` / `DeleteKey` / `DeleteAll`）。IL2CPP 把这些 icall 解析后的函数缓存在可写的数据槽里，mod 从包装函数的 `adrp/add/ldr` 指令解码出槽地址，再换成自己的函数。
  - 生效范围：自动游玩那一局的关卡场景和它的结算场景（重试也算）。在这些场景里的写入一律丢弃；一旦在其他场景发生写入，沙盒自动结束。
- `Not Saved`：结算页的 `AutoSyncControl.Start` 不执行云同步，改为把文字设成 `Not Saved`，再按原来的动画触发 `Show` 和 `Failed`。
- 倒立 φ：结算页每帧找到 `LevelOverControl.ratings` 里当前显示的评级，转 180°，并挂一只（反向转回来的）Clawd 当子物体。
- 练习模式：
  - **时钟**：`ProgressControl.nowTime = 音频封装.time - _offset`，而这个 `time` 是由 DSP 时钟推出来的，不会跟着 `pitch` 变。所以练习时在 `AddressableAudioSource.Update` 之后，用「实际帧时间 × 倍速」得到的平滑时钟（会慢慢向 `AudioSource.time` 校正）把 `time` 覆盖掉。
  - **跳转**：游戏开局第一帧就把所有音符生成好，判定后销毁。所以跳转的做法是：
    1. 销毁场上剩下的音符
    2. 对选段里的音符调用 `JudgeLineControl.CreateNote` 重新生成
    3. 把判定线事件索引（只会往前扫）和 `JudgeControl` 的触摸判定窗口拨回去
    4. 用 `AddressableAudioSource.Seek` 让音频对齐，同时同步 `ProgressControl.audioTime`（游戏恢复播放时 `PlayScheduled` 用的就是它）
  - **变速**：`AudioSource.pitch = 倍速`。
  - **不变调**：在音乐对象上挂 UniTask 的 `AsyncAudioFilterReadTrigger`，hook 它的 `OnAudioFilterRead`，在音频线程里用颗粒合成（两个 Hann 窗、1/倍速读取速率）把音高拉回原调（`src/pitch.cpp`）。低速时会有一点颗粒感。
  - **暂停菜单**：就是 `ProgressControl.pauseBar`。打开选段界面时把它藏起来，同时设置 `stopJudge`，避免触摸误判。
  - **课题模式判定**（`course.cpp`）：游戏的 `ProgressControl.Update` 每帧按 `_inChallengeMode` 设置 `JudgeControl` 的静态值（课题：`InChallengeMode = true`，Perfect / Good / Bad = 0.04 / 0.09 / 0.14 + 最近 10 帧平均帧时间的一半；普通：false，0.08 / 0.18 / 0.22）；`ScoreControl.Update` 在 `InChallengeMode` 时用 (Perfect + 0.65 Good) / 物量 × 1000000 计分。我们在 `ProgressControl.Update` 之后照它的课题分支写这些静态值，判定和分数都由游戏原本的代码算。选歌界面的分数由 `SongSelector.SetText` 从 `SongSelectorItem.records`（`List<LevelRecord {s, a, c}>`，一个难度一项）里读，所以把这个 List 的底层数组 `_items` 换成课题记录做成的新数组，切回普通时放回原数组，再调用 `SetText` 刷新。歌曲 id 和难度在开局时从 `GameInformation._main.levelStartInfo` 读下来；`LevelOverControl.Start` 是协程，`_levelStartInfo` 要等它跑起来才有，所以结算记录在之后一两帧再取（优先结算界面自己的，取不到用开局记下的），绕过沙盒写入自己的键，并持续把 BEST 一行改成课题模式的。游戏的普通成绩只存在 PlayerPrefs 里（`GameInformation.GetSongRecord` / `SetSongRecord`），沙盒拦下写入后普通成绩不受影响。
  - **噪域（Petrichor）**：噪域方块（`PreviewBlockControl`）过了消失时间就会自我销毁。练习跳转时，如果跳到比之前播放到的更早位置，而且确实有方块已经没了，就调用 `PreviewElementUpdateControl.DestroyAndCreateAllBlocks`（关卡开始时 `LevelControl` 用的同一个方法）重新生成全部方块；每次重新开始还会调用 `ScoreControl.UnblockCombo`，解除碰到噪域时被锁住的连击。
  - **不结算**：`ProgressControl.Update` 在 `nowTime` 超过曲长 + 0.2 秒时置 `levelInformation.levelOver`，3.5 秒后启动 `LevelOver` 协程。练习时在曲末前 0.15 秒就暂停并打开选段界面，走不到那一步。
- 继续前回放谱面（`src/resume.cpp`）：
  - **检测继续**：`Play(true)` 被编译后的代码直接调用，hook 不到。但它会把 `_startTime` 置为 -1，下一次 `ProgressControl.Update` 才换成「现在 + 3 秒」，所以在 `Update` 的 hook 里（原函数之前）看到 -1 就知道刚点了继续（开局那次有 `_isFirstTimeLaunch` 标记，跳过）。
  - **接管准备时间**：音乐直接倒回「暂停点 − N」（`audioTime` / `AddressableAudioSource.startTime` 写成那个位置，先置 `_playPrepared` 挡住游戏自己「提前 1 秒」的排程，再调 `PlayScheduled`），`_startTime` 设为音乐开始的时刻。之后 `nowTime` 照常由游戏从音乐算出来，回放和正式游玩是同一个时钟，走到暂停点时不需要切换。回放期间每帧调 `set_Volume` 做渐入，结束时恢复游戏设的音量。暂停菜单生成的倒计时动画（`Pause.instantiatedContinueAnimation`）直接销毁，换成自己的 0.1 秒倒数。
    - v0.8.0 用帧时间驱动回放、音乐开始时再交给音乐时钟，两边对不齐到一帧以内，衔接处会顿一下，所以改成了现在这样
    - 暂停点前不足 N 秒（刚开局）时，谱面先停在开头等一会，再从开头连音乐一起播；N = 0 或没有可回放的内容时，音乐从暂停点直接（或等 N 秒后）开始
    - 回放中再次暂停：谱面和音乐位置都退回暂停点，下次继续照样从这里倒回
  - **真实音符不动**：游戏开局就为每个音符建好对象、判定后才销毁，所以还没判定的音符都还在 `NoteUpdateManager` 里。`nowTime` 早于暂停点时它们只是离判定线远些，不会提前或延后判定，回放期间完全不碰它们。
  - **回放**：暂停点前 N 秒内已经没有对象的音符（暂停前打过的）调用 `CreateNote` 生成副本，按自动游玩的方式打成 Perfect，打完由游戏自己销毁。
  - **不计分**：`ScoreControl` 的所有值类型字段（用 `il2cpp_class_get_fields` 自动列出）和它持有的 `List` 长度，在每次 `NoteUpdateManager.Update` 前后存档、还原。
  - **结束**：谱面走到暂停点（或再次暂停）时，只需移走还没打到的少量副本（`List.RemoveAt` + 销毁），恢复 `ChartNote` 判定标志和 `stopJudge`。没有大批对象的开关，也不换时钟。
- 准度显示 / 判定详情（`src/hud.cpp`）：
  - `ScoreControl.Update` 每帧从计数器（`perfect` / `good` / `bad` / `miss`）重写分数和连击文字，准度公式是 `(perfect + 0.65 × good) ÷ levelInformation.numOfNotes`。mod 在它之后运行，只改自己的文字（交换时再改分数文字）。
  - 小字是游戏 `score`、`comboText` 这两个 `Text` 的子物体，跟着它们的位置和动画走，随关卡一起销毁。位置按宿主文字的对齐方式、字号和行高（分数文字的 `preferredHeight ÷ fontSize`）算出「字形那一行的底边」，贴在下面。日志里每局有 `hud: ...` 行记录尺寸，方便微调。
  - 字符串只在数值变化时重新生成；判定详情用富文本上色，颜色标签里带着连击文字的透明度，跟着游戏的淡入淡出。
  - 交换模式：先把游戏这一帧写的分数字符串挪给下方小字，再把分数文字换成准度。
  - 回放期间分数计数每帧还原，所以显示的也是暂停时的数。
- 区分大小 P（`src/critical.cpp`）：
  - 判定都在 `NoteUpdateManager.Update` 里：`ClickControl.Judge` 的偏差是 `noteInfor.realTime − ProgressControl.nowTime`，小于静态 `JudgeControl.PerfectTimeRange` 为 Perfect，调用 `ScoreControl.Perfect(code, −偏差, 位置, false)`，在音符处 `Instantiate(ScoreControl.perfectJudge)`；Hold 把头判偏差存在 `_judgeTime`，结束（`judgeOver`）时 `Perfect(…, _judgeTime, …, isHold)`，持续火花用它自己的 `perfect` 预制体。
  - `NoteUpdateManager.Update` 的流程：先对每条判定线 `UpdateInfo`，再依次处理 Click / Drag / Hold / Flick 列表：可见性裁剪、`NoteMove()`，2 秒内到时间的音符调 `Judge()`，返回 true 就销毁并移出列表。
  - `ScoreControl.Perfect` 和各 `Judge()` 都是直接调用，hook 不到；`_noteCodes` 里的音符编码是百万级的 float，小数部分（偏差）已经丢了。所以 mod 自己调用游戏原本的 `NoteUpdateManager.Update`，分一到两遍：
    - 第一遍：照常全部更新，用调亮的金黄 Perfect 特效；只是这一帧会成为小 P 的点击（偏差在半个 Perfect 区间到 Perfect 区间之间，算法和 `ClickControl.Judge` 一样）先把触摸标志收起来
    - 第二遍（只有这一帧有小 P 点击时才跑）：标志放回，Drag / Hold / Flick 列表临时清空，`ScoreControl.perfectJudge` 换成橘黄那份，于是只判这些点击。它们都在 Perfect 区间内，第一遍不会被判 Miss；`NoteMove` / `UpdateInfo` 只是用同一个时间再跑一次
    - 头判即将进来的 Hold，在更新前就换好对应的 `perfect`（火花）预制体，第一下火花颜色就对
    - 大 P 数 = 第一遍新增的 Perfect − 以小 P 头判结束的 Hold；第二遍的全是小 P
  - 特效颜色：从 Perfect 预制体复制两份（大 P 调亮、小 P 橘黄），换上染色后的材质（`_Color` / `_TintColor` 乘上色调，大于 1 就是调亮），日志里每 15 秒有一行 `critical: N Critical / M Perfect ...` 统计，方便排查。复制品放在未激活的父物体下，它本身不运行，游戏复制出来的才运行。`Renderer.set_sharedMaterial` 在 4.0.x 被裁掉了，用的是 `set_material`。
  - 另外 hook 了 icall `UnityEngine.Object::Internal_CloneSingle`（所有 `Instantiate` 都走它），点击特效在复制那一刻决定用哪份：第一遍复制游戏原版 Perfect 特效时换成金色那份（我们的两份原样放行，所以 Hold 小 P 头的橘色不受影响）；第二遍里这一家一律换成橘黄那份。游戏可能在关卡第一帧之后才把 Perfect 预制体交给 `ScoreControl.perfectJudge`，所以没拿到时每帧再找一次。每关前几次复制会把材质 `_Color` 和 SpriteRenderer 颜色记进日志（刚生成时和 0.15 秒后各一次），用来看特效脚本有没有把颜色改回去。
  - 判定线颜色在 `JudgeLineControl.UpdateInfo`（只由 `NoteUpdateManager.Update` 调用）里设置，mod 在它之后按大小 P 状态重设 RGB（保留透明度），显示图片的判定线不动。
  - 分数：打歌时的分数文字改成 `round(_score) + 大P数`，`_score` 本身不改。
- 成绩详情（`src/details.cpp`）：
  - 存档里的成绩是 `LevelRecord {s, a, c}`（分数 int、准度百分比 float、FC int）。`LevelRecord.UpdateRecord` 对三项各取最大（`Math.Max(s, IntScore)`、`Math.Max(a, percent)`、`Math.Max(c, FullCombo)`），所以最高分和最高准度可能来自不同的两局；卡片里注明了这种情况
  - 结算的 `LevelResultInfo` 有 `perfect / good / bad / miss / early / late / maxCombo`。`LevelOverControl.Start` 之后一两帧（等协程把 `_levelStartInfo` / `levelResultInfo` 填好，1.5 秒后改用 `GameInformation._main` 里的）读出来，大 P 数由 `critResultCount` 给出（只认这一局确实跑了区分大小 P、Perfect 数对得上的）
  - JSON 用自己写的小解析器读（多出来的字段会跳过），写的时候先写 `.tmp` 再 `rename`；读不完整的文件改名为 `.bad` 留着，能读出的部分照用
  - 选歌界面：`SongSelector.SetText` 从 `SongItems[nowSong].records[nowLevel]` 把分数写成 `D7`、准度写成 `a.ToString("F2") + "%"`。mod 每帧检查准度文字：是这条记录的两位小数时换成四位小数（字符串缓存，不重复分配），不是就不动；准度文字改成不换行
  - 判定小字是准度 `Text` 的子物体（位置算法同打歌时的小字），点击区是分数 / 准度字形范围（按最长内容估宽度，不用整个 Text 框，免得盖住旁边的按钮）上的透明按钮
  - 卡片在自己的 Overlay Canvas 上，数据在打开时读一次
- 云存档里的 PhiMe 数据（`src/savedata.cpp`）：
  - 游戏同步时把 5 个存档模块写进 `<temporaryCachePath>/save/`，再把整个文件夹打成 zip 上传；下载时解压到清空的 `/save/`，`LoadFromFolder` 只读那 5 个模块名。mod 在 `GameKeySaveModule.Serialize`（`SaveToFolder` 最后序列化的模块，接口调用，所以改 vtable）时往 `/save/` 里写一份 `phime.json`（判定详情 + 课题成绩），它就跟着云存档一起上传；原版游戏会忽略它
  - 下载云存档时游戏会 `PlayerPrefs.DeleteAll()`，这会清掉 mod 存在 PlayerPrefs 里的设置和课题记录：`result.cpp` 的 DeleteAll hook 调 `saveWipe`，调用前按歌曲列表（`GameInformation._main.song.AllSongsBypassLimitedAndOthers`）读出所有 `PhiMeMod.course.*`，调用后连同设置一起写回，再把解压出来的 `phime.json` 合并进来（每个难度保留更好的那一局）
  - 以前的存档导入导出、全解锁空存档（官方存档的 5 个模块、`CloudSaveManager.SaveToFolder / LoadFromFolder`、钥匙表、剧情标志的写法）都在 `mod/disabled/saves/savedata.cpp` 的注释里
  - 系统文件选择器（`src/filepicker.cpp`，现在用于导入皮肤包）：iOS 用 `UIDocumentPickerViewController`（`initForOpeningContentTypes:asCopy:` / `initForExportingURLs:asCopy:`），通过 dlsym 到的 ObjC 运行时直接调用，委托类在运行时创建。安卓用存储访问框架（`ACTION_OPEN_DOCUMENT` / `ACTION_CREATE_DOCUMENT`）：结果只能回到 Activity / Fragment，所以 mod 里带了一个很小的 dex（`android/picker`，`phime.Picker` 是一个无界面 Fragment），用 `InMemoryDexClassLoader` 加载（安卓 8+），不用改 manifest，推送新 .so 就能用。JavaVM 从加载器导出的 `PhiMe_JavaVM` 拿（新打的包），旧包退回 `JNI_GetCreatedJavaVMs`（安卓 12+ 公开）。选择结果在下一帧交给调用方
- 快速加载（`src/loading.cpp`）：进出曲包走的是 `Loading` 场景，它的时间几乎都花在固定等待上：
  - `LoadImage` 协程显示随机曲绘后先 `WaitForSeconds(0.9 + 随机 0–0.6)`，之后才开始 `LoadSceneAsync`。mod 在 `MoveNext` 返回后、Unity 读取之前，把这个 `WaitForSeconds.m_Seconds` 改成 0.1 秒。
  - `OnGUI` 在异步加载到 0.9（加载完、等待激活）后，记 `startTime = realtimeSinceStartup`（仅当 `IsZero(startTime)`），再等满 1 秒才 `allowSceneActivation`。mod 在 `Loading.Start` 后把 `startTime` 设成很久以前，加载完就立刻激活。
  - 最后 0.67 秒的转场动画（`LoadOver`）保留。
  - 进入歌曲是另一条路（`SongSelector.StartAnimation` 里固定等 4 + 1 秒后 `LoadScene`），没有改。
- 字体：按「我们用到的汉字有几个能显示」给候选字体打分（`Font.HasCharacter`），依次试系统中文字体（`CreateDynamicFontFromOSFont`：PingFang、Noto CJK 等）、游戏已加载的全部字体（`Resources.FindObjectsOfTypeAll`）、Unity 内置字体，取分最高的。日志里有一行 `font: ...` 记录选中的字体。
  - 一个动态字体把正在显示的所有字形（每种字号、粗细各一份）放在同一张贴图里。iPad 的画布缩放大（约 1.5–1.9 倍），字形比手机上大好几倍，贴图会装满，装不下的字 Unity 直接不画（iPhone 正常、iPad 缺字就是这个）。所以选中系统字体时，每种「字号 + 粗细」分到同一系统字体的几个独立实例之一（最多 6 个），各有自己的贴图；日志里 `font: own texture #N for ...` 记录新开的实例。
- 配置存在 `PlayerPrefs`：`PhiMeMod.autoplay`、`PhiMeMod.practice*`、`PhiMeMod.resumePreview`、`PhiMeMod.resumePreviewTenths`（准备时间，0–30，单位 0.1 秒）、`PhiMeMod.accShow`、`PhiMeMod.accMode`（0 实时 / 1 最高）、`PhiMeMod.accSwap`、`PhiMeMod.judgeShow`、`PhiMeMod.fastLoad`、`PhiMeMod.critical`、`PhiMeMod.welcomed`（首次引导已看过）、`PhiMeMod.loginGuide`（安卓登录提示已看过）

## 安卓（arm64-v8a）

和 iOS 是同一套源码，用同一个 LLVM 编译，不需要 NDK。系统库只在链接时用自动生成的桩，运行时用手机自己的。

**原理**：只改一次 apk，以后只推一个几十 KB 的 `libPhiMeMod.so`。
- 改包时把原版 `libmain.so` 改名为 `libmain_orig.so`，换上 mod 的加载器 `libmain.so`。
- 游戏启动时，加载器先加载 mod，再把 `JNI_OnLoad` 原样转发给原版，Unity 照常启动。
- mod 的加载顺序：
  1. 你推到 `/sdcard/Android/data/com.PigeonGames.Phigros/files/PhiMeMod/libPhiMeMod.so` 的版本（先复制到游戏私有目录再加载，因为 sdcard 不允许执行代码）
  2. 没推过就用 apk 里自带的那份

**第一次：改包和安装**（游戏本体更新后也要重做一次）

```bash
powershell -ExecutionPolicy Bypass -File mod\android\repack.ps1 -Apk com.PigeonGames.Phigros.apk
```

产物：`mod/out/android/Phigros-PhiMeMod.apk`。脚本会编译、重打包（只拷原始数据，不重新压缩）、对齐、签名，最后校验。

签名用的是 `mod/android/phimemod.keystore`，密码在 `keystore.local.txt`。**这两个文件要留好**：以后重新改包时用同一个证书签名，新包才能覆盖安装、保留数据。

因为签名和官方版不同，第一次安装前必须先卸载官方版，而**卸载会清掉本地存档**：
1. 先在游戏里同步一次云存档
2. 卸载官方版，安装改过的包
3. 在账号页点「登录」（PhiMe 的 TapTap 扫码登录），再同步，把云存档拉回来

**搬运官方版的本地存档（电脑 + adb，不需要登录）**

不想登录时，也可以把官方版的本地存档（PlayerPrefs）直接搬过来：

1. 装上**官方版**，登录并同步，让存档出现在本地，然后关掉游戏
2. 导出存档。手机上会弹出确认，选「备份我的数据」，**密码留空**：

   ```bash
   powershell -ExecutionPolicy Bypass -File mod\android\import-save.ps1 -Backup
   ```

   存档会保存在电脑上的 `mod/android/save/`，同时留一份带时间戳的备份。
3. 卸载官方版，安装 `Phigros-PhiMeMod.apk`
4. 导入：

   ```bash
   powershell -ExecutionPolicy Bypass -File mod\android\import-save.ps1 -Import
   ```

   游戏会自动重启。加载器在 Unity 读取存档之前把它放好，覆盖前会先把当前存档备份到 `files/PhiMeMod/playerprefs.before-import.xml`。

几点说明：
- 在改过的包里正常游玩产生的进度只留在本机，不会上传云端。
- 官方版那边有了新进度，想再搬过来时，重复上面的步骤即可。
- 如果 `-Backup` 得到的是空文件，检查一下手机的开发者选项。部分系统需要额外打开「USB 调试（安全设置）」之类的开关，备份才会生效。

**纯手机更新（MT 管理器等文件管理器）**

1. 给 MT 打开「所有文件访问权限」
2. 把新的 `libPhiMeMod.so` 放到 `/sdcard/Android/media/com.PigeonGames.Phigros/PhiMeMod/`（目录不存在就新建）
3. 在系统设置的应用信息里「强行停止」Phigros，再重新打开
4. 打开「PhiMe 小助手」，标题右边的版本号就是当前加载的 mod 版本

导入存档也可以这样做：把存档 xml 放到 `/sdcard/Android/media/com.PigeonGames.Phigros/PhiMeMod/import/`。

`Android/media` 不像 `Android/data` 那样被系统锁住，各安卓版本的文件管理器都能写入。加载器会同时检查 `media` 和 adb 用的 `data` 目录，取修改时间最新的那份。

**以后：更新 mod（电脑 + adb）**

```bash
powershell -ExecutionPolicy Bypass -File mod\android\push.ps1 -Log
```

脚本会编译、推送到手机、重启游戏，然后跟着看日志（`adb logcat -s PhiMeMod`）。正常会先看到：

```
loader: loaded pushed mod ...      ← 用的是你刚推的版本
init: unity=1 autoplay=1 panel=1 result=1
```

如果显示 `loaded bundled mod`，说明推送的文件没被读到，用的是 apk 里自带的那份。

**改包工具**

| 文件 | 内容 |
|---|---|
| `build_android.ps1` | 编译 `libPhiMeMod.so` 和加载器 `libmain.so` |
| `android/loader.cpp` | 加载器源码 |
| `android/ApkRepack.cs` | 流式重打包：原样拷贝压缩数据、改名/新增条目、按 zipalign 规则对齐、去掉旧签名 |
| `android/repack.ps1` | 一次性改包和签名 |
| `android/push.ps1` | 日常推送、重启、看日志 |
| `android/import-save.ps1` | 用 adb backup 导出官方存档，再导入改过的包 |
| `android/AbExtract.cs` | 从 adb 备份（.ab）里取出存档文件 |

需要的工具（已装在这台电脑上）：
- LLVM
- JDK 21（`winget install Microsoft.OpenJDK.21`）
- adb（`winget install Google.PlatformTools`）
- Android build-tools（放在 `%LOCALAPPDATA%\PhiMeTools\bt`，提供 apksigner 和 zipalign）

## 文件

| 文件 | 内容 |
|---|---|
| `src/main.cpp` | 入口：等 IL2CPP 初始化后装 hook，日志 |
| `src/il2cpp.*` | IL2CPP API 解析、按名字查找、方法 hook、icall hook、委托 |
| `src/unity.*` | Unity/UGUI 封装、运行时画贴图（圆角、圆、像素 Clawd）、每帧回调 |
| `src/autoplay.cpp` | 自动游玩 + 屏蔽手动判定 + CLAWD PLAYING… 标识 |
| `src/result.cpp` | 存档沙盒 + `Not Saved` + 倒立 φ 和 Clawd |
| `src/practice.cpp` | 练习模式核心：跳转、循环、变速时钟、暂停菜单按钮、镜像旁的开关 |
| `src/practice_ui.cpp` | 选段界面：时间轴手势、倍速条、开关、波形 |
| `src/wave.cpp` | 波形：用 Addressables 找到歌曲的 bundle 文件，后台线程解码，缓存到 PhiMeWave |
| `src/wave_decode.cpp` | 波形解码：UnityFS（LZ4）→ FSB5 → 补上 Vorbis setup 头（`vorbis_setups.inc`，FMOD 只留了 CRC32）→ Ogg → stb_vorbis，每 128 个采样一个峰值 / RMS；还有画波形的函数。`test/wave_test.cpp` 可以在电脑上对 APK 跑同一套代码 |
| `src/pitch.cpp` | 实时变调器（变速不变调） |
| `src/resume.cpp` | 继续前回放谱面 |
| `src/hud.cpp` | 打歌时的准度显示和判定详情 |
| `src/loading.cpp` | 进出曲包的快速加载 |
| `src/critical.cpp` | 区分大小 P：计数、特效颜色、判定线颜色 |
| `src/details.cpp` | 成绩详情：四位小数准度、判定小字、详情卡片、本机 JSON 记录、PhiMe 存档文件 |
| `src/savedata.cpp` | 云存档里的 phime.json、对话框、首次引导、安卓登录提示、面板按钮 |
| `src/filepicker.cpp` | 系统文件选择器（iOS UIDocumentPicker / 安卓 SAF），打开 Mod 文件夹 |
| `android/picker/` | 安卓的 Java 部分：文件选择器 `Picker`、TapTap 登录 `Cloud`（里面的云端拉取已不再调用）、二维码 `Qr`（构建时编译成一个 dex，生成 `picker_dex.inc`） |
| `src/cloud.cpp` | TapTap 扫码登录（安卓）：JNI 调 `phime.Cloud`、二维码卡片，凭证交给 `account.cpp` |
| `src/account.cpp` | 游戏自己的账号登录（安卓）：账号页「登录」走 PhiMe 的扫码登录 |
| `src/jni_lite.h` | 安卓 JNI 小工具（按函数表槽位调用，不需要 jni.h） |
| `src/panel.cpp` | 设置入口 + 设置面板（含语言切换、「关于」页） |
| `disabled/` | 发布版去掉的功能的源码（不参与编译），见 `disabled/README.md` |
| `src/title.cpp` | 标题界面版本号后加 PhiMe 版本 |
| `ios/libSystem.tbd` | 免 SDK 链接用的 libSystem 桩 |
