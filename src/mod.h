#pragma once
#include "unity.h"

#define PM_VERSION "1.0.0"
#define PM_VERSION_NUM 10000 // major * 10000 + minor * 100 + patch (PhiMe_Claim)

struct ModConfig {
    bool loaded = false;
    bool autoplay = false;   // judge every note as Perfect (results are shown but never saved)
    bool practice = false;   // practice mode (toggle beside the mirror switch in song select)
    int practiceSpeed = 100; // percent, step 5
    bool practiceLoop = false;
    bool practiceKeepPitch = true;
    bool resumePreview = true;     // own prepare countdown after "continue", replaying the chart
    int resumePreviewTenths = 30;  // prepare time, 0..30 tenths of a second (0 = start at once)
    // in-level HUD (hud.cpp)
    bool accShow = false;   // accuracy line under the score
    int accMode = 0;        // 0 = live (judged notes only), 1 = best reachable (100% minus what is lost)
    bool accSwap = false;   // accuracy takes the big score slot, the score becomes the small line
    bool judgeShow = false; // Perfect / Good / Bad+Miss counts under the combo
    bool fastLoad = true;   // shorter song loading screen (loading.cpp)
    bool critical = false;  // Critical Perfect (<= 40 ms) vs Perfect (critical.cpp)
    bool course = false;    // course (challenge) mode judging and scoring, records kept locally (course.cpp)
    int lang = 0;           // the mod's own UI: 0 = 中文, 1 = English (serif font)
    bool correctSound = false;  // 正解音: every hit sound at the note's exact time (hitsound.cpp)
    bool resultAccBig = false;  // result screen: accuracy as the big number (its switch is on that screen)
    bool skin = false;          // Phira-format skin (skin.cpp); forces critical off
    int skinId = 0;             // which pack of the library (skins/<id>.zip)
    int volTap = 100, volDrag = 100, volFlick = 100;
    bool maskOn = false;        // 上隐条 (mask.cpp)
    int maskPct = 25;           // its height, 0..50 % of the screen from the top  // hit sound volume per note colour, 0..200 % (skinsound.cpp)
};

extern ModConfig g_cfg;
void configLoad();  // main thread
void configSave();  // main thread

// the mod's UI text in the chosen language: T("中文", "English")
inline const char* T(const char* zh, const char* en) { return g_cfg.lang == 1 ? en : zh; }
inline bool langEn() { return g_cfg.lang == 1; }

// log the first time a hook fires (helps confirm hooks work on a new device / game version)
#define PM_ONCE(msg)              \
    do {                          \
        static bool _done = false; \
        if (!_done) {             \
            _done = true;         \
            LOG("%s", msg);       \
        }                         \
    } while (0)

// Clawd's little hop: height in canvas units for time t (seconds). A quick hop, then a pause.
inline float clawdHop(float t, float height) {
    const float period = 1.1f, air = 0.38f;
    float p = t - (float)(int)(t / period) * period;
    if (p >= air) return 0;
    float u = p / air;
    return 4 * u * (1 - u) * height;
}

bool autoplayInstall();      // game hooks (any thread)
bool autoplayLevelActive();  // the current / last level was started with autoplay on
bool panelInstall();         // settings entry + panel hooks (any thread)
bool resultInstall();        // save sandbox + result-screen decorations (any thread)
bool practiceInstall();     // practice mode hooks (any thread)
void practiceLevelStart(Obj* progressControl);  // called from the ProgressControl.Start hook
void practiceTickFromUpdate();                  // backup per-frame driver (NoteUpdateManager.Update)

// resume (resume.cpp): our own prepare time after "continue", with a chart replay
bool resumeInstall();
bool resumeBeforeNotes(Obj* noteUpdateManager);  // NoteUpdateManager.Update, before the original
void resumeAfterNotes();                         // ... and after it
bool resumeActive();                             // replay running: no touch judging at all
void resumeAbort();                              // put the real notes back now (practice seeks)

// in-level HUD (hud.cpp): accuracy line and judgement counts
bool hudInstall();
// faster chapter loading screen (loading.cpp)
bool loadingInstall();
// Critical Perfect (critical.cpp)
bool criticalInstall();
bool critEnabled();                 // switch on and hooks resolved
int critCount();                    // Criticals so far in this level
// Runs NoteUpdateManager.Update (`orig`) so every Perfect gets its Critical / Perfect effect and count;
// false = mode off, the caller runs the original. counting = false during the resume replay (restored anyway).
void critResultStart(Obj* levelOverControl);  // result screen: split the Perfect count (LevelOverControl.Start)
bool critRunNotes(Obj* noteUpdateManager, void (*orig)(Obj*, const MethodInfo*), const MethodInfo* m,
                  bool counting);
