// Resume with a prepare time of our own (setting: 0 - 3 s, 0.1 s steps) that replays the chart.
//
// The game's resume: the pause menu's "continue" spawns its countdown animation (Pause.continueAnimation,
// always 3 s) and calls ProgressControl.Play(true), which leaves _startTime = -1. The next
// ProgressControl.Update turns that into now + 3, schedules the music 1 s before _startTime and from
// _startTime on drives nowTime from the music. Play is called from compiled code (not hookable), so we spot
// the -1 marker in our ProgressControl.Update hook, before the original runs, and take over.
//
// The replay is the real playback, rewound: the music is put back to (pause time - D) and started right
// away (fading in), with _startTime = now, so the game drives nowTime from the music the whole time, exactly
// like normal play. When the chart reaches the pause time nothing has to switch clocks: touch judging simply
// comes back. (v0.8.0 ran the replay on the frame clock and handed over to the music clock at the end; the
// two never line up to the frame, which showed as a small hitch.)
//
// The replay never touches the real judgement:
//   - the real note objects stay exactly where they are (every note not judged yet has one): with the clock
//     before the pause time they are simply drawn further away, and nothing can judge them early or late;
//   - notes already played before the pause get a temporary copy (JudgeLineControl.CreateNote) that we hit
//     on time: Perfect effect and hit sound, and the copy is gone again on its own once hit;
//   - ScoreControl's value fields (score, combo, counts, ...) are restored around every note update of the
//     replay, so its Perfects show but never count; the ChartNote judged flags are restored at the end;
//   - touch judging is off while it runs (the pause button still works; pausing puts everything back at the
//     pause point, music included).
// Near the very start (less than D of chart before the pause) the chart waits at 0 for the rest of D first.
#include "practice.h"

using namespace il2cpp;
using namespace unity;

static struct {
    int pcNow, pcPlaying, pcStart, pcPrepared, pcVolSet, pcFirst, pcFirstGuide, pcJc, pcScore, pcAas,
        pcAudioTime, pcOffset, pcPauseBar = -1;
    int aasStart;
    const MethodInfo *aasSchedule, *aasGetVol, *aasSetVol;
    int jcNum, jcLines, jcStop;
    int numLists[4];  // click, drag, hold, flick
    int ctlNote[4];   // noteInfor of each control type
    int dragJudged, holdJudged, holdSafe;
    int jlAbove, jlBelow, jlIdx[4];
    const MethodInfo* jlCreate;
    int noteTime, noteJudged, noteFlick;
    int listItems = -1, listSize = -1;
} R;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_PC_Update;

// ---- pointer set (open addressing; ChartNotes are kept alive by their judge lines) ----
struct PtrSet {
    static const int kCap = 1 << 15;
    void** slot = nullptr;
    int n = 0;
    bool init() {
        if (!slot) slot = (void**)calloc(kCap, sizeof(void*));
        return slot != nullptr;
    }
    void clear() {
        if (slot && n) memset(slot, 0, kCap * sizeof(void*));
        n = 0;
    }
    static unsigned hash(void* p) {
        uintptr_t x = (uintptr_t)p >> 3;
        x ^= x >> 15;
        return (unsigned)(x * 2654435761u) & (kCap - 1);
    }
    bool add(void* p) {
        if (!p || n >= kCap / 2) return false;
        for (unsigned i = hash(p);; i = (i + 1) & (kCap - 1)) {
            if (slot[i] == p) return true;
            if (!slot[i]) {
                slot[i] = p;
                n++;
                return true;
            }
        }
    }
    bool has(void* p) const {
        if (!p || !n) return false;
        for (unsigned i = hash(p);; i = (i + 1) & (kCap - 1)) {
            if (slot[i] == p) return true;
            if (!slot[i]) return false;
        }
    }
};

