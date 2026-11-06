// Practice mode core: section seek, loop / play-once, variable speed with optional pitch correction,
// the "选段练习" button in the pause menu and the 练习 switch next to the mirror switch.
//
// How the game runs a chart (Phigros 4.0.x):
//   - ProgressControl.nowTime = AddressableAudioSource.time - _offset; the audio wrapper derives `time` from
//     the DSP clock, so it does not follow AudioSource.pitch. We drive `time` ourselves while practising.
//   - Every note object is created on the first frame (JudgeLineControl.CreateNote over the whole
//     notesAbove/notesBelow lists) and destroyed once judged. Seeking = destroy what is left, recreate the
//     notes of the section with CreateNote, rewind the per-line event indices (they only scan forward)
//     and the touch-judging window of JudgeControl.
#include "practice.h"
#include "pitch.h"

using namespace il2cpp;
using namespace unity;

PracticeSession g_prac;

static struct {
    // ProgressControl
    int pcNow, pcOffset, pcAas, pcJc, pcScore, pcPlaying, pcPauseBar, pcAudioTime, pcStartTime, pcFirst, pcFirstGuide;
    int scoreFloats[2], scoreInts[8], scoreFlags[2], scComboLabel, scComboNum;
    const MethodInfo* pcPlay;
    // AddressableAudioSource
    int aasTime, aasSrc, aasKey;
    const MethodInfo *aasSeek, *aasLen;
    // JudgeControl
    int jcLines, jcSorted, jcStart, jcEnd, jcStop, jcNum;
    // JudgeLineControl
    int jlAbove, jlBelow, jlSpeedIdx, jlDisIdx, jlMoveIdx, jlRotIdx;
    const MethodInfo* jlCreate;
    // NoteUpdateManager lists
    int numLists[4];
    // ChartNote / ScoreControl
    int noteTime, noteJudged, noteFlick, scoreCombo;
    // misc
    Il2CppClass* FilterTrigger;
    int listItems = -1, listSize = -1;
    int ssMirror, ssMirrorIcon;
    int ssBlockInput;  // SongSelector.implexroughRandomGlitchBlockingInput: Update skips its own touch handling
    // Petrichor's noise field (噪域) blocks
    Il2CppClass* Blocks;
    const MethodInfo *blocksRecreate, *blocksHas, *blocksGet, *scUnblock;
    int scBlocked;
} P;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
typedef void (*FilterFn)(Obj*, Il2CppArray*, int, const MethodInfo*);
static VoidFn orig_AAS_Update, orig_SS_Start;
static FilterFn orig_Filter;

static Handle g_pc, g_aas, g_filter, g_ui, g_btn;
static Obj* volatile g_filterRaw = nullptr;  // compared on the audio thread (no GC handle access there)
static float g_clock = 0;     // audio clip time we feed to the game while practising
static char g_songKey[128];   // keeps the last section when replaying the same song
static float g_songKeyLen = 0;  // that song's length (g_prac.songLen is cleared at every level start)

// ---------------- small helpers ----------------
static int listView(Obj* list, Obj*** out) {
    if (!list) return 0;
    if (P.listItems < 0) {
        Il2CppClass* k = il2cpp_object_get_class(list);
        P.listItems = fieldOffset(k, "_items");
        P.listSize = fieldOffset(k, "_size");
        if (P.listItems < 0 || P.listSize < 0) return 0;
    }
    Il2CppArray* items = fld<Il2CppArray*>(list, P.listItems);
    int n = fld<int>(list, P.listSize);
    if (!items || n <= 0 || (uintptr_t)n > items->max_length) return 0;
    *out = (Obj**)arrayData(items);
    return n;
}

static void listClear(Obj* list) {
    if (!list) return;
    static const MethodInfo* clear = nullptr;
    if (!clear) clear = findMethod(il2cpp_object_get_class(list), "Clear", 0);
    invokeSafe(clear, list);
}

static float offsetOf(Obj* pc) { return fld<float>(pc, P.pcOffset); }

// for the editor's waveform: the music's Addressables key, and clip time = chart time + this offset
const char* practiceSongKey() { return g_songKey; }
float practiceClipOffset() {
    Obj* pc = (Obj*)g_pc.get();
    return alive(pc) ? offsetOf(pc) : 0;
}

// The music wrapper creates its UnityEngine.AudioSource lazily, so look it up every time.
static Obj* musicSource() {
    Obj* aas = (Obj*)g_aas.get();
    Obj* src = alive(aas) ? fld<Obj*>(aas, P.aasSrc) : nullptr;
    return alive(src) ? src : nullptr;
}

static void playGame(Obj* pc, bool play) {
    void* args[] = {&play};
    invokeSafe(P.pcPlay, pc, args);
}

// ---------------- flow speed ----------------
// A note sits (note.floorPosition - its line's floor now) x note.speed x 0.6 screen heights from its line, and a
// line's floor comes from its speed events (floorPosition + value x time). The game has no global flow setting
// (1.1.0 tried LevelInformation.Speed: not a float field), so the chart itself is scaled: every speed event's value
// and floorPosition, every note's floorPosition, and every hold's speed (a hold's speed is its body length) x k.
// That is exactly a chart whose lines all move k times faster. Applied while a section runs, back to 1 otherwise.
static int g_jlSpeedEv = -1, g_noteFloor = -1, g_noteSpeed = -1, g_noteType = -1;
static int g_evFloor = -1, g_evValue = -1, g_evStride = 8, g_evAdj = 0;
static bool g_evValueType = false;
static Handle g_flowJc;          // the JudgeControl whose chart is scaled
static float g_flowApplied = 1;  // its current factor
// What was scaled, held by us: the game may keep the chart for the next play of the song after this level's
// objects are gone, so the scale is always undone through these
struct FlowLists {
    Handle ev, above, below;
};
static FlowLists* g_fl = nullptr;
static int g_flN = 0;
static void flowForget() {
    for (int i = 0; i < g_flN; i++) g_fl[i].ev.clear(), g_fl[i].above.clear(), g_fl[i].below.clear();
    free(g_fl);
    g_fl = nullptr;
    g_flN = 0;
}
static void scaleEvents(Obj* evs, float f, int* count) {
    if (!evs) return;
    Il2CppArray* items;
    int n;
    const char* cn = il2cpp_class_get_name(il2cpp_object_get_class(evs));
    size_t cl = strlen(cn);
    if (cl > 2 && cn[cl - 2] == '[' && cn[cl - 1] == ']') {  // SpeedEvent[]
        items = (Il2CppArray*)evs;
        n = (int)items->max_length;
    } else {  // List<SpeedEvent>
        if (P.listItems < 0) return;
        items = fld<Il2CppArray*>(evs, P.listItems);
        n = fld<int>(evs, P.listSize);
        if (!items || n <= 0 || (uintptr_t)n > items->max_length) return;
    }
    char* base = (char*)arrayData(items);
    for (int i = 0; i < n; i++) {
        char* e = g_evValueType ? base + (size_t)i * g_evStride - g_evAdj : ((char**)base)[i];
        if (!e) continue;
        fld<float>(e, g_evFloor) *= f;
        fld<float>(e, g_evValue) *= f;
        (*count)++;
    }
}
static void flowRemember(Obj* jc) {
    flowForget();
    Obj** lines = nullptr;
    int nl = listView(fld<Obj*>(jc, P.jcLines), &lines);
    if (nl <= 0) return;
    g_fl = (FlowLists*)calloc((size_t)nl, sizeof(FlowLists));
    if (!g_fl) return;
    for (int l = 0; l < nl; l++) {
        if (!alive(lines[l])) continue;
        g_fl[g_flN].ev.set(fld<Obj*>(lines[l], g_jlSpeedEv));
        g_fl[g_flN].above.set(fld<Obj*>(lines[l], P.jlAbove));
        g_fl[g_flN].below.set(fld<Obj*>(lines[l], P.jlBelow));
        g_flN++;
    }
}
static void scaleChart(float f) {
    int evs = 0, notes = 0, holds = 0;
    for (int l = 0; l < g_flN; l++) {
        scaleEvents((Obj*)g_fl[l].ev.get(), f, &evs);
        for (int side = 0; side < 2; side++) {
            Obj** ns = nullptr;
            int nn = listView((Obj*)(side ? g_fl[l].below : g_fl[l].above).get(), &ns);
            for (int i = 0; i < nn; i++) {
                Obj* note = ns[i];
                if (!note) continue;
                fld<float>(note, g_noteFloor) *= f;
                if (fld<int>(note, g_noteType) == 3) {
                    fld<float>(note, g_noteSpeed) *= f;
                    holds++;
                }
                notes++;
            }
        }
    }
    static int logged = 0;
    if (logged++ < 6) LOG("practice: chart flow x%.3f: %d lines, %d speed events, %d notes (%d holds)", f, g_flN, evs, notes, holds);
}
static bool flowReady() { return g_jlSpeedEv >= 0 && g_noteFloor >= 0 && g_noteSpeed >= 0 && g_noteType >= 0 && g_evFloor >= 0; }
// back to the chart's own flow, even when the level is gone (never leave the chart scaled)
static void flowRestore() {
    if (g_flowApplied != 1) {
        scaleChart(1 / g_flowApplied);
        LOG("practice: chart flow back to x1");
    }
    flowForget();
    g_flowJc.clear();
    g_flowApplied = 1;
}
static void applyFlow(bool running) {
    if (!flowReady()) return;
    Obj* pc = (Obj*)g_pc.get();
    Obj* jc = alive(pc) ? fld<Obj*>(pc, P.pcJc) : nullptr;
    if (!alive(jc)) return;
    if (jc != g_flowJc.get()) {  // a new level: its chart is at its own flow
        flowRestore();
        g_flowJc.set(jc);
    }
    float k = running && !maskPreviewActive() ? g_prac.flow : 1.f;
    if (k == g_flowApplied) return;
    if (!g_flN) flowRemember(jc);
    scaleChart(k / g_flowApplied);
    g_flowApplied = k;
    // the lines rescan their speed events from the start (they only look forward)
    Obj** lines = nullptr;
    int nl = listView(fld<Obj*>(jc, P.jcLines), &lines);
    for (int l = 0; l < nl; l++)
        if (alive(lines[l])) fld<int>(lines[l], P.jlSpeedIdx) = 0;
}