int critResultCount(int perfect);  // Criticals of the level whose result has `perfect` Perfects, -1 = mode was off
// course mode (course.cpp): the game's own challenge-mode windows and score, records on this device only
bool courseInstall();
bool courseLevelActive();             // the current / last level runs in course mode
bool gameChallengeLevel();            // the current / last level is the game's own challenge mode
void courseLevelStart(Obj* pc);       // ProgressControl.Start, after the original
void courseSongSelect(Obj* ss);       // song select: show course or normal records (SongSelector.Start / toggle)
void courseResultStart(Obj* loc);     // LevelOverControl.Start: COURSE MODE label, best, local record
void prefSetStringRaw(const char* key, const char* value);  // result.cpp: a write the save sandbox lets through
// score details (details.cpp): 4-decimal accuracy and judgement counts in song select, a details card;
// judgement counts of the best play per chart kept in our own JSON file (this device only)
bool detailsInstall();
void detailsSongSelect(Obj* ss);    // SongSelector.Start (after the original)
void detailsResultStart(Obj* loc);  // LevelOverControl.Start: remember this play's judgements
// world x of the level tabs .. rank badge bar under the cover; *yMid = the tabs' vertical middle (world)
bool songBarSpan(Obj* ss, float* x0, float* x1, float* yMid = nullptr, bool* exact = nullptr);
// the PhiMe save file (phime.json inside the game's cloud save): judgement details + course records (merged)
bool detailsExportFile(const char* path, int* records, int* course);
bool detailsImportFile(const char* path, int* records, int* course);
// course records (course.cpp), for the PhiMe save file; courseRecMerge needs a prefSave() afterwards
bool courseRecGet(const char* id, const char* lv, int* s, float* a, bool* c);
void courseRecMerge(const char* id, const char* lv, int s, float a, bool c);
// savedata.cpp: PhiMe's data inside the game's cloud save, the dialog card, the first-run welcome (save import /
// export and the all-unlocked save were removed for the release: mod/disabled/saves)
bool savedataInstall();
void saveWipe(bool after);       // PlayerPrefs.DeleteAll outside the sandbox: keep PhiMe's keys, merge phime.json
bool detailsResultPending();     // details.cpp: this result's judgements are not recorded yet
bool titleInstall();  // title screen: PhiMe version next to the game's (title.cpp)
bool rksInstall();    // RKS with 3 decimals; count-up + "+0.012" on the result screen (rks.cpp)
// result accuracy (resacc.cpp): 4 decimals, [分数 | ACC] switch for the big number and its best line
bool resAccInstall();
// 正解音 (hitsound.cpp): the game's hit sounds muted, each note's sound played at its exact time
bool hitsoundInstall();
void hitsoundLevelStart(Obj* progressControl);  // ProgressControl.Start, after the original
// skin.cpp: Phira-format skins (note sprites + hit effects)
bool skinInstall();
void skinLevelStart(Obj* progressControl);  // ProgressControl.Start, after the original
bool skinActive();                          // on and a pack is loaded
// the panel's 皮肤 page: the library, the pack in use (0 = the game's own), switching / deleting
int skinLibCount();
bool skinLibEntry(int i, int* id, const char** name, const char** author);
int skinCurrent();
bool skinBroken(int id);
void skinUse(int id);
void skinAskDelete(int id);
void skinImport();                          // the panel's 导入 button: system picker, checks, keeps skin.zip
void skinOnClone(Obj* src, Obj* clone);     // critical.cpp's Instantiate hook: hit effects
void skinRefreshNotes();
void skinSettingStart(Obj* setting);   // the settings' delay preview (Setting.Start)
struct SkinPack;
void skinSoundsLoad(const SkinPack& p);  // skinsound.cpp: decode a pack's hit sounds (kept as PCM)
void skinSoundsApply(bool on);          // level start: SEPlayer's tap / drag / flick sounds -> the skin's (or back)
bool skinLineColors(Color out[2]);
bool skinJudgeHex(char perf[8], char good[8]);  // skin on: its Perfect / Good colours as RRGGBB      // skin on: its AP (colorPerfect) / FC (colorGood) judge line colours
void critTintRenderer(Obj* renderer, Color k);  // critical.cpp: a tinted copy of its material (cached)                    // practice.cpp after it recreated notes
const char* modFolderPath();                // savedata.cpp: the PhiMeMod folder (absolute)
void panelRefresh();                        // panel.cpp: rebuild the open page (values changed elsewhere)
// challenge.cpp: the game's own course mode, one mirror switch per song
bool challengeInstall();
void challengeSongSelectStart(Obj* songSelector);
void challengeSongSelectFrame();  // per frame
bool challengeUiActive();        // song select shows the 1st / 2nd / 3rd bar
void challengeBeforeLevel(const char* when);  // Loading.Start / ProgressControl.Start
bool maskInstall();                      // mask.cpp: 上隐条, a black band over the top of the chart
void maskLevelStart(Obj* progressControl);
bool maskPreviewPending();  // armed: the next level is the adjusting preview
bool maskPreviewActive();   // this level is it
bool maskPreviewLevel();    // this level is (or was, after 确认) the preview
void maskPreviewArm();     // the panel's 预览调整
bool maskPreviewStart();   // song select: select Rrhar'il AT and start it (false = by hand)
void panelCloseNow();      // panel.cpp
void practicePauseToLeave();  // practice.cpp: stop the loop, show the game's pause menu
bool practiceLeaveLevel();    // practice.cpp: the pause menu's 返回, invisibly (false = not found)
bool skipInstall();                       // skip.cpp: SKIP pill for long intros and after the last note
void skipLevelStart(Obj* progressControl);  // ProgressControl.Start, after the original
void resAccStart(Obj* levelOverControl);  // LevelOverControl.Start, before the original
bool resAccOwnsBest();                    // the ACC best line is shown (course.cpp leaves the line alone)
bool resAccOldBest(float* acc);           // this result's record accuracy from before the play (false = unknown)
void prefSetIntRaw(const char* key, int v);  // result.cpp: past the autoplay save sandbox
void forEachChart(void (*cb)(const char* id, const char* level, void* user), void* user);  // every song, every level
enum { SAVE_OPEN_FOLDER, SAVE_SKIN_IMPORT, SAVE_MASK_PREVIEW, SAVE_LICENSE };
bool openFolder(const char* path);  // filepicker.cpp: the Files app (iOS) / a file manager (Android) at this folder
void saveAction(int action);       // the settings panel's buttons
const char* saveFolderHint();      // the mod folder, short form for the panel
// the save page's dialog card (760 x 470): `ok` primary on the right, `cancel` beside it, `extra` at the left edge;
// tapping outside = cancel unless modal; no buttons = a status card. decorate(card) adds own content
// (card-local coordinates, centre origin). The callbacks run after the card closed.
struct SaveDialog {
    const char *title, *body, *cancel, *ok, *extra;
    void (*onCancel)();
    void (*onOk)();
    void (*onExtra)();
    bool modal;
    float bodyW;  // 0 = full width
    void (*decorate)(Obj* card);
};
void saveDialog(const SaveDialog& d);
bool saveDialogOpen();
void saveDialogClose();
void saveDialogBody(const char* body);  // new text for the open card
void saveMessage(const char* title, const char* body);  // a card with one "好"
bool saveTempFile(char* out, int cap, const char* dir, const char* file);  // "<game cache>/<dir>/<file>", dir emptied
// TapTap login (cloud.cpp, Android; the cloud pull was removed for the release: mod/disabled/saves)
void cloudPoll();                    // per frame (savedata.cpp)
bool cloudAvailable();               // Android with the Java side loaded
// a TapTap login only (saved token or QR): onToken(sessionToken, nickname) on success; false = could not start
bool cloudLogin(void (*onToken)(const char* token, const char* nick));
void cloudForget();                  // delete the saved token
// the game's own account login on the re-signed APK (account.cpp, Android): its sync / auto sync then work
bool accountInstall();
bool accountLoggedIn();  // TapSDKWrapper.LoggedIn (iOS: always true, no guide needed)
bool accountLoginNow();  // start the login from anywhere (the first-run guide); false = could not start
// the system file picker (filepicker.cpp): iOS UIDocumentPicker / Android Storage Access Framework
enum { PICK_ZIP, PICK_JSON };
// status 1 = done (open: `path` is a readable copy of the chosen file), 0 = cancelled, -1 = failed (`name` = why);
// `name` = the chosen file's name. Runs on the main thread, from pickerPoll().
typedef void (*PickDone)(int status, const char* path, const char* name);
bool pickerAvailable();
bool pickerOpen(int type, const char* copyTo, PickDone done);  // Android copies the file to copyTo
bool pickerSave(const char* path, int type, PickDone done);    // a copy of `path`, wherever the user puts it
void pickerPoll();                                             // per frame
// settings panel (panel.cpp): open it from anywhere (song select's entry button)
void panelOpen();
bool panelIsOpen();
bool detailsCardOpen();  // the score details card (details.cpp)
// song select buttons (practice.cpp): repaint after the settings changed elsewhere
void songButtonsRepaint();
void songButtonsRelabel();  // language switched: the song select labels again
void detailsRelabel();      // ... and the 详情 pill (details.cpp)

// save sandbox (result.cpp): drop PlayerPrefs writes while an autoplay run is on screen
void sandboxBegin(bool on);
void sandboxAddScene();
bool sandboxActive();