// ---- session state ----
static bool g_session = false;  // our prepare countdown is running
static bool g_active = false;   // ... and the chart replay with it
static float g_T = 0;       // countdown end (timeSinceLevelLoad), countdown without replay
static float g_Tmusic = 0;  // when the rewound music starts (timeSinceLevelLoad)
static float g_pauseT = 0, g_pauseClip = 0, g_t0 = 0, g_span = 0, g_rate = 1;  // span in real seconds
static float g_volTarget = -1;  // the game's music volume, read once it has set it
static Handle g_pcH, g_numH, g_jcH;
static bool g_jcStop;

struct Saved {
    Obj* note;
    bool judged, flick;
};
static const int kSavedCap = 4096;
static Saved* g_saved;
static int g_savedN;
static PtrSet g_live, g_ghost;  // ChartNotes with a real object / with a replay copy

// ---- small helpers ----
static int listView(Obj* list, Obj*** out) {
    if (!list) return 0;
    if (R.listItems < 0) {
        Il2CppClass* k = il2cpp_object_get_class(list);
        R.listItems = fieldOffset(k, "_items");
        R.listSize = fieldOffset(k, "_size");
        if (R.listItems < 0 || R.listSize < 0) return 0;
    }
    Il2CppArray* items = fld<Il2CppArray*>(list, R.listItems);
    int n = fld<int>(list, R.listSize);
    if (!items || n <= 0 || (uintptr_t)n > items->max_length) return 0;
    *out = (Obj**)arrayData(items);
    return n;
}

static void resetLineIndices(Obj* jc) {
    Obj** lines = nullptr;
    int nl = listView(fld<Obj*>(jc, R.jcLines), &lines);
    for (int l = 0; l < nl; l++)
        if (alive(lines[l]))
            for (int off : R.jlIdx) fld<int>(lines[l], off) = 0;  // event indices only scan forward
}

// ---- ScoreControl snapshot: every value-type field, plus the size of every List it owns ----
struct ValField {
    int off, size;
};
static ValField g_vf[96];
static int g_vfN = 0, g_vfBytes = 0;
static int g_refOff[32], g_refN = 0;
static unsigned char g_scBuf[2048];
static int g_scLists[32];

static void scoreLayout(Il2CppClass* k) {
    struct F {
        int off;
        bool val;
    } fs[128];
    int n = 0;
    void* it = nullptr;
    while (FieldInfo* f = il2cpp_class_get_fields(k, &it)) {
        if (il2cpp_field_get_flags(f) & 0x50) continue;  // static / literal
        if (n >= 128) break;
        const Il2CppType* t = il2cpp_field_get_type(f);
        Il2CppClass* fk = t ? il2cpp_class_from_type(t) : nullptr;
        fs[n].off = (int)il2cpp_field_get_offset(f);
        fs[n].val = fk && il2cpp_class_is_valuetype(fk);
        n++;
    }
    for (int i = 1; i < n; i++)  // by offset
        for (int j = i; j > 0 && fs[j].off < fs[j - 1].off; j--) {
            F tmp = fs[j];
            fs[j] = fs[j - 1];
            fs[j - 1] = tmp;
        }
    int inst = il2cpp_class_instance_size(k);
    g_vfN = g_vfBytes = g_refN = 0;
    for (int i = 0; i < n; i++) {
        int end = i + 1 < n ? fs[i + 1].off : inst;
        int size = end - fs[i].off;
        if (size <= 0) continue;
        if (fs[i].val) {
            if (g_vfN < 96 && g_vfBytes + size <= (int)sizeof(g_scBuf)) {
                g_vf[g_vfN++] = ValField{fs[i].off, size};
                g_vfBytes += size;
            }
        } else if (g_refN < 32) {
            g_refOff[g_refN++] = fs[i].off;
        }
    }
}

static int listSizeOffset(Obj* o) {
    if (!o) return -1;
    Il2CppClass* k = il2cpp_object_get_class(o);
    const char* nm = il2cpp_class_get_name(k);
    if (!nm || strcmp(nm, "List`1") != 0) return -1;
    return fieldOffset(k, "_size");
}