static void applySpeed(bool running) {
    Obj* src = musicSource();
    float s = running ? g_prac.speed : 1.f;
    setPitch(src, s);
    pitch::setSpeed(running && g_prac.keepPitch ? s : 1.f);
    applyFlow(running);
}

// ---------------- noise field (噪域) ----------------
// Each block of Petrichor's noise field destroys itself once the chart is past its disappear time (plus a
// short interval). LevelControl's start makes them all with PreviewElementUpdateControl.DestroyAndCreateAllBlocks;
// when a seek goes back before the furthest time played since then, we make them again the same way.
static Handle g_blocks;
static float g_blocksSeen = 0;  // furthest chart time since the blocks were (re)made
static bool g_blocksNone = false;

static void blocksRewind(float t) {
    if (!P.blocksRecreate || g_blocksNone) return;
    if (t < g_blocksSeen - 0.05f) {
        Obj* c = (Obj*)g_blocks.get();
        if (!alive(c)) {
            c = findObjectOfType(P.Blocks);
            g_blocks.set(c);
        }
        if (!alive(c) || (P.blocksHas && !callm<bool>(P.blocksHas, c))) {
            g_blocksNone = true;  // a level without noise blocks: never look again
            return;
        }
        // only when one of them is really gone (a drag back through the editor would redo it every frame)
        bool gone = !P.blocksGet;
        if (P.blocksGet) {
            Obj** e = nullptr;
            int n = listView(callm<Obj*>(P.blocksGet, c), &e);
            for (int i = 0; i < n && !gone; i++) gone = !alive(e[i]);
        }
        if (gone && invokeSafe(P.blocksRecreate, c))
            LOG("practice: noise field blocks recreated at %.2f (were past %.2f)", t, g_blocksSeen);
        g_blocksSeen = t;
    }
    if (t > g_blocksSeen) g_blocksSeen = t;
}

// ---------------- seek ----------------
// Dragging an end point re-creates the chart every frame. A note made this frame is not drawn until the next one,
// so destroying the old notes at once left the field empty for as long as the finger moved (on a phone it never
// stops). While previewing, the old notes therefore stay on screen (no longer updated) for one more frame.
static const int kLateMax = 768;
static Handle g_late[kLateMax];
static int g_lateN = 0, g_lateFrame = -1;
static void flushLate() {
    for (int i = 0; i < g_lateN; i++) {
        Obj* go = (Obj*)g_late[i].get();
        if (alive(go)) destroy(go);
        g_late[i].clear();
    }
    g_lateN = 0;
}

// Recreate the chart at chart time `t`: notes with time in [from, to] get fresh objects.
static void reseed(float t, float from, float to, bool late = false) {
    Obj* pc = (Obj*)g_pc.get();
    if (!alive(pc)) return;
    blocksRewind(t);
    Obj* jc = fld<Obj*>(pc, P.pcJc);
    if (!alive(jc)) return;
    Obj* num = fld<Obj*>(jc, P.jcNum);

    // 1. remove every note object still on the field (a preview keeps them one more frame)
    flushLate();
    if (alive(num)) {
        for (int off : P.numLists) {
            Obj* list = fld<Obj*>(num, off);
            Obj** e = nullptr;
            int n = listView(list, &e);
            for (int i = 0; i < n; i++) {
                if (!alive(e[i])) continue;
                if (late && g_lateN < kLateMax) {
                    g_late[g_lateN++].set(gameObject(e[i]));
                    g_lateFrame = frameCount();
                }
                else destroy(gameObject(e[i]));
            }
            listClear(list);
        }
    }

    // 2. per judge line: rewind event indices, recreate the section's notes
    Obj** lines = nullptr;
    int nl = listView(fld<Obj*>(jc, P.jcLines), &lines);
    int created = 0;
    for (int l = 0; l < nl; l++) {
        Obj* jl = lines[l];
        if (!alive(jl)) continue;
        fld<int>(jl, P.jlSpeedIdx) = 0;
        fld<int>(jl, P.jlDisIdx) = 0;
        fld<int>(jl, P.jlMoveIdx) = 0;
        fld<int>(jl, P.jlRotIdx) = 0;
        for (int side = 0; side < 2; side++) {
            bool above = side == 0;
            Obj** notes = nullptr;
            int nn = listView(fld<Obj*>(jl, above ? P.jlAbove : P.jlBelow), &notes);
            for (int i = 0; i < nn; i++) {
                Obj* note = notes[i];
                if (!note) continue;
                float rt = fld<float>(note, P.noteTime);
                if (rt < from || rt > to) continue;
                fld<bool>(note, P.noteJudged) = false;
                fld<bool>(note, P.noteFlick) = false;
                int idx = i;
                void* args[] = {&idx, &above};
                if (invokeSafe(P.jlCreate, jl, args)) created++;
            }
        }
    }

    // 3. touch-judging window starts at the first note near t
    Obj** sorted = nullptr;
    int ns = listView(fld<Obj*>(jc, P.jcSorted), &sorted);
    int first = 0;
    while (first < ns && sorted[first] && fld<float>(sorted[first], P.noteTime) < t - 0.3f) first++;
    fld<int>(jc, P.jcStart) = first;
    fld<int>(jc, P.jcEnd) = first;

    // 4. clocks
    float clip = t + offsetOf(pc);
    if (clip < 0) clip = 0;
    fld<float>(pc, P.pcNow) = t;
    Obj* aas = (Obj*)g_aas.get();
    if (alive(aas)) fld<float>(aas, P.aasTime) = clip;
    g_clock = clip;
    pitch::reset();
    skinRefreshNotes();  // the new note objects start with the game's sprites
    // a drag in the editor reseeds every frame (1.3.2 logged each: 100+ lines per drag); every 20th preview is enough
    static int previews = 0;
    if (!late || previews++ % 20 == 0) LOG("practice: %s %.2f, %d notes", late ? "preview" : "seek", t, created);
}

void practiceAudioJump(float clip) {
    if (g_prac.st == PR_OFF) return;
    g_clock = clip;
    pitch::reset();
}

static float g_lastPreview = -1;
void practicePreview(float t) {
    if (t == g_lastPreview) return;
    g_lastPreview = t;
    reseed(t, t, t + 3.f, true);  // only the next few seconds: cheap enough to redo on every drag frame
}

// Put both the chart and the audio wrapper's own clock at chart time t (Seek re-anchors its DSP clock).
static void seekAudio(float t) {
    Obj* pc = (Obj*)g_pc.get();
    float clip = t + offsetOf(pc);
    if (clip < 0) clip = 0;
    Obj* aas = (Obj*)g_aas.get();
    if (alive(aas)) {
        void* args[] = {&clip};
        invokeSafe(P.aasSeek, aas, args);
        fld<float>(aas, P.aasTime) = clip;
    }
    setAudioTime(musicSource(), clip);
    // ProgressControl resumes the music with PlayScheduled(audioTime): keep it in step
    if (alive(pc)) fld<float>(pc, P.pcAudioTime) = clip;
    g_clock = clip;
}

// ---------------- run control ----------------
// The pause menu is ProgressControl.pauseBar (Play(false) activates it, the menu's "continue" hides it).
static Obj* pauseBar() {
    Obj* pc = (Obj*)g_pc.get();
    return alive(pc) ? fld<Obj*>(pc, P.pcPauseBar) : nullptr;
}

static void hidePauseMenu(bool hide) {
    Obj* bar = pauseBar();
    if (alive(bar)) setActive(bar, !hide);
    Obj* pc = (Obj*)g_pc.get();
    Obj* jc = alive(pc) ? fld<Obj*>(pc, P.pcJc) : nullptr;
    if (alive(jc)) fld<bool>(jc, P.jcStop) = hide;  // no touch judging while the editor is up
}

