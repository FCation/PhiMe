# PhiMe Mod — guide for Codex / other agents

PhiMe is a native mod for **Phigros 4.0.1.2** (Unity, IL2CPP). It is one C++ library built two ways: an iOS arm64
dylib (injected into the IPA) and an Android arm64 `.so` (loaded by a replacement `libmain.so`). It hooks the game
through the IL2CPP runtime API at runtime. There is no game source and no il2cpp dump: names come from
`global-metadata.dat` (see "Finding names" below) and are checked at runtime, with every result logged.

The user talks in Chinese and tests on an iPad (iOS) mostly; they send `PhiMeMod-least.log` (this launch only).

## Build, test, package (Windows, PowerShell)

| What | Command | Output |
|---|---|---|
| iOS dylib | `powershell -ExecutionPolicy Bypass -File mod\build_ios.ps1` | `mod\out\PhiMeMod.dylib` |
| Android .so | `powershell -ExecutionPolicy Bypass -File mod\build_android.ps1` | `mod\out\android\libPhiMeMod.so` (+ `libmain.so`) |
| skin pack parser tests | `powershell -ExecutionPolicy Bypass -File mod\tests\build_skinpack_test.ps1` | 104 cases |
| audio decoder tests | `powershell -ExecutionPolicy Bypass -File mod\tests\build_audiodec_test.ps1` | 52 cases |
| install IPA | `mod\ios\package_ipa.ps1 -Ipa games.Pigeon.Phigros-4.0.1.2.ipa` | `mod\out\ios\Phigros-PhiMeMod.ipa` |
| install APK | `mod\android\repack.ps1 -Apk com.PigeonGames.Phigros.apk` | `mod\out\android\Phigros-PhiMeMod.apk` |

- Toolchain: LLVM at `C:\Program Files\LLVM\bin` (clang++, ld64.lld, ld.lld). No Xcode, no NDK.
- Both build scripts compile **every `mod/src/*.cpp`** (not subfolders). `mod/src/third_party/stb_vorbis.c` is
  `#include`d by `audiodec.cpp` only.
- After every build bump `PM_VERSION` / `PM_VERSION_NUM` in `mod/src/mod.h` (e.g. "0.32.1" / 3201) and check the
  new version string is in the dylib. The user installs the dylib + .so by hand.
- Keep `mod/android/phimemod.keystore` + `keystore.local.txt`: APKs must be signed with it to update in place.

## Hard constraints (the build fails or the game crashes otherwise)

- **Freestanding C++17**, `-fno-exceptions -fno-rtti`, **no STL, no system headers**. `mod/src/platform.h` declares
  the only libc functions available. A new libc/libm function must be declared AND added to BOTH stub lists:
  `mod/ios/libSystem.tbd` (`_name` in the symbols list) and `mod/build_android.ps1` (`$stubs`, `libc.so`/`libm.so`).
  Missing → "undefined symbol" at link time (e.g. `unlink`, `qsort` were added this way).
- Prefer `__builtin_fabsf`, `__builtin_sqrtf` etc. Use `snprintf`, `memcmp`, `strlen`; there is no `strncmp`,
  `atoi`, `strtod`.
- **Hooks never patch code** (iOS is not jailbroken):
  - `hookMethod(MethodInfo*, fn)` only catches calls made *through the MethodInfo*: Unity messages (Start,
    Update), coroutines (`<Name>d__N.MoveNext`, find with `findNestedPrefix(cls, "<Name>d__")`), delegates,
    reflection. A direct C# call (inlined / `bl`) is NOT caught.
  - `hookIcall(wrapper, "Ns.Class::Method(args)", fn)` for engine internal calls (e.g. `Object::Internal_CloneSingle`
    = every Instantiate, used by critical.cpp + skin.cpp).
  - `hookVirtual(cls, method, fn)` patches vtable slots.
  - A static delegate can be "hooked" by replacing its `invoke_impl` (hitsound.cpp: `SEPlayer.PlayHitFx`).