static void scoreSave(Obj* sc) {
    int p = 0;
    for (int i = 0; i < g_vfN; i++) {
        memcpy(g_scBuf + p, (char*)sc + g_vf[i].off, g_vf[i].size);
        p += g_vf[i].size;
    }
    for (int i = 0; i < g_refN; i++) {
        Obj* o = fld<Obj*>(sc, g_refOff[i]);
        int so = listSizeOffset(o);
        g_scLists[i] = so >= 0 ? fld<int>(o, so) : -1;
    }
}

static void scoreRestore(Obj* sc) {
    int p = 0;
    for (int i = 0; i < g_vfN; i++) {
        memcpy((char*)sc + g_vf[i].off, g_scBuf + p, g_vf[i].size);
        p += g_vf[i].size;
    }
    for (int i = 0; i < g_refN; i++) {
        if (g_scLists[i] < 0) continue;
        Obj* o = fld<Obj*>(sc, g_refOff[i]);
        int so = listSizeOffset(o);
        if (so >= 0 && fld<int>(o, so) > g_scLists[i]) fld<int>(o, so) = g_scLists[i];  // drop what was added
    }
}

// ---- replay begin / end ----
static bool ensureBuffers() {
    if (!g_saved) g_saved = (Saved*)malloc(sizeof(Saved) * kSavedCap);
    return g_saved && g_live.init() && g_ghost.init();
}

static bool begin(Obj* pc) {
    Obj* jc = fld<Obj*>(pc, R.pcJc);
    Obj* num = alive(jc) ? fld<Obj*>(jc, R.jcNum) : nullptr;
    if (!alive(num) || !ensureBuffers()) return false;
    float t0 = g_t0;
    float from = t0 - 0.02f;
    // a practice run only ever had the notes of its section
    if (g_prac.st == PR_RUN && from < g_prac.a - 0.02f) from = g_prac.a - 0.02f;

    // 1. which notes still have their real object (those are left alone)
    g_live.clear();
    g_ghost.clear();
    g_savedN = 0;
    for (int k = 0; k < 4; k++) {
        Obj** e = nullptr;
        int n = listView(fld<Obj*>(num, R.numLists[k]), &e);
        for (int i = 0; i < n; i++)
            if (e[i]) g_live.add(fld<Obj*>(e[i], R.ctlNote[k]));
    }

    // 2. replay copies of the notes played in [from, pause time]
    int created = 0;
    Obj** lines = nullptr;
    int nl = listView(fld<Obj*>(jc, R.jcLines), &lines);
    for (int l = 0; l < nl; l++) {
        Obj* jl = lines[l];
        if (!alive(jl)) continue;
        for (int side = 0; side < 2; side++) {
            bool above = side == 0;
            Obj** notes = nullptr;
            int nn = listView(fld<Obj*>(jl, above ? R.jlAbove : R.jlBelow), &notes);
            for (int i = 0; i < nn && g_savedN < kSavedCap; i++) {
                Obj* note = notes[i];
                if (!note || g_live.has(note)) continue;
                float t = fld<float>(note, R.noteTime);
                if (t < from || t > g_pauseT) continue;
                Saved& s = g_saved[g_savedN++];
                s.note = note;
                s.judged = fld<bool>(note, R.noteJudged);
                s.flick = fld<bool>(note, R.noteFlick);
                fld<bool>(note, R.noteJudged) = false;
                fld<bool>(note, R.noteFlick) = false;
                g_ghost.add(note);
                int idx = i;
                void* args[] = {&idx, &above};
                if (invokeSafe(R.jlCreate, jl, args)) created++;
            }
        }
    }

    // 3. touch judging off, lines rewound (their event indices only move forward)
    g_jcStop = fld<bool>(jc, R.jcStop);
    fld<bool>(jc, R.jcStop) = true;
    resetLineIndices(jc);

    g_numH.set(num);
    g_jcH.set(jc);
    g_active = true;
    LOG("resume: replay %.2f -> %.2f (%.1fs real), %d copies of played notes, %d real notes untouched", t0,
        g_pauseT, g_span, created, g_live.n);
    return true;
}