// Score, combo and judgement counts back to a fresh start (every run / loop / exit starts from zero).
static void resetScore() {
    Obj* pc = (Obj*)g_pc.get();
    Obj* sc = alive(pc) ? fld<Obj*>(pc, P.pcScore) : nullptr;
    if (!alive(sc)) return;
    // a noise-field touch blocks the combo; a fresh run starts unblocked
    if (P.scUnblock && P.scBlocked >= 0 && fld<bool>(sc, P.scBlocked)) invokeSafe(P.scUnblock, sc);
    for (int off : P.scoreFloats)
        if (off >= 0) fld<float>(sc, off) = 0;
    for (int off : P.scoreInts)
        if (off >= 0) fld<int>(sc, off) = 0;
    for (int off : P.scoreFlags)
        if (off >= 0) fld<bool>(sc, off) = true;  // isAllPerfect / isFullCombo
}

// ---------------- no result screen in a practice level ----------------
// ProgressControl.Update starts its LevelOver coroutine once the chart time reaches the end, even while paused:
// dragging the editor's B point to the far right put the preview there and the result screen followed (0.24.0
// log). In a practice level the coroutine is ended before its first step; the frame then reopens the editor.
typedef bool (*MoveNextFn)(Obj*, const MethodInfo*);
static MoveNextFn orig_LevelOverNext;
static bool g_overBlocked = false;
static int g_liOver = -1, g_pcLevelInfo = -1;
static bool hk_LevelOverNext(Obj* self, const MethodInfo* m) {
    if (g_prac.st != PR_OFF && alive((Obj*)g_pc.get())) {
        static int logged = 0;
        if (logged++ < 3) LOG("practice: the game tried to end the level (chart time %.2f), result screen blocked",
                                fld<float>((Obj*)g_pc.get(), P.pcNow));
        g_overBlocked = true;
        return false;
    }
    return orig_LevelOverNext(self, m);
}
// the game marks the level as over before it starts the coroutine: clear that for the next run
static void clearLevelOver() {
    Obj* pc = (Obj*)g_pc.get();
    Obj* li = alive(pc) && g_pcLevelInfo >= 0 ? fld<Obj*>(pc, g_pcLevelInfo) : nullptr;
    if (li && g_liOver >= 0) fld<bool>(li, g_liOver) = false;
}

static void startRun() {
    Obj* pc = (Obj*)g_pc.get();
    if (!alive(pc)) return;
    // a loop restart while a resume session still ran: end it first, or its end would put back the judged flags of
    // the notes made again here (they would vanish unplayed in the new loop)
    resumeAbort();
    clearLevelOver();
    float pre = 1.0f * g_prac.speed;  // ~1 s of real time before the first note
    float t0 = g_prac.a - pre;
    if (t0 < 0) t0 = 0;
    applyFlow(true);  // before the notes are made, in case the game reads it when it creates them
    reseed(t0, g_prac.a, g_prac.b);
    resetScore();
    seekAudio(t0);
    g_prac.st = PR_RUN;
    applySpeed(true);
}

void practiceStart() {
    Obj* pc = (Obj*)g_pc.get();
    if (!alive(pc)) return;
    resumeAbort();
    editorClose();
    // the level was paused before the game's first countdown: that one-off "first launch" lead-in must not
    // come back later (it would also hide later resumes from the resume preview)
    if (P.pcFirst >= 0) fld<bool>(pc, P.pcFirst) = false;
    if (P.pcFirstGuide >= 0) fld<bool>(pc, P.pcFirstGuide) = false;
    hidePauseMenu(true);  // menu stays hidden, like after the game's own "continue"
    Obj* jc = fld<Obj*>(pc, P.pcJc);
    if (alive(jc)) fld<bool>(jc, P.jcStop) = false;
    startRun();
    playGame(pc, true);
    // Play(true) arms the game's resume countdown (3 s, then 1 s of audio scheduling). Skip the countdown:
    // with _startTime = now + 1 the game schedules the music right away, 1 s ahead, like it always does.
    fld<float>(pc, P.pcStartTime) = timeSinceLevelLoad() + 1.0f;
    LOG("practice: run %.2f-%.2f x%.2f loop=%d keepPitch=%d", g_prac.a, g_prac.b, g_prac.speed, g_prac.loop,
        g_prac.keepPitch);
}

// Leaving the editor without practising: the whole chart goes back to the beginning (fresh notes, score
// zero, music at 0) and the game's pause menu is shown, so practice progress never carries into a normal run.
void practiceCancelEdit() {
    resumeAbort();
    editorClose();
    reseed(0, -1, g_prac.songLen + 10);
    resetScore();
    clearLevelOver();
    seekAudio(0);
    hidePauseMenu(false);
    g_prac.st = PR_IDLE;
    applySpeed(false);
    LOG("practice: back to the start of the song");
}

// the hide bar preview is done: stop looping, show the game's pause menu (its 返回 leaves the level)
// Leave the level the way the pause menu's 返回 does: its BackToChapterSelect, on a component of the pause bar. The bar
// is activated (a hidden object cannot run the coroutine) but kept invisible. false = not found
bool practiceLeaveLevel() {
    Obj* pc = (Obj*)g_pc.get();
    Obj* bar = pauseBar();
    if (!alive(pc) || !bar) return false;
    const char* bn = il2cpp_class_get_name(il2cpp_object_get_class(bar));
    Obj* go = !strcmp(bn, "GameObject") ? bar : gameObject(bar);
    static Il2CppClass* MB = findClass("UnityEngine", "MonoBehaviour");
    Il2CppArray* cs = MB ? componentsInChildren(go, MB) : nullptr;
    for (uintptr_t i = 0; cs && i < cs->max_length; i++) {
        Obj* c = (Obj*)arrayData(cs)[i];
        if (!alive(c)) continue;
        Il2CppClass* k = il2cpp_object_get_class(c);
        if (strcmp(il2cpp_class_get_name(k), "Pause")) continue;  // the pause menu's own component
        const MethodInfo* m = findMethod(k, "BackToChapterSelect", -1);
        if (!m) continue;
        // its parameters (it takes some in 4.0.x): objects null, numbers / bools 0 (false)
        uint32_t np = il2cpp_method_get_param_count(m);
        static uint64_t zero[8];
        void* args[8];
        for (uint32_t p = 0; p < np && p < 8; p++) {
            int tt = il2cpp_type_get_type(il2cpp_method_get_param(m, p));
            bool ref = tt == IL2CPP_TYPE_CLASS || tt == IL2CPP_TYPE_STRING || tt == IL2CPP_TYPE_SZARRAY || tt == 0x1c;
            args[p] = ref ? nullptr : (void*)&zero[p];
        }
        setGroupAlpha(addCanvasGroup(go), 0);
        setActive(go, true);
        bool ok = invokeSafe(m, c, np ? args : nullptr);
        LOG("practice: leaving the level via Pause.BackToChapterSelect (%u parameter(s)): %s", np, ok ? "ok" : "FAILED");
        if (ok) return true;
    }
    LOG("practice: no BackToChapterSelect on the pause bar");
    return false;
}

void practicePauseToLeave() {
    Obj* pc = (Obj*)g_pc.get();
    if (!alive(pc)) return;
    g_prac.loop = false;
    g_prac.st = PR_IDLE;
    hidePauseMenu(false);
    playGame(pc, false);
}

void practiceSaveSettings() {
    g_cfg.practiceSpeed = (int)(g_prac.speed * 100 + 0.5f);
    g_cfg.practiceFlow = (int)(g_prac.flow * 100 + 0.5f);
    g_cfg.practiceLoop = g_prac.loop;
    g_cfg.practiceKeepPitch = g_prac.keepPitch;
    configSave();
}

// Song length / section defaults: read when first needed (the clip may still be loading at level start).
static void refreshSong() {
    Obj* pc = (Obj*)g_pc.get();
    Obj* aas = (Obj*)g_aas.get();
    // ClipLength dereferences the AudioSource/clip, which may not exist yet: only call it safely
    Obj* src = musicSource();
    if (!alive(pc) || !alive(aas) || !P.aasLen || !src) return;
    // the clip is still loading: ask again next frame (calling ClipLength now throws a NullReferenceException,
    // which 1.3.1 and older did on every frame of the level start: slow, and a log line each time)
    static const MethodInfo* getClip = nullptr;
    if (!getClip) getClip = findMethod(il2cpp_object_get_class(src), "get_clip", 0);
    if (getClip && !callm<Obj*>(getClip, src)) return;
    float clipLen = 0;
    if (!invokeFloat(P.aasLen, aas, &clipLen)) return;
    float len = clipLen - offsetOf(pc);
    if (len < 1) return;
    char key[128] = {0};
    Il2CppString* k = P.aasKey >= 0 ? fld<Il2CppString*>(aas, P.aasKey) : nullptr;
    utf8(k, key, sizeof(key));
    bool same = key[0] && strcmp(key, g_songKey) == 0 && g_songKeyLen == len;
    g_songKeyLen = len;
    g_prac.songLen = len;
    if (!same || g_prac.b <= g_prac.a) {
        g_prac.a = 0;
        g_prac.b = len - 0.2f;
        memcpy(g_songKey, key, sizeof(key));
    }
}