- Everything Unity runs on the main thread. Per-frame work goes in `onEveryFrame(cb)` (Canvas.willRenderCanvases:
  after animation, before rendering — writes there win over the game's animators). Max 32 callbacks.
- Unity objects can die any time: always `alive(obj)` before use; hold managed objects across frames with
  `Handle` (GC handle), never raw pointers.

## Lessons that cost a version each (read before touching the area)

1. **Many 4.0.x methods are stripped**: e.g. `Renderer.set_sharedMaterial`, `Renderer.get_material`. Resolve with
   `findMethod` at install, log "method not found", and have a fallback. `Renderer.set_material` exists.
2. **Objects we create are unloaded on scene change** (`UnloadUnusedAssets`) unless `set_hideFlags(32)`
   (DontUnloadUnusedAsset): textures, sprites, materials, audio clips. Symptom: magenta materials / empty sprites
   in the 2nd level.
3. **Lists are empty in the first frame of a level** (judge lines' `notesAbove/notesBelow`, `speedEvents`): read
   them later or retry until non-empty.
4. **Game sprites/assets are reloaded per song** (new objects, same look): maps keyed by object pointer must be
   reset per level (skin.cpp `g_map`), caches keyed by content (`g_si`).
5. **Static vs instance fields**: check `il2cpp_field_get_flags(f) & 0x10` before `il2cpp_field_static_get_value`.
   Reading an instance field as static returns garbage → crash (skinsound.cpp, SEPlayer tables).
6. **Never call `gameObject()` on an arbitrary UnityEngine.Object** (materials, meshes): crash → black loading
   screen. In the clone hook compare pointers first.
7. **Colours in Phira skins are often 0xRRGGBB** (alpha byte 0): treat alpha 0 as opaque.
8. ProgressControl.Update starts the result screen (`<LevelOver>d__47`) whenever chart time reaches the end, even
   paused: practice.cpp blocks that coroutine in practice levels.
9. `nowChallengeIndex` already points at the next song when a challenge level starts.
10. **A level scene has no EventSystem**: `onClick` buttons never fire in a level. In-level UI reads `touches()`
    itself (practice entry, SKIP, the hide bar preview's handle and 确认). Song select / settings / results do have one.

## Source map (`mod/src`)

| File | Feature | Notes / entry points |
|---|---|---|
| main.cpp | init, logging, version claim | `pm_log` → `Documents/PhiMeMod.log` + `PhiMeMod-least.log` (iOS) or `/sdcard/Android/media/<pkg>/PhiMeMod/` |
| il2cpp.cpp/.h, il2cpp_api.inl | IL2CPP API, hooks | `findClass`, `findMethod(k,name,argc,firstParamType)`, `findMethodSig`, `fieldOffset`, `invokeSafe/invokeObj`, `Handle` |
| unity.cpp/.h | UI + engine helpers | `overlayCanvas`, `image`, `text`, `onClick`, `touches`, `screenToCanvas`, `worldBounds`, `scrollArea` ... |
| autoplay.cpp | autoplay + config | `configLoad/configSave` (all `PhiMeMod.*` prefs), `hk_PC_Start` = level start: calls every `*LevelStart` |
| panel.cpp | settings panel (5 tabs) | 游玩 / 显示 / 其他 / 皮肤 / 关于 (license + copyright); `toggleRow`, `actionRow` (buttons → `saveAction(SAVE_*)`), `para` (wrapped text) |
| practice.cpp, practice_ui.cpp | practice mode, song-select dock | `reseed` (recreate notes at t), `practiceStart`, result guard, the 镜像/练习/自动 dock |
| resume.cpp | replay before resuming | |
| hud.cpp | in-level accuracy + judgement counts | |
| critical.cpp | 大小 P (Critical Perfect) | always counted; `critEnabled()` = shown. Two-pass NoteUpdateManager update; clone hook; `paintLines` |
| result.cpp, resacc.cpp, rks.cpp | result screen tweaks | 4-dec accuracy, [分数/ACC] switch, RKS count-up |
| details.cpp | judgement records, song select overlay, details card | records.json, best = highest accuracy |
| course.cpp | PhiMe course mode | |
| challenge.cpp | game's own challenge mode: per-song mirror | EnterLevel + LevelControl.<Start>d__46 hooks |
| savedata.cpp | phime.json inside the game's cloud save, dialog card, first-run welcome, panel buttons | save import / export, all-unlocked save: removed for the release (`mod/disabled/saves`) |
| cloud.cpp, account.cpp | Android: TapTap QR login handed to the game's own SDK | the cloud pull was removed (`mod/disabled/saves/cloud.cpp`) |
| filepicker.cpp | system file picker (skin import), open the mod folder | |
| hitsound.cpp | 正解音 (on-time hit sounds) | PlayHitFx delegate patched; plays from `chartNoteSortByTime` |
| skip.cpp | SKIP intro / ending | music resync after jumps |
| skinpack.cpp/.h | Phira pack parser (zip+inflate+yml+checks) | pure, tested |
| audiodec.cpp/.h | OGG Vorbis / WAV → PCM16 | wraps stb_vorbis, tested |
| skin.cpp | Phira skins: notes, hit fx, colours, library, settings preview | see "Skin" below |
| skinsound.cpp | skin hit sounds + per-colour volume | swaps SEPlayer tables |
| mask.cpp | 上隐条 (top hide bar) + its preview page | |
| loading.cpp, title.cpp | fast loading, title version line | unlock-all was removed (`mod/disabled/unlock`) |

## Game names we rely on ("混淆表": the game is not obfuscated, these are the real names)

Namespaces are "" unless shown. Offsets are always looked up with `fieldOffset` at runtime.

- **ProgressControl** (level clock): `nowTime` (chart time, s), `_offset`, `audioSource` (AddressableAudioSource),
  `judgeControl`, `scoreControl`, `isPlaying`, `pauseBar`, `audioTime`, `levelInformation`, `Play(bool)`,
  coroutine `<LevelOver>d__47`. nowTime = music time − (chart offset + mainOffset + player delay).
- **AddressableAudioSource**: `time`, `audioSource` (UnityEngine.AudioSource), `clipKey`, `Seek(float)`, `get_ClipLength`.
- **JudgeControl**: `judgeLineControls` (List<JudgeLineControl>), `chartNoteSortByTime` (List<ChartNote>),
  `noteUpdateManager`, `startIndex`, `endIndex`, `stopJudge`, static `PerfectTimeRange`.
- **JudgeLineControl**: `notesAbove`, `notesBelow` (List<ChartNote>), `speedEvents`, `CreateNote(int,bool)`,
  HL sprite fields `ClickHL`, `DragHL`, `FlickHL`, `HoldHL0` (head), `HoldHL1` (body), `_judgeLineSpriteRenderer`.
  Note objects are children of `JudgeLine(Clone)`.
- **ChartNote**: `type` (1 tap, 2 drag, 3 hold, 4 flick), `realTime` (s), `holdTime`, `floorPosition`, `speed`,
  `positionX`, `isJudged`, `isJudgedForFlick`.
- **NoteUpdateManager**: `clickControls`, `dragControls`, `holdControls`, `flickControls` (note components).
- **ClickControl / DragControl / FlickControl**: `noteInfor` (ChartNote), `noteImage` (Sprite the game puts on the
  renderer when visible), `_spriteRenderer`, `isVisible`. **HoldControl**: `noteImages[]`, `_holdHeadSpriteRenderer`,
  `_holdSpriteRenderer`, `_holdEndSpriteRenderer1`, `perfect` (spark prefab), `judged`, `judgeOver`.
- **ScoreControl**: `perfectJudge` / `goodJudge` (hit effect prefabs, name "perfect"), `comboText`, `combo`,
  `_score`, `_percent`, `perfect/good/bad/miss/early/late`, `maxcombo` (no `scoreText` field in 4.0.x).
- **SEPlayer** (hit sounds): static delegate `PlayHitFx(int)` (7 tap/hold head, 8 drag, 9 flick); instance fields
  `audioClips` (String[] keys), `nativeAudioClips` (AudioClip[16]), `pointers` (NativeAudioPointer[16]),
  `useCriware`, `nativeAudioReady`. E7.Native.**NativeAudio**`.Load(AudioClip)` → pointer.
- **LevelOverControl** (result): `scoreText` 0x78, `percentText` 0x80, `best` 0x70, `ratings` 0x60, `perfectText`.
- **SongSelector**: `mirrorBackground`, `enableChartMirror`, `ToggleChartMirror()`, `challengeSongSelectorControl`
  (→ `songCoverImages`), `implexroughRandomGlitchBlockingInput`. **SongSelectorItem**.
- **GameInformation** (static `_main`): `offset` (player delay, s), `noteScale`, `nowChallengeIndex`,
  `challengeSongItems`. **LevelStartInfo**: `<mirror>k__BackingField`.
- **LevelControl**: coroutine `<Start>d__46` with `<levelStartInfo>5__3`.
- **Setting** (settings scene): `note` (delay preview note), `previewNoteHolder`, `clickPosition`, `Start/Update`.
- **Loading**: coroutine `<LoadImage>d__17` (fast loading). **ChallengeModeControl** (`Phigros2.ChallengeMode.Scripts`):
  `<EnterLevel>d__47`.

### Finding names

There is no dump. The string pool of `assets/bin/Data/Managed/Metadata/global-metadata.dat` (inside the APK) lists
class names followed by their members. PowerShell:

```powershell
# once: take global-metadata.dat out of the APK (repo root)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z=[IO.Compression.ZipFile]::OpenRead((Resolve-Path com.PigeonGames.Phigros.apk))
$e=$z.Entries|?{$_.FullName -like '*global-metadata.dat'}|select -First 1
[IO.Compression.ZipFileExtensions]::ExtractToFile($e,"$env:TEMP\claude_meta.dat",$true); $z.Dispose()
# then: the names around a class
$n=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes("$env:TEMP\claude_meta.dat")) -split "`0"
$i=[Array]::IndexOf($n,'ProgressControl'); $n[($i-5)..($i+60)] -join ' | '
```

Then confirm at runtime with `findMethod` / `fieldOffset` and LOG what was found (types via
`il2cpp_class_get_name(il2cpp_object_get_class(obj))`, field flags/types via `il2cpp_class_get_fields`).

## Config (`g_cfg` in mod.h, saved as PlayerPrefs `PhiMeMod.<name>` in autoplay.cpp)

autoplay, practice(+Speed/Loop/KeepPitch), resumePreview(+Tenths), accShow, accMode, accSwap, judgeShow,
fastLoad, critical (shown or not), course, lang, correctSound, resultAccBig, skin, skinId, volTap/volDrag/volFlick
(0..200), maskOn, maskPct (0..50). A new option: field in `ModConfig`, load + save in autoplay.cpp, a row in panel.cpp.

## Skin (skin.cpp) in short

Pack = Phira zip, kept in `<mod folder>/skins/<id>.zip` + `index.txt`. Note sprites: map game sprite → skin sprite
(ppu = skinW × game ppu / game width; `_mh` images use the plain kind's ppu); also rewrite each note's sprite
fields so the game assigns ours. Hold body = the atlas middle box-resampled. Hit fx = the game's effect renderer
showing our frames each frame; sparks tinted (skin colour ÷ gold) via `critTintRenderer`. Judge line AP/FC colours
via critical.cpp `paintLines`.

## Working rules for agents

- Change only what the task names; keep the 4-space style and short comments.
- Never push, never touch the keystore, never edit `mod/out`.
- Report: files changed, how it was built/tested, what still needs a device test (most game behaviour does).