static void removeAt(Obj* list, int i) {
    static Il2CppClass* cls[4];
    static const MethodInfo* fn[4];
    Il2CppClass* k = il2cpp_object_get_class(list);
    const MethodInfo* m = nullptr;
    for (int j = 0; j < 4; j++) {
        if (cls[j] == k) {
            m = fn[j];
            break;
        }
        if (!cls[j]) {
            cls[j] = k;
            fn[j] = m = findMethod(k, "RemoveAt", 1);
            break;
        }
    }
    void* args[] = {&i};
    if (m) invokeSafe(m, list, args);
}

static void setMusicVolume(Obj* pc, float v) {
    Obj* aas = alive(pc) ? fld<Obj*>(pc, R.pcAas) : nullptr;
    if (!alive(aas) || !R.aasSetVol) return;
    void* args[] = {&v};
    invokeSafe(R.aasSetVol, aas, args);
}

// handover = the replay reached the pause point and play simply goes on (music untouched, full volume);
// otherwise (paused again, practice seek, ...) the chart and the music position go back to the pause point.
static void endReplay(bool levelAlive, bool handover) {
    if (!g_active) return;
    g_active = false;
    Obj* num = (Obj*)g_numH.get();
    Obj* jc = (Obj*)g_jcH.get();
    Obj* pc = (Obj*)g_pcH.get();
    if (levelAlive && alive(num)) {
        // copies not hit yet (right at the pause time): out of the lists before their flags go back
        int left = 0;
        for (int k = 0; k < 4; k++) {
            Obj* list = fld<Obj*>(num, R.numLists[k]);
            Obj** e = nullptr;
            int n = listView(list, &e);
            for (int i = n - 1; i >= 0; i--) {
                listView(list, &e);
                Obj* c = e[i];
                if (!c || !g_ghost.has(fld<Obj*>(c, R.ctlNote[k]))) continue;
                if (alive(c)) destroy(gameObject(c));
                removeAt(list, i);
                left++;
            }
        }
        for (int i = 0; i < g_savedN; i++) {
            fld<bool>(g_saved[i].note, R.noteJudged) = g_saved[i].judged;
            fld<bool>(g_saved[i].note, R.noteFlick) = g_saved[i].flick;
        }
        if (alive(jc)) fld<bool>(jc, R.jcStop) = g_jcStop;
        if (alive(pc)) {
            if (handover) {
                if (g_volTarget >= 0) setMusicVolume(pc, g_volTarget);
            } else {
                fld<float>(pc, R.pcNow) = g_pauseT;
                fld<float>(pc, R.pcAudioTime) = g_pauseClip;  // the next "continue" starts from here again
            }
        }
        LOG("resume: replay %s at %.2f (%d copies removed)", handover ? "reached the pause point" : "cancelled",
            alive(pc) ? fld<float>(pc, R.pcNow) : g_pauseT, left);
    }
    g_savedN = 0;
    g_live.clear();
    g_ghost.clear();
    g_numH.clear();
    g_jcH.clear();
}

// ---- countdown overlay: dim layer + remaining time in 0.1 s steps ----
static Handle g_ovl, g_ovlDim, g_ovlNum;
static int g_ovlTenths = -1;
static Il2CppClass* g_pauseClass;
static int g_pauseAnim = -1;

static void overlayClear(bool destroyIt) {
    Obj* root = (Obj*)g_ovl.get();
    if (destroyIt) uiFadeAway(root);  // fades out, then destroyed
    g_ovl.clear();
    g_ovlDim.clear();
    g_ovlNum.clear();
    g_ovlTenths = -1;
}