static void openEditor() {
    Obj* pc = (Obj*)g_pc.get();
    if (!alive(pc)) return;
    resumeAbort();
    refreshSong();
    if (g_prac.songLen < 1) {
        LOG("practice: song length not available yet");
        return;
    }
    if (fld<bool>(pc, P.pcPlaying)) playGame(pc, false);
    applySpeed(false);
    hidePauseMenu(true);
    g_prac.st = PR_EDIT;
    g_lastPreview = -1;
    editorOpen();
}

// ---------------- on-screen entry ----------------
// Our own "选段练习" pill at the bottom of the screen, always there in practice mode (except while the
// editor is open). Taps are read straight from the touch list, so it works without any EventSystem.
static bool g_autoOpened = false;  // editor opened once at level start
static Vec2 g_entryPos, g_entrySize{240, 64};
static bool g_entryDown = false;

static void ensureFilter() {
    if (!P.FilterTrigger || alive((Obj*)g_filter.get())) return;
    Obj* src = musicSource();
    if (!alive(src)) return;
    Obj* f = addComponent(gameObject(src), P.FilterTrigger);
    g_filter.set(f);  // the handle keeps it alive; the raw pointer is what the audio thread compares
    g_filterRaw = f;
    LOG("practice: pitch filter attached");
}

static void entryTick() {
    Obj* btn = (Obj*)g_btn.get();
    if (!alive(btn)) return;
    // only while the chart is not running (paused, before the start, after a play-once section):
    // during play it would just be in the way of notes
    Obj* pc = (Obj*)g_pc.get();
    bool playing = alive(pc) && fld<bool>(pc, P.pcPlaying);
    bool show = g_prac.st != PR_EDIT && !playing && !maskPreviewLevel();  // (never in the hide bar preview)
    setActive(btn, show);
    if (!show) {
        g_entryDown = false;
        return;
    }
    TouchInfo t[4];
    int n = touches(t, 4);
    for (int i = 0; i < n; i++) {
        Vec2 p = screenToCanvas(t[i].pos);
        float dx = p.x - g_entryPos.x, dy = p.y - g_entryPos.y;
        bool inside = dx > -g_entrySize.x / 2 - 12 && dx < g_entrySize.x / 2 + 12 && dy > -g_entrySize.y / 2 - 12 &&
                      dy < g_entrySize.y / 2 + 12;
        if (t[i].phase == 0 && inside) {
            g_entryDown = true;
            setScale(goTransform(btn), 0.94f);  // btn is a GameObject, not a Component
        } else if ((t[i].phase == 3 || t[i].phase == 4) && g_entryDown) {
            g_entryDown = false;
            setScale(goTransform(btn), 1);
            if (inside && t[i].phase == 3) {
                LOG("practice: entry tapped");
                openEditor();
                return;
            }
        }
    }
}

// ---------------- per frame ----------------
static void practiceFrame(void*) {
    if (g_prac.st == PR_OFF) return;
    // driven by both Canvas.willRenderCanvases and NoteUpdateManager.Update: run once per frame
    static int lastFrame = -1;
    int f = frameCount();
    if (f == lastFrame) return;
    lastFrame = f;
    if (g_lateN && f > g_lateFrame) flushLate();  // a preview's old notes were shown one more frame
    Obj* pc = (Obj*)g_pc.get();
    if (!alive(pc)) {  // left the level
        g_prac.st = PR_OFF;
        g_pc.clear();
        flowRestore();  // (if the level's LevelInformation outlives it)
        return;
    }
    static int loggedCh = -1;  // the music's output layout: 变速不变调 needs it (see pitch.cpp)
    if (pitch::channels() != loggedCh && pitch::channels() > 0) {
        loggedCh = pitch::channels();
        LOG("practice: music output has %d channel(s)", loggedCh);
    }
    PM_ONCE("practice: frame callback alive");
    float dt = unscaledDt();
    ensureFilter();
    if (editorIsOpen()) editorTick(dt);
    entryTick();

    if (g_overBlocked) {  // the game wanted the result screen: back to the section editor instead
        g_overBlocked = false;
        if (g_prac.st != PR_EDIT) openEditor();
        return;
    }
    bool playing = fld<bool>(pc, P.pcPlaying);
    float nowT = fld<float>(pc, P.pcNow);
    if (nowT > g_blocksSeen) g_blocksSeen = nowT;
    // practice levels open straight into the section editor (as soon as the song's length is known)
    if (!g_autoOpened && g_prac.st == PR_IDLE && playing) {
        refreshSong();
        if (g_prac.songLen >= 1) {
            g_autoOpened = true;
            if (maskPreviewActive()) {  // the hide bar's adjusting preview: 1:22 to the end, looping, no editor
                g_prac.a = g_prac.songLen > 100 ? 82.f : g_prac.songLen * 0.5f;
                g_prac.b = g_prac.songLen - 0.2f;
                g_prac.loop = true;
                g_prac.speed = 1;
                LOG("practice: hide bar preview, %.2f-%.2f looping", g_prac.a, g_prac.b);
                practiceStart();
                return;
            }
            LOG("practice: level started, opening the section editor");
            openEditor();
            return;
        }
    }

    if (g_prac.st == PR_RUN && playing) {
        float now = fld<float>(pc, P.pcNow);
        if (now >= g_prac.b) {
            if (g_prac.loop) {
                startRun();
            } else {
                playGame(pc, false);
                openEditor();
            }
        }
    } else if (g_prac.st == PR_IDLE && playing && g_prac.songLen >= 1 && !resumeActive()) {
        // whole song played without a section: back to the editor right before the game would end the
        // level, so a practice level never reaches the result screen
        float now = fld<float>(pc, P.pcNow);
        if (now >= g_prac.songLen - 0.15f) {
            LOG("practice: end of song, back to the section editor");
            openEditor();
        }
    }
}

void practiceTickFromUpdate() { practiceFrame(nullptr); }

// Combo display: ScoreControl.Update writes "COMBO" + the number when combo >= 3 and empty strings below
// that. In a practice level we overwrite both right after it: "PRACTICE" and the combo, always shown.
static VoidFn orig_SC_Update;
static Handle g_practiceStr, g_comboStr;
static int g_comboShown = -1;
static Handle g_courseStr;
static void hk_SC_Update(Obj* self, const MethodInfo* m) {
    orig_SC_Update(self, m);
    if (g_prac.st == PR_OFF) {
        // course mode: the label under the combo says COURSE (only while the game shows one, combo >= 3)
        if (!courseLevelActive() || autoplayLevelActive()) return;
        Obj* label = fld<Obj*>(self, P.scComboLabel);
        Il2CppString* cur = textStr(label);
        if (!cur || il2cpp_string_length(cur) == 0) return;
        if (!g_courseStr.h) g_courseStr.set(str("COURSE"));
        if (cur != g_courseStr.get()) setTextStr(label, (Il2CppString*)g_courseStr.get());
        return;
    }
    if (!g_practiceStr.h) g_practiceStr.set(str("PRACTICE"));
    int c = fld<int>(self, P.scoreCombo);
    if (c != g_comboShown || !g_comboStr.h) {  // new string only when the number changes
        char b[16];
        snprintf(b, sizeof(b), "%d", c);
        g_comboStr.set(str(b));
        g_comboShown = c;
    }
    setTextStr(fld<Obj*>(self, P.scComboLabel), (Il2CppString*)g_practiceStr.get());
    setTextStr(fld<Obj*>(self, P.scComboNum), (Il2CppString*)g_comboStr.get());
}

// Audio wrapper: after its own Update, replace its DSP-derived time with one that follows our speed.
// Done for the whole practice level, not only while a section runs: after a few seeks the wrapper's own
// DSP-based time no longer matches where the AudioSource really is (it ran ahead and ended the song early),
// so the real playback position (AudioSource.time, which follows pitch) drives the game everywhere.
static void hk_AAS_Update(Obj* self, const MethodInfo* m) {
    orig_AAS_Update(self, m);
    if (g_prac.st == PR_OFF || self != g_aas.get()) return;
    Obj* src = musicSource();
    if (!audioPlaying(src)) return;  // lead-in before the music starts stays the game's own
    float at = audioTime(src);
    g_clock += unscaledDt() * (g_prac.st == PR_RUN ? g_prac.speed : 1.f);
    float err = at - g_clock;
    if (err > 0.12f || err < -0.12f) g_clock = at;
    else g_clock += err * 0.05f;
    fld<float>(self, P.aasTime) = g_clock;
}

static void hk_Filter(Obj* self, Il2CppArray* data, int channels, const MethodInfo* m) {
    if (self == g_filterRaw) {  // our instance on the music source (audio thread)
        if (data && channels > 0) pitch::process((float*)arrayData(data), (int)(data->max_length / channels), channels);
        return;
    }
    orig_Filter(self, data, channels, m);
}