// The game's own 3 s countdown (spawned by the pause menu right before Play(true)): ours replaces it.
static void removeGameCountdown(Obj* pc) {
    if (!g_pauseClass || g_pauseAnim < 0 || R.pcPauseBar < 0) return;
    Obj* bar = fld<Obj*>(pc, R.pcPauseBar);
    Obj* pause = alive(bar) ? getComponent(bar, g_pauseClass) : nullptr;
    Obj* anim = alive(pause) ? fld<Obj*>(pause, g_pauseAnim) : nullptr;
    if (!alive(anim)) return;
    const char* kn = il2cpp_class_get_name(il2cpp_object_get_class(anim));
    if (!kn || strcmp(kn, "GameObject") != 0) anim = gameObject(anim);  // a component of it
    destroy(anim);
}

static void overlayUpdate(float r) {
    if (!alive((Obj*)g_ovl.get())) {
        Obj* root = overlayCanvas("PhiMeResumeCountdown", 29000);
        Obj* dim = image(root, "dim", SPR_SOLID, Color{0, 0, 0, 0.4f}, Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{100, 100});
        stretch(rt(dim), -20);
        Obj* num = text(root, "", 150, rgb(0xFFFFFF), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
                        Vec2{0, 0}, Vec2{520, 240});
        g_ovl.set(root);
        g_ovlDim.set(dim);
        g_ovlNum.set(num);
        g_ovlTenths = -1;
    }
    Obj* dim = (Obj*)g_ovlDim.get();
    Obj* num = (Obj*)g_ovlNum.get();
    int t = (int)(r * 10 + 0.999f);  // ceil to 0.1 s: 3.0, 2.9, ... 0.1
    if (t < 1) t = 1;
    if (t != g_ovlTenths && alive(num)) {
        char b[16];
        snprintf(b, sizeof(b), "%d.%d", t / 10, t % 10);
        setText(num, b);
        g_ovlTenths = t;
    }
    float fade = r < 0.2f ? r / 0.2f : 1.f;  // melt away right before the music
    float frac = r - (float)(int)r;           // pulse as each whole second begins
    float p = frac > 0.75f ? (frac - 0.75f) / 0.25f : 0;
    if (alive(dim)) setColor(dim, Color{0, 0, 0, 0.4f * fade});
    if (alive(num)) {
        setColor(num, rgb(0xFFFFFF, 0.95f * fade));
        setScale(rt(num), 1 + 0.16f * p * p);
    }
}

// ---- session ----
static void finish(bool levelAlive, bool handover) {
    overlayClear(levelAlive);  // a dead level took the overlay with it
    endReplay(levelAlive, handover);
    g_session = false;
    g_volTarget = -1;
    if (!levelAlive) g_pcH.clear();
}

static void tick(bool afterUpdate);

void resumeAbort() {
    if (!g_session && !g_active) return;
    finish(alive((Obj*)g_pcH.get()), false);
}

bool resumeActive() {
    if (g_session) tick(false);  // JudgeControl.Update may run first in a frame: hand over on time
    return g_active;
}

// Per frame while the countdown runs: called around ProgressControl.Update, before the note update and
// before touch judging.
static void tick(bool afterUpdate) {
    if (!g_session) return;
    Obj* pc = (Obj*)g_pcH.get();
    if (!alive(pc)) {  // level gone
        finish(false, false);
        return;
    }
    if (!fld<bool>(pc, R.pcPlaying)) {  // paused again: everything back to the pause point
        finish(true, false);
        return;
    }
    bool music = fld<bool>(pc, R.pcVolSet);
    float r;
    if (g_active) {
        float now = fld<float>(pc, R.pcNow);
        if (music) {
            if (now >= g_pauseT) {  // the replay reached the pause point: judging back on, play goes on
                finish(true, true);
                return;
            }
            r = (g_pauseT - now) / g_rate;
            // the game has just set the music volume (first frame of the music): remember it, then fade in
            Obj* aas = fld<Obj*>(pc, R.pcAas);
            if (g_volTarget < 0 && afterUpdate && alive(aas) && R.aasGetVol) {
                float v = 1;
                if (invokeFloat(R.aasGetVol, aas, &v)) g_volTarget = v;
            }
            if (g_volTarget >= 0) {
                float p = g_span > 0 ? 1 - r / g_span : 1;
                if (p < 0) p = 0;
                if (p > 1) p = 1;
                setMusicVolume(pc, g_volTarget * (0.2f + 0.8f * p * p));
            }
        } else {
            r = g_Tmusic - timeSinceLevelLoad() + g_span;  // still waiting at the start of the chart
        }
    } else {  // countdown only (nothing to replay)
        r = g_T - timeSinceLevelLoad();
        if (r <= 0 || music) {
            finish(true, true);
            return;
        }
    }
    overlayUpdate(r);
}

static bool scheduleMusic(Obj* pc, float clip, float lead) {
    Obj* aas = fld<Obj*>(pc, R.pcAas);
    if (!alive(aas) || !R.aasSchedule || R.aasStart < 0) return false;
    fld<bool>(pc, R.pcPrepared) = true;  // the game's own "1 s before" scheduling stays out of it
    fld<float>(pc, R.pcAudioTime) = clip;
    fld<float>(aas, R.aasStart) = clip;
    double l = lead;
    void* args[] = {&l};
    if (!invokeSafe(R.aasSchedule, aas, args)) return false;
    practiceAudioJump(clip);
    return true;
}

// "continue" was just tapped (Play(true) left _startTime at -1): our prepare time instead of the game's 3 s
static void takeOver(Obj* pc) {
    float d = g_cfg.resumePreviewTenths / 10.f;
    float now = timeSinceLevelLoad();
    removeGameCountdown(pc);
    g_rate = g_prac.st == PR_RUN ? g_prac.speed : 1.f;
    g_pauseT = fld<float>(pc, R.pcNow);
    g_pauseClip = fld<float>(pc, R.pcAudioTime);
    float offset = fld<float>(pc, R.pcOffset);  // music time = chart time + offset
    float t0 = g_pauseT - d * g_rate;
    float earliest = -offset > 0 ? -offset : 0;  // neither before the chart nor before the music
    if (t0 < earliest) t0 = earliest;
    g_t0 = t0;
    g_span = g_pauseT > t0 ? (g_pauseT - t0) / g_rate : 0;
    g_volTarget = -1;

    if (d <= 0 || g_span < 0.05f) {
        // nothing to replay: just the countdown (or none), music from the pause point after d seconds
        g_T = now + d;
        fld<float>(pc, R.pcStart) = g_T;
        bool own = d < 1.f && scheduleMusic(pc, g_pauseClip, d);  // the game itself only does "1 s ahead"
        LOG("resume: prepare %.1fs at %.2f, no replay%s", d, g_pauseT, own ? ", music scheduled by PhiMe" : "");
        if (d > 0) {
            g_session = true;
            g_pcH.set(pc);
        }
        return;
    }

    // rewind: the music restarts at t0 after `lead` (only > 0 near the start of the chart) and the game drives
    // the chart from it as usual; we only add the copies of played notes and keep judging off until g_pauseT
    float lead = d - g_span;
    if (lead < 0) lead = 0;
    g_Tmusic = now + lead;
    g_T = now + d;
    if (!scheduleMusic(pc, t0 + offset, lead)) {
        LOG("resume: could not restart the music, game's own resume");
        fld<float>(pc, R.pcStart) = now + 3.f;
        return;
    }
    fld<float>(pc, R.pcStart) = g_Tmusic;
    fld<float>(pc, R.pcNow) = t0;
    g_pcH.set(pc);
    g_session = true;
    if (!begin(pc)) {  // no replay copies: still a correct run, just without judging off
        LOG("resume: replay setup failed");
    }
    LOG("resume: prepare %.1fs at %.2f, music rewound to %.2f (wait %.2fs)", d, g_pauseT, t0, lead);
}