// ---------------- level start ----------------
void practiceLevelStart(Obj* pc) {
    configLoad();
    flowRestore();
    g_prac.st = PR_OFF;
    g_btn.clear();
    g_filter.clear();
    g_filterRaw = nullptr;
    g_autoOpened = false;
    g_blocks.clear();
    g_blocksSeen = 0;
    g_blocksNone = false;
    // the last level's length must not open the editor before this song's clip is read (it kept the old timeline
    // and end points); the section itself is kept by refreshSong when the song is the same
    g_prac.songLen = 0;
    // (the hide bar preview runs as a practice loop; the game's own course mode never practises)
    if ((!g_cfg.practice || gameChallengeLevel() || challengeRunning()) && !maskPreviewPending()) return;
    Obj* aas = fld<Obj*>(pc, P.pcAas);
    if (!alive(aas)) {
        LOG("practice: music player not found");
        return;
    }
    g_pc.set(pc);
    g_aas.set(aas);  // its AudioSource may not exist yet: musicSource() looks it up when needed
    g_prac.speed = g_cfg.practiceSpeed / 100.f;
    g_prac.flow = g_cfg.practiceFlow / 100.f;
    g_prac.loop = g_cfg.practiceLoop;
    g_prac.keepPitch = g_cfg.practiceKeepPitch;
    pitch::setSpeed(1);
    refreshSong();

    // on-screen entry, bottom centre (above the CLAWD PLAYING badge when autoplay is on too)
    Obj* root = overlayCanvas("PhiMePractice", 30500);
    g_ui.set(root);
    float y = autoplayLevelActive() ? 108 : 44;
    g_entryPos = Vec2{0, -canvasH() / 2 + y};
    Obj* border = image(root, "practiceEntry", SPR_ROUND, rgb(0xE8E6DC, 0.95f), Vec2{0.5f, 0}, Vec2{0, y},
                        Vec2{g_entrySize.x + 4, g_entrySize.y + 4}, 32);
    Obj* fill = image(rt(border), "fill", SPR_ROUND, rgb(0xFAF9F5, 0.95f), Vec2{0.5f, 0.5f}, Vec2{0, 0}, g_entrySize,
                      30);
    image(rt(fill), "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0, 0.5f}, Vec2{38, 1}, Vec2{40, 23});
    text(rt(fill), T("选段练习", "Sections"), 24, rgb(0xD97757), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{18, 0},
         Vec2{180, 44});
    g_btn.set(gameObject(border));

    g_prac.st = PR_IDLE;
    onEveryFrame(practiceFrame);
    LOG("practice: ready, song %.1fs, section %.2f-%.2f", g_prac.songLen, g_prac.a, g_prac.b);
}

// ---------------- song select: 镜像 / 练习 / 自动 buttons ----------------
// The game's Mirror switch is hidden (CanvasGroup alpha 0 + click-through, so its own animation keeps
// running) and replaced by one dock of our own controls, in one row from where Mirror was, in the game's
// dark-glass look with Claude orange for what is on:
//   [镜像][练习][自动]   [普通 | 课题]   [Clawd PhiMe 小助手]
//   镜像 -> SongSelector.ToggleChartMirror (exactly what the original button did)
//   练习 -> practice mode        自动 -> autoplay        普通 | 课题 -> course mode        PhiMe 小助手 -> our panel
// The dock lives under the Mirror switch's parent (same canvas, so it scales with the game UI) and is pinned
// every frame to the live Mirror button, so it follows the screen's slide-in animation.
struct SongBtn {
    Handle border, fill, label;
    bool (*get)();
    void (*toggle)();
    const char* name;
};
static SongBtn g_sb[3];
static Handle g_ss, g_mirrorTr, g_btnRow;
static float g_rowDx = 0, g_rowDy = 0;  // world offset of the row from the Mirror button
static struct {
    int mirrorOn = -1;
    const MethodInfo* toggleMirror = nullptr;
} MIR;  // SongSelector's own mirror switch

// dock palette
static const Color kGlass{0.07f, 0.07f, 0.09f, 0.62f}, kHairline{1, 1, 1, 0.16f};
static const int kClay = 0xD97757, kIvory = 0xFAF9F5, kLight = 0xF0EEE6, kDim = 0xB9B6AC, kInk = 0x141413;

static bool getMirror() {
    Obj* ss = (Obj*)g_ss.get();
    return alive(ss) && MIR.mirrorOn >= 0 && fld<bool>(ss, MIR.mirrorOn);
}
static void toggleMirror() { invokeSafe(MIR.toggleMirror, g_ss.get()); }
static bool getPractice() { return g_cfg.practice; }
static void togglePractice() {
    g_cfg.practice = !g_cfg.practice;
    configSave();
}
static bool getAuto() { return g_cfg.autoplay; }
static void toggleAuto() {
    g_cfg.autoplay = !g_cfg.autoplay;
    configSave();
}

static bool isGameObject(Obj* o) {
    return o && strcmp(il2cpp_class_get_name(il2cpp_object_get_class(o)), "GameObject") == 0;
}
static Obj* asGo(Obj* o) { return !alive(o) ? nullptr : (isGameObject(o) ? o : gameObject(o)); }

static void paintSongBtn(SongBtn& b) {
    bool on = b.get && b.get();
    Obj* br = (Obj*)b.border.get();
    Obj* fl = (Obj*)b.fill.get();
    Obj* lb = (Obj*)b.label.get();
    if (alive(br)) setColor(br, on ? rgb(kClay) : kHairline);
    if (alive(fl)) setColor(fl, on ? rgb(kClay) : kGlass);
    if (alive(lb)) setColor(lb, rgb(on ? 0xFFFFFF : kLight));
}

void songButtonsRepaint() {
    for (SongBtn& b : g_sb)
        if (b.border.h) paintSongBtn(b);
}

static const char* songBtnLabel(const char* name) {
    if (!strcmp(name, "mirror")) return T("镜像", "Mirror");
    if (!strcmp(name, "practice")) return T("练习", "Practice");
    return T("自动", "Auto");
}
static Handle g_entry, g_entryFill, g_entryLabel, g_entryClawd, g_segLabelH[2], g_segTrack;
static float g_rowBh = 0;  // the dock's button height (label sizes)
static int g_nToggles = 0;
// the dock's world placement, from the Mirror switch: k = world units per local unit, (ax, ay) = where the middle
// of the first line goes (relative to the Mirror's position), maxW = the bar above in local units (0 = no limit)
static float g_dockK = 0, g_dockAx = 0, g_dockAy = 0, g_dockMaxW = 0;
static bool g_dockCentred = false;
// Phigros' song select is slanted like "/" (the cover's top edge is 0.125 of its height right of the rect centre:
// 0.25 across per unit down). The bar's centre is at the tabs' middle height, so each dock line goes that much left
// for how far below it sits (centring straight down put the dock right of the bar, worse on a 2-line phone dock).
static const float kSlant = 0.25f;
static float g_dockBarY = 0;  // the bar's middle height, relative to the Mirror button (world)
static bool g_dockSlant = false;
static bool g_dockExact = false;  // the bar was measured (else estimated from the tabs / last time, or not found)
static float g_dockRetry = 0;

static int labelSize() { return (int)(g_rowBh * 0.42f + 0.5f); }

// the entry: Clawd + "PhiMe 小助手", as wide as its content (dockLayout places it)
static void placeEntry() {
    Obj *en = (Obj*)g_entry.get(), *ef = (Obj*)g_entryFill.get(), *lb = (Obj*)g_entryLabel.get(),
        *cl = (Obj*)g_entryClawd.get();
    if (!alive(en) || !alive(lb) || !alive(cl) || g_rowBh <= 0) return;
    float h = g_rowBh, ch = h * 0.55f, cw = ch * 128.f / 72.f, gap = h * 0.22f, padX = h * 0.55f;
    const char* s = T("PhiMe 小助手", "PhiMe Assistant");
    int fs = labelSize();
    float lw = textWidth(s, fs, true);
    float w = padX + cw + gap + lw + padX;
    setSize(rt(en), Vec2{w, h});
    if (alive(ef)) setSize(rt(ef), Vec2{w - 3, h - 3});
    setPos(rt(cl), Vec2{-w / 2 + padX + cw / 2, 0});
    setSize(rt(lb), Vec2{lw + 4, h});
    setPos(rt(lb), Vec2{-w / 2 + padX + cw + gap, 1});
    retext(lb, s, fs, true);
}

static void pinRow();
static float dockBw() { return g_rowBh * 2.3f; }   // a toggle's width
static float dockSegW() { return dockBw() * 2.1f; }  // 普通 | 课题

// The dock in lines, each centred: [toggles] [普通|课题] [PhiMe entry]. One line when it fits under the bar above,
// else broken between those three groups so the widest line is as narrow as it can be.
static void dockLayout() {
    Obj* row = (Obj*)g_btnRow.get();
    Obj* en = (Obj*)g_entry.get();
    if (!alive(row) || !alive(en) || g_rowBh <= 0) return;
    placeEntry();
    float bh = g_rowBh, bw = dockBw(), gap = bh * 0.24f, groupGap = bh * 0.5f, lineGap = bh * 0.3f;
    Rect er{0, 0, bh * 4, bh};
    rectOf(rt(en), &er);
    int nt = g_nToggles;
    float gw[3] = {nt * bw + (nt > 0 ? (nt - 1) * gap : 0), dockSegW(), er.w};
    auto span = [&](int a, int b) {  // width of groups a..b-1 on one line
        float w = 0;
        for (int i = a; i < b; i++) w += gw[i] + (i > a ? groupGap : 0);
        return w;
    };
    int line[3] = {0, 0, 0};
    if (g_dockMaxW > 0 && span(0, 3) > g_dockMaxW) {
        auto mx = [](float p, float q) { return p > q ? p : q; };
        float a = mx(span(0, 1), span(1, 3)), b = mx(span(0, 2), span(2, 3));
        if ((a < b ? a : b) <= g_dockMaxW) {
            line[1] = a < b ? 1 : 0;
            line[2] = 1;
        } else {
            line[1] = 1;
            line[2] = 2;
        }
    }
    int lines = line[2] + 1;
    float lw[3] = {0, 0, 0}, W = 0;
    for (int l = 0; l < lines; l++) {
        int a = 3, b = 0;
        for (int i = 0; i < 3; i++)
            if (line[i] == l) {
                if (i < a) a = i;
                b = i + 1;
            }
        lw[l] = a < b ? span(a, b) : 0;
        if (lw[l] > W) W = lw[l];
    }
    float H = lines * bh + (lines - 1) * lineGap;
    float x[3] = {-lw[0] / 2, -lw[1] / 2, -lw[2] / 2};
    float shift[3] = {0, 0, 0};
    if (g_dockSlant && g_dockK > 1e-6f)
        for (int l = 0; l < lines; l++) {
            float lineY = g_dockAy - l * (bh + lineGap) * g_dockK;  // world, relative to the Mirror button
            shift[l] = -kSlant * (g_dockBarY - lineY) / g_dockK;
            x[l] += shift[l];
        }
    for (int i = 0; i < 3; i++) {
        int l = line[i];
        float y = H / 2 - bh / 2 - l * (bh + lineGap);
        if (i == 0) {
            for (int t = 0; t < nt; t++) {
                Obj* b = (Obj*)g_sb[t].border.get();
                if (alive(b)) setPos(rt(b), Vec2{x[l] + t * (bw + gap) + bw / 2, y});
            }
        } else {
            Obj* o = i == 1 ? (Obj*)g_segTrack.get() : en;
            if (alive(o)) setPos(rt(o), Vec2{x[l] + gw[i] / 2, y});
        }
        x[l] += gw[i] + groupGap;
    }
    setSize(row, Vec2{W, H});
    // the first line stays where it was put; a left-aligned dock (no bar found) keeps its left edge
    g_rowDx = g_dockAx + (g_dockCentred ? 0 : W * g_dockK / 2);
    g_rowDy = g_dockAy - (H / 2 - bh / 2) * g_dockK;
    pinRow();
    LOG("song select: dock %d line(s), %.0f x %.0f (bar %.0f), slant shift %.0f / %.0f", lines, W, H, g_dockMaxW,
        shift[0], shift[1]);
}

static void onSongBtn(void* user) {
    SongBtn* b = (SongBtn*)user;
    if (b->toggle) b->toggle();
    paintSongBtn(*b);
    LOG("song select: %s=%d", b->name, b->get ? b->get() : -1);
}

// ---- 普通 | 课题: an ivory thumb slides to the chosen side ----
static Handle g_segThumb, g_segLabel[2];
static float g_segX = 0, g_segHalf = 0;  // thumb x now, and its target distance from the centre

static float segTarget() { return g_cfg.course ? g_segHalf : -g_segHalf; }
static void paintModeSeg() {
    for (int i = 0; i < 2; i++) {
        Obj* l = (Obj*)g_segLabel[i].get();
        bool on = (i == 1) == g_cfg.course;
        if (alive(l)) setColor(l, rgb(on ? kInk : kDim));
    }
}
// the language changed while song select is up: every label again, in the new language and font
void songButtonsRelabel() {
    if (g_rowBh <= 0) return;
    for (SongBtn& b : g_sb)
        if (b.label.h && b.name) retext((Obj*)b.label.get(), songBtnLabel(b.name), labelSize(), true);
    const char* const modes[] = {T("普通", "Normal"), T("课题", "Course")};
    for (int i = 0; i < 2; i++) retext((Obj*)g_segLabelH[i].get(), modes[i], labelSize(), true);
    dockLayout();
    songButtonsRepaint();
    paintModeSeg();
    LOG("song select: labels switched to %s", langEn() ? "English" : "中文");
}

static void onModeSeg(void* user) {
    bool course = user != nullptr;
    if (course == g_cfg.course) return;
    g_cfg.course = course;
    configSave();
    paintModeSeg();
    LOG("song select: course=%d", g_cfg.course);
    Obj* ss = (Obj*)g_ss.get();
    if (alive(ss)) courseSongSelect(ss);  // its records (course / normal) are what song select shows now
}

static void pinRow() {
    Obj* row = (Obj*)g_btnRow.get();
    Obj* mtr = (Obj*)g_mirrorTr.get();
    if (!alive(row) || !alive(mtr)) return;
    Vec3 p = worldPos(mtr);
    setWorldPos(rt(row), Vec3{p.x + g_rowDx, p.y + g_rowDy, p.z});
}
// While our panel, the details card or a save dialog is open, song select must not scroll or swipe under it. The
// game's buttons are already covered by our backdrop, but SongSelector.Update reads the touches itself; it skips
// that while its own glitch effect's input-block flag is set, so we set the flag too (and only clear what we set).
static bool g_inputBlocked = false;
static void blockSongSelectInput() {
    Obj* ss = (Obj*)g_ss.get();
    if (!alive(ss) || P.ssBlockInput < 0) {
        g_inputBlocked = false;
        return;
    }
    bool want = panelIsOpen() || detailsCardOpen() || saveDialogOpen();
    if (want == g_inputBlocked) return;
    if (want) {
        if (!fld<bool>(ss, P.ssBlockInput)) {  // already set = the game's glitch is blocking; leave it to the game
            fld<bool>(ss, P.ssBlockInput) = true;
            g_inputBlocked = true;
        }
    } else {
        fld<bool>(ss, P.ssBlockInput) = false;
        g_inputBlocked = false;
    }
}

// the bar under the cover -> where the dock's first line is centred (needs g_dockK / g_dockAy / g_mirrorTr set)
static bool dockBar(Obj* ss) {
    Obj* mtr = (Obj*)g_mirrorTr.get();
    if (!alive(mtr) || g_dockK <= 1e-6f) return false;
    float sx0 = 0, sx1 = 0, sy = 0;
    bool exact = false;
    if (!songBarSpan(ss, &sx0, &sx1, &sy, &exact)) return false;
    Vec3 p = worldPos(mtr);
    g_dockCentred = true;
    g_dockExact = exact;
    g_dockAx = (sx0 + sx1) / 2 - p.x;
    g_dockMaxW = (sx1 - sx0) / g_dockK;
    g_dockBarY = sy - p.y;
    g_dockSlant = g_dockBarY > g_dockAy;  // the dock is under the bar, as expected
    return true;
}

// Song select opened on a locked song (no rank badge): the bar was estimated or missing, so measure again now and
// then; once a song with a badge is shown the dock moves to the exact place and stays there.
static void dockRetry() {
    if (!g_btnRow.h || g_dockExact || g_dockK <= 1e-6f) return;
    g_dockRetry += unscaledDt();
    if (g_dockRetry < 0.5f) return;
    g_dockRetry = 0;
    Obj* ss = (Obj*)g_ss.get();
    if (!alive(ss)) return;
    bool was = g_dockCentred;
    float ax = g_dockAx;
    if (!dockBar(ss)) return;
    if (g_dockExact || !was || (g_dockAx - ax) * (g_dockAx - ax) > g_dockK * g_dockK) {
        dockLayout();
        LOG("song select: dock re-centred (%s bar)", g_dockExact ? "exact" : "estimated");
    }
}

static void toggleFrame(void*) {
    blockSongSelectInput();
    dockRetry();
    challengeSongSelectFrame();
    if (g_btnRow.h) {  // the game's course mode puts its 1st / 2nd / 3rd bar where the dock is: hide the dock there
        Obj* row = (Obj*)g_btnRow.get();
        bool hide = challengeUiActive();  // compared with the dock itself: every song select builds a new one
        if (alive(row) && activeSelf(gameObject(row)) == hide) setActive(gameObject(row), !hide);
        pinRow();
    }
    Obj* th = (Obj*)g_segThumb.get();
    if (!alive(th)) return;
    float tx = segTarget();
    if (g_segX != tx) {  // ease toward the chosen side
        float k = unscaledDt() * 14;
        if (k > 1) k = 1;
        g_segX += (tx - g_segX) * k;
        if ((tx - g_segX) * (tx - g_segX) < 0.01f) g_segX = tx;
        setPos(rt(th), Vec2{g_segX, 0});
    }
}

// a dark-glass rounded box with a hairline border: returns the border (the button), *fill = the inside
static Obj* glassBox(Obj* parent, const char* name, Vec2 pos, Vec2 size, Color border, Color fill, Obj** fillOut) {
    float r = size.y * 0.48f;  // a full pill (the sprite's border is a little wider than its radius)
    Obj* b = image(parent, name, SPR_ROUND, border, Vec2{0.5f, 0.5f}, pos, size, r, true);
    Obj* f = image(rt(b), "fill", SPR_ROUND, fill, Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{size.x - 3, size.y - 3}, r - 1.5f);
    if (fillOut) *fillOut = f;
    return b;
}

static void hk_SS_Start(Obj* self, const MethodInfo* m) {
    orig_SS_Start(self, m);
    g_inputBlocked = false;  // a new song select: its flag starts clear
    configLoad();
    g_ss.set(self);
    courseSongSelect(self);  // course mode on: song select shows the course records
    challengeSongSelectStart(self);  // the game's own course mode: per-song mirror pills
    detailsSongSelect(self);  // 4-decimal accuracy, judgement line, details card
    Obj* mgo = asGo(P.ssMirror >= 0 ? fld<Obj*>(self, P.ssMirror) : nullptr);
    if (!mgo) return;
    Obj* mtr = goTransform(mgo);
    g_ss.set(self);
    bool mirrorAvailable = activeSelf(mgo);

    // size and place, from the Mirror switch: local size of its button, world bounds of all its parts
    RectInfo ri;
    Rect r{0, 0, 0, 0};
    float w = 80, h = 50;
    if (rectInfo(mtr, &ri)) {
        rectOf(mtr, &r);
        w = r.w > 1 ? r.w : ri.size.x;
        h = r.h > 1 ? r.h : ri.size.y;
    }
    float bx0 = 0, bx1 = 0, by0 = 0, by1 = 0, ux0 = 0;
    bool haveBg = worldBounds(mtr, &bx0, &bx1, &by0, &by1);
    ux0 = bx0;
    static const char* const parts[] = {"mirrorBackground", "mirrorIcon", "mirrorTextSize", "mirrorTextDisplay"};
    Il2CppClass* ssk = il2cpp_object_get_class(self);
    Obj* hidden[8];
    int nHidden = 0;
    hidden[nHidden++] = mtr;
    for (const char* part : parts) {
        int off = fieldOffset(ssk, part);
        Obj* o = off >= 0 ? fld<Obj*>(self, off) : nullptr;
        if (!alive(o)) continue;
        float x0, x1, y0, y1;
        if (worldBounds(rt(o), &x0, &x1, &y0, &y1) && x0 < ux0) ux0 = x0;
        hideUi(asGo(o));  // the original Mirror switch: invisible and click-through
        if (nHidden < 8) hidden[nHidden++] = rt(o);
    }
    hideUi(mgo);
    // the row's parent must not be (inside) a hidden part, or the CanvasGroup alpha 0 hides our buttons too
    auto underHidden = [&](Obj* t) {
        for (int guard = 0; alive(t) && guard < 64; guard++, t = parentOf(t))
            for (int i = 0; i < nHidden; i++)
                if (t == hidden[i]) return true;
        return false;
    };
    Obj* host = parentOf(mtr);
    int hops = 0;
    while (alive(host) && underHidden(host) && hops < 64) {
        host = parentOf(host);
        hops++;
    }
    if (!alive(host)) host = parentOf(mtr);

    // the dock, in the Mirror button's local units: one height for everything (dockLayout places the parts)
    float bh = h * 1.12f;
    for (SongBtn& b : g_sb) {  // release last visit's handles
        b.border.clear();
        b.fill.clear();
        b.label.clear();
    }
    g_segThumb.clear();
    g_segLabel[0].clear();
    g_segLabel[1].clear();
    int n = 0;
    auto def = [&](bool (*get)(), void (*toggle)(), const char* name) {
        g_sb[n].get = get;
        g_sb[n].toggle = toggle;
        g_sb[n].name = name;
        n++;
    };
    if (mirrorAvailable && MIR.toggleMirror) def(getMirror, toggleMirror, "mirror");
    def(getPractice, togglePractice, "practice");
    def(getAuto, toggleAuto, "autoplay");
    g_rowBh = bh;
    g_nToggles = n;
    float bw = dockBw();
    int ls = labelSize();

    Obj* row = newUI("PhiMeSongButtons", host);
    Vec2 c{0.5f, 0.5f};
    setLayout(row, hops ? c : ri.anchorMin, hops ? c : ri.anchorMin, c);
    // toggles
    for (int i = 0; i < n; i++) {
        SongBtn& b = g_sb[i];
        Obj* fill;
        Obj* border = glassBox(row, "btn", Vec2{0, 0}, Vec2{bw, bh}, kHairline, kGlass, &fill);
        Obj* lb = text(rt(border), songBtnLabel(b.name), ls, rgb(kLight), MiddleCenter, true, Vec2{0.5f, 0.5f},
                       Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{bw - bh * 0.5f, bh});
        b.border.set(border);
        b.fill.set(fill);
        b.label.set(lb);
        onClick(gameObject(border), border, onSongBtn, &b);
        paintSongBtn(b);
    }
    // 普通 | 课题
    float segW = dockSegW(), pad = bh * 0.09f;
    Obj* trackFill;
    g_segTrack.set(glassBox(row, "PhiMeMode", Vec2{0, 0}, Vec2{segW, bh}, kHairline, kGlass, &trackFill));
    float tw = (segW - 3 - 2 * pad) / 2, thh = bh - 3 - 2 * pad;
    g_segHalf = tw / 2;
    g_segX = segTarget();
    Obj* thumb = image(rt(trackFill), "thumb", SPR_ROUND, rgb(kIvory), Vec2{0.5f, 0.5f}, Vec2{g_segX, 0},
                       Vec2{tw, thh}, thh * 0.48f);
    g_segThumb.set(thumb);
    const char* const modes[] = {T("普通", "Normal"), T("课题", "Course")};
    for (int i = 0; i < 2; i++) {
        float sx = i ? g_segHalf : -g_segHalf;
        Obj* l = text(rt(trackFill), modes[i], ls, rgb(kDim), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
                      Vec2{sx, 1}, Vec2{tw - bh * 0.4f, bh});
        g_segLabel[i].set(l);
        g_segLabelH[i].set(l);
        // each half is its own (invisible) button, on top of thumb and labels
        Obj* hit = image(rt(trackFill), "half", SPR_SOLID, Color{1, 1, 1, 0}, Vec2{0.5f, 0.5f}, Vec2{sx, 0},
                         Vec2{tw, bh}, 0, true);
        onClick(gameObject(hit), nullptr, onModeSeg, i ? (void*)1 : nullptr);
    }
    paintModeSeg();
    // the PhiMe entry: Claude-orange border, Clawd + label
    Obj* entryFill;
    Obj* entry = glassBox(row, "PhiMeEntry", Vec2{0, 0}, Vec2{bh * 4, bh}, rgb(kClay), kGlass, &entryFill);
    float ch = bh * 0.55f, cw = ch * 128.f / 72.f;
    g_entry.set(entry);
    g_entryFill.set(entryFill);
    g_entryClawd.set(image(rt(entry), "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{cw, ch}));
    g_entryLabel.set(text(rt(entry), "", ls, rgb(kLight), MiddleLeft, true, Vec2{0.5f, 0.5f}, Vec2{0, 0.5f},
                          Vec2{0, 1}, Vec2{10, bh}));
    onClick(gameObject(entry), entry, [](void*) { panelOpen(); }, nullptr);
    setSize(row, Vec2{100, bh});

    // world placement: the first line where the Mirror switch was (vertically), centred under the level tabs ..
    // rank badge bar; without that bar, its left edge where Mirror's leftmost part was
    g_mirrorTr.set(mtr);
    g_btnRow.set(row);
    g_dockK = g_dockAx = g_dockAy = g_dockMaxW = 0;
    g_dockCentred = false;
    g_dockSlant = false;
    g_dockExact = false;
    g_dockRetry = 0;
    bool haveBar = false;
    if (haveBg) {
        float k = (bx1 - bx0) / (w > 1 ? w : 1);  // world units per local unit
        // match the Mirror's world scale even if the host has a different one
        float rx0, rx1, ry0, ry1;
        if (worldBounds(row, &rx0, &rx1, &ry0, &ry1) && rx1 - rx0 > 1e-6f) setScale(row, 100 * k / (rx1 - rx0));
        Vec3 p = worldPos(mtr);
        g_dockK = k;
        g_dockAy = (by0 + by1) / 2 - p.y;
        haveBar = dockBar(self);
        if (!haveBar) g_dockAx = ux0 - p.x;
    }
    dockLayout();
    static bool tickRegistered = false;
    if (!tickRegistered) {
        tickRegistered = true;
        onEveryFrame(toggleFrame);
    }
    char hn[64] = "?";
    objName(host, hn, sizeof hn);
    LOG("song select: dock of %d toggles + mode + entry, button height %.0f, %s, row offset %.2f,%.2f, host %s "
        "(+%d up)",
        n, bh,
        !haveBar ? "left-aligned (no level bar)"
                 : g_dockExact ? "centred under the level bar" : "centred under the estimated level bar",
        g_rowDx, g_rowDy, hn, hops);
}

// ---------------- install ----------------
bool practiceInstall() {
    Il2CppClass* PC = findClass("", "ProgressControl");
    Il2CppClass* AAS = findClass("", "AddressableAudioSource");
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* JL = findClass("", "JudgeLineControl");
    Il2CppClass* NUM = findClass("", "NoteUpdateManager");
    Il2CppClass* Note = findClass("", "ChartNote");
    Il2CppClass* Score = findClass("", "ScoreControl");
    Il2CppClass* SS = findClass("", "SongSelector");
    P.FilterTrigger = findClass("Cysharp.Threading.Tasks.Triggers", "AsyncAudioFilterReadTrigger");
    if (!PC || !AAS || !JC || !JL || !NUM || !Note) return false;

    P.pcNow = fieldOffset(PC, "nowTime");
    P.pcOffset = fieldOffset(PC, "_offset");
    P.pcAas = fieldOffset(PC, "audioSource");
    P.pcJc = fieldOffset(PC, "judgeControl");
    P.pcScore = fieldOffset(PC, "scoreControl");
    P.pcPlaying = fieldOffset(PC, "isPlaying");
    P.pcPauseBar = fieldOffset(PC, "pauseBar");
    P.pcAudioTime = fieldOffset(PC, "audioTime");
    P.pcPlay = findMethod(PC, "Play", 1, IL2CPP_TYPE_BOOLEAN);
    P.aasTime = fieldOffset(AAS, "time");
    P.aasSrc = fieldOffset(AAS, "audioSource");
    P.aasKey = fieldOffset(AAS, "clipKey");
    P.aasSeek = findMethod(AAS, "Seek", 1);
    P.aasLen = findMethod(AAS, "get_ClipLength", 0);
    P.jcLines = fieldOffset(JC, "judgeLineControls");
    P.jcSorted = fieldOffset(JC, "chartNoteSortByTime");
    P.jcStart = fieldOffset(JC, "startIndex");
    P.jcEnd = fieldOffset(JC, "endIndex");
    P.jcStop = fieldOffset(JC, "stopJudge");
    P.jcNum = fieldOffset(JC, "noteUpdateManager");
    P.jlAbove = fieldOffset(JL, "notesAbove");
    P.jlBelow = fieldOffset(JL, "notesBelow");
    P.jlSpeedIdx = fieldOffset(JL, "nowSpeedIndex");
    P.jlDisIdx = fieldOffset(JL, "nowDisappearIndex");
    P.jlMoveIdx = fieldOffset(JL, "nowMoveIndex");
    P.jlRotIdx = fieldOffset(JL, "nowRotateIndex");
    P.jlCreate = findMethod(JL, "CreateNote", 2);
    P.numLists[0] = fieldOffset(NUM, "clickControls");
    P.numLists[1] = fieldOffset(NUM, "dragControls");
    P.numLists[2] = fieldOffset(NUM, "holdControls");
    P.numLists[3] = fieldOffset(NUM, "flickControls");
    P.noteTime = fieldOffset(Note, "realTime");
    P.noteJudged = fieldOffset(Note, "isJudged");
    P.noteFlick = fieldOffset(Note, "isJudgedForFlick");
    P.scoreCombo = Score ? fieldOffset(Score, "_combo") : -1;
    P.pcStartTime = fieldOffset(PC, "_startTime");
    P.pcFirst = fieldOffset(PC, "_isFirstTimeLaunch");
    P.pcFirstGuide = fieldOffset(PC, "_isFirstTimeLaunchInGuide");
    P.scComboLabel = Score ? fieldOffset(Score, "comboText") : -1;
    P.scComboNum = Score ? fieldOffset(Score, "combo") : -1;
    {
        static const char* const fl[] = {"_score", "_percent"};
        static const char* const in[] = {"_combo", "maxcombo", "perfect", "good", "bad", "miss", "early", "late"};
        static const char* const fg[] = {"isAllPerfect", "isFullCombo"};
        for (int i = 0; i < 2; i++) P.scoreFloats[i] = Score ? fieldOffset(Score, fl[i]) : -1;
        for (int i = 0; i < 8; i++) P.scoreInts[i] = Score ? fieldOffset(Score, in[i]) : -1;
        for (int i = 0; i < 2; i++) P.scoreFlags[i] = Score ? fieldOffset(Score, fg[i]) : -1;
    }
    P.Blocks = findClass("Phigros2.Level.Scripts", "PreviewElementUpdateControl");
    P.blocksRecreate = P.Blocks ? findMethod(P.Blocks, "DestroyAndCreateAllBlocks", 0) : nullptr;
    P.blocksHas = P.Blocks ? findMethod(P.Blocks, "get_HasBlocks", 0) : nullptr;
    P.blocksGet = P.Blocks ? findMethod(P.Blocks, "get_Blocks", 0) : nullptr;
    P.scUnblock = Score ? findMethod(Score, "UnblockCombo", 0) : nullptr;
    P.scBlocked = Score ? fieldOffset(Score, "comboBlocked") : -1;
    {
        Il2CppClass* LO = findNestedPrefix(PC, "<LevelOver>d__");
        const MethodInfo* next = LO ? findMethod(LO, "MoveNext", 0) : nullptr;
        if (next) orig_LevelOverNext = (MoveNextFn)hookMethod(next, (void*)hk_LevelOverNext);
        Il2CppClass* LI = findClass("", "LevelInformation");
        g_pcLevelInfo = fieldOffset(PC, "levelInformation");
        g_liOver = LI ? fieldOffset(LI, "levelOver") : -1;
        // the note flow: the chart's speed events + note floor positions (see applyFlow)
        g_jlSpeedEv = fieldOffset(JL, "speedEvents");
        g_noteFloor = fieldOffset(Note, "floorPosition");
        g_noteSpeed = fieldOffset(Note, "speed");
        g_noteType = fieldOffset(Note, "type");
        Il2CppClass* SE = findClass("", "SpeedEvent");
        if (SE) {
            g_evFloor = fieldOffset(SE, "floorPosition");
            g_evValue = fieldOffset(SE, "value");
            g_evValueType = il2cpp_class_is_valuetype(SE);
            g_evAdj = g_evValueType ? (int)sizeof(Il2CppObject) : 0;
            g_evStride = g_evValueType ? il2cpp_class_instance_size(SE) - (int)sizeof(Il2CppObject) : 8;
            if (g_evValue < 0) g_evFloor = -1;
        }
        LOG("practice: flow speed %s (SpeedEvent %s)", flowReady() ? "on (chart scaling)" : "OFF (chart fields missing)",
            SE ? (g_evValueType ? "struct" : "class") : "missing");
        LOG("practice: result screen guard %s (levelOver flag %s)", orig_LevelOverNext ? "on" : "MISSING",
            g_liOver >= 0 ? "found" : "missing");
    }
    P.ssMirror = SS ? fieldOffset(SS, "mirrorBackground") : -1;
    P.ssMirrorIcon = SS ? fieldOffset(SS, "mirrorIcon") : -1;
    P.ssBlockInput = SS ? fieldOffset(SS, "implexroughRandomGlitchBlockingInput") : -1;
    if (P.ssBlockInput < 0) LOG("song select: no input-block flag, the list stays touchable under the panel");
    if (SS) {
        MIR.mirrorOn = fieldOffset(SS, "enableChartMirror");
        MIR.toggleMirror = findMethod(SS, "ToggleChartMirror", 0);
    }

    const int must[] = {P.pcNow, P.pcOffset, P.pcAas, P.pcJc, P.pcScore, P.pcPlaying, P.pcPauseBar,
                        P.pcAudioTime, P.pcStartTime, P.aasTime, P.aasSrc,
                        P.jcLines, P.jcSorted, P.jcStart, P.jcEnd, P.jcStop, P.jcNum, P.jlAbove, P.jlBelow,
                        P.jlSpeedIdx, P.jlDisIdx, P.jlMoveIdx, P.jlRotIdx, P.numLists[0], P.numLists[1],
                        P.numLists[2], P.numLists[3], P.noteTime, P.noteJudged, P.noteFlick};
    for (int v : must)
        if (v < 0) return false;
    if (!P.pcPlay || !P.aasSeek || !P.jlCreate) return false;

    orig_AAS_Update = (VoidFn)hookMethod(findMethod(AAS, "Update", 0), (void*)hk_AAS_Update);
    if (Score && P.scoreCombo >= 0 && P.scComboLabel >= 0 && P.scComboNum >= 0)
        orig_SC_Update = (VoidFn)hookMethod(findMethod(Score, "Update", 0), (void*)hk_SC_Update);
    if (P.FilterTrigger) {
        const MethodInfo* f = findMethod(P.FilterTrigger, "OnAudioFilterRead", 2);
        if (f) orig_Filter = (FilterFn)hookMethod(f, (void*)hk_Filter);
        else P.FilterTrigger = nullptr;
    }
    if (!P.FilterTrigger) LOG("practice: no audio filter hook, pitch will follow speed");
    if (SS && P.ssMirror >= 0) orig_SS_Start = (VoidFn)hookMethod(findMethod(SS, "Start", 0), (void*)hk_SS_Start);
    return orig_AAS_Update != nullptr;
}