static void hk_PC_Update(Obj* self, const MethodInfo* m) {
    float st = fld<float>(self, R.pcStart);
    if (!g_session && st < -0.5f && st > -1.5f && fld<bool>(self, R.pcPlaying) && !fld<bool>(self, R.pcFirst) &&
        !fld<bool>(self, R.pcFirstGuide)) {
        configLoad();
        if (g_cfg.resumePreview) takeOver(self);  // off: the game's own 3 s countdown, untouched
    }
    if (g_session) tick(false);
    orig_PC_Update(self, m);
    if (g_session) tick(true);
}

// Before NoteUpdateManager.Update: Perfect for the replay copies, score snapshot.
bool resumeBeforeNotes(Obj* num) {
    if (g_session) tick(false);
    if (!g_active || num != g_numH.get()) return false;
    Obj* pc = (Obj*)g_pcH.get();
    if (!alive(pc)) return false;
    float now = fld<float>(pc, R.pcNow);
    const float lead = 0.5f / 60;
    for (int k = 0; k < 4; k++) {
        Obj** e = nullptr;
        int n = listView(fld<Obj*>(num, R.numLists[k]), &e);
        for (int i = 0; i < n; i++) {
            Obj* c = e[i];
            if (!c) continue;
            Obj* note = fld<Obj*>(c, R.ctlNote[k]);
            if (!g_ghost.has(note)) continue;  // a real note: never touched
            float dt = fld<float>(note, R.noteTime) - now;
            switch (k) {
                case 0:  // click
                    if (dt <= lead) fld<bool>(note, R.noteJudged) = true;
                    break;
                case 1:  // drag
                    if (dt <= 0.08f && dt >= -0.08f) fld<bool>(c, R.dragJudged) = true;
                    break;
                case 2:  // hold
                    if (dt <= lead) fld<bool>(note, R.noteJudged) = true;
                    if (fld<bool>(c, R.holdJudged)) fld<int>(c, R.holdSafe) = 3;
                    break;
                case 3:  // flick
                    if (dt <= 0.08f) fld<bool>(note, R.noteFlick) = true;
                    break;
            }
        }
    }
    Obj* sc = alive(pc) ? fld<Obj*>(pc, R.pcScore) : nullptr;
    if (alive(sc)) scoreSave(sc);
    return true;
}

void resumeAfterNotes() {
    Obj* pc = (Obj*)g_pcH.get();
    Obj* sc = alive(pc) ? fld<Obj*>(pc, R.pcScore) : nullptr;
    if (alive(sc)) scoreRestore(sc);  // the replay's Perfects never count
}

bool resumeInstall() {
    Il2CppClass* PC = findClass("", "ProgressControl");
    Il2CppClass* AAS = findClass("", "AddressableAudioSource");
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* JL = findClass("", "JudgeLineControl");
    Il2CppClass* NUM = findClass("", "NoteUpdateManager");
    Il2CppClass* Note = findClass("", "ChartNote");
    Il2CppClass* Score = findClass("", "ScoreControl");
    Il2CppClass* ctl[4] = {findClass("", "ClickControl"), findClass("", "DragControl"), findClass("", "HoldControl"),
                           findClass("", "FlickControl")};
    if (!PC || !AAS || !JC || !JL || !NUM || !Note || !Score) return false;
    for (Il2CppClass* k : ctl)
        if (!k) return false;

    R.pcNow = fieldOffset(PC, "nowTime");
    R.pcPlaying = fieldOffset(PC, "isPlaying");
    R.pcStart = fieldOffset(PC, "_startTime");
    R.pcPrepared = fieldOffset(PC, "_playPrepared");
    R.pcVolSet = fieldOffset(PC, "_volumeSet");
    R.pcFirst = fieldOffset(PC, "_isFirstTimeLaunch");
    R.pcFirstGuide = fieldOffset(PC, "_isFirstTimeLaunchInGuide");
    R.pcJc = fieldOffset(PC, "judgeControl");
    R.pcScore = fieldOffset(PC, "scoreControl");
    R.pcAas = fieldOffset(PC, "audioSource");
    R.pcAudioTime = fieldOffset(PC, "audioTime");
    R.pcPauseBar = fieldOffset(PC, "pauseBar");  // optional: only to remove the game's own countdown
    R.pcOffset = fieldOffset(PC, "_offset");
    R.aasStart = fieldOffset(AAS, "startTime");
    R.aasSchedule = findMethod(AAS, "PlayScheduled", 1);
    R.aasGetVol = findMethod(AAS, "get_Volume", 0);
    R.aasSetVol = findMethod(AAS, "set_Volume", 1);
    g_pauseClass = findClass("", "Pause");
    g_pauseAnim = g_pauseClass ? fieldOffset(g_pauseClass, "instantiatedContinueAnimation") : -1;
    R.jcNum = fieldOffset(JC, "noteUpdateManager");
    R.jcLines = fieldOffset(JC, "judgeLineControls");
    R.jcStop = fieldOffset(JC, "stopJudge");
    static const char* const lists[] = {"clickControls", "dragControls", "holdControls", "flickControls"};
    for (int k = 0; k < 4; k++) {
        R.numLists[k] = fieldOffset(NUM, lists[k]);
        R.ctlNote[k] = fieldOffset(ctl[k], "noteInfor");
    }
    R.dragJudged = fieldOffset(ctl[1], "isJudged");
    R.holdJudged = fieldOffset(ctl[2], "judged");
    R.holdSafe = fieldOffset(ctl[2], "_safeFrame");
    R.jlAbove = fieldOffset(JL, "notesAbove");
    R.jlBelow = fieldOffset(JL, "notesBelow");
    static const char* const idx[] = {"nowSpeedIndex", "nowDisappearIndex", "nowMoveIndex", "nowRotateIndex"};
    for (int i = 0; i < 4; i++) R.jlIdx[i] = fieldOffset(JL, idx[i]);
    R.jlCreate = findMethod(JL, "CreateNote", 2);
    R.noteTime = fieldOffset(Note, "realTime");
    R.noteJudged = fieldOffset(Note, "isJudged");
    R.noteFlick = fieldOffset(Note, "isJudgedForFlick");

    const int must[] = {R.pcNow, R.pcPlaying, R.pcStart, R.pcPrepared, R.pcVolSet, R.pcFirst, R.pcFirstGuide,
                        R.pcJc, R.pcScore, R.pcAas, R.pcAudioTime, R.pcOffset, R.jcNum, R.jcLines, R.jcStop, R.numLists[0],
                        R.numLists[1], R.numLists[2], R.numLists[3], R.ctlNote[0], R.ctlNote[1], R.ctlNote[2],
                        R.ctlNote[3], R.dragJudged, R.holdJudged, R.holdSafe, R.jlAbove, R.jlBelow, R.jlIdx[0],
                        R.jlIdx[1], R.jlIdx[2], R.jlIdx[3], R.noteTime, R.noteJudged, R.noteFlick};
    for (int v : must)
        if (v < 0) return false;
    if (!R.jlCreate || !R.aasSchedule || R.aasStart < 0) return false;
    scoreLayout(Score);
    LOG("resume: score snapshot %d value fields (%d bytes), %d refs", g_vfN, g_vfBytes, g_refN);

    const MethodInfo* upd = findMethod(PC, "Update", 0);
    if (!upd) return false;
    orig_PC_Update = (VoidFn)hookMethod(upd, (void*)hk_PC_Update);
    return orig_PC_Update != nullptr;
}
