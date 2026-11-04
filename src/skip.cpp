// SKIP: a pill at the bottom right of a level.
//   - Intro: when the first note is 3 s or more into the chart, skip to just before the first note comes onto the
//     screen. Where that is comes from the judge lines' speed events: a note sits (note.floorPosition - the line's
//     floor position now) x speed x 0.6 screen heights from its line, so we look for the first moment any early
//     note is within ~1.1 screen heights, and go 0.3 s before it (fallback: 1.5 s before the first note).
//   - Ending: once the last note is done (no note objects left) and the song still has a while to go, jump the
//     music to its end; the game then ends the level its own way.
// Not in practice levels (the section editor owns the clock there), not during the resume replay or a pause.
#include "mod.h"
#include "practice.h"

using namespace il2cpp;
using namespace unity;

static struct {
    int pcNow, pcOffset, pcAas, pcJc, pcPlaying, pcAudioTime;
    int aasTime, aasSrc;
    const MethodInfo *aasSeek, *aasLen;
    int jcSorted, jcLines, jcNum;
    int jlAbove, jlBelow, jlSpeed;
    int numLists[4];
    int noteTime, noteFloor, noteSpeed, noteType, noteRaw;
    Il2CppClass* SpeedEv;
    int evStart, evEnd, evFloor, evValue, evStride, evAdj;
    bool evValueType;
    int listItems = -1, listSize = -1;
    bool ok;
} K;

static Handle g_pc, g_root, g_btn;
static bool g_built = false, g_introDone = false, g_endDone = false, g_down = false;
static int g_introState = 0;  // 0 = not computed yet, 1 = there is an intro to skip, 2 = none
static float g_introTarget = 0, g_alpha = 0, g_endCheck = 0;
static bool g_endReady = false;
static float g_syncT = 0;  // seconds left to watch the music after a jump
static int g_syncFixes = 0;
static int g_mode = 0;  // what a tap does now: 1 = intro, 2 = ending
static Vec2 g_pos, g_size{196, 64};

static int listView(Obj* list, Obj*** out) {
    if (!list) return 0;
    if (K.listItems < 0) {
        Il2CppClass* k = il2cpp_object_get_class(list);
        K.listItems = fieldOffset(k, "_items");
        K.listSize = fieldOffset(k, "_size");
        if (K.listItems < 0 || K.listSize < 0) return 0;
    }
    Il2CppArray* items = fld<Il2CppArray*>(list, K.listItems);
    int n = fld<int>(list, K.listSize);
    if (!items || n <= 0 || (uintptr_t)n > items->max_length) return 0;
    *out = (Obj**)arrayData(items);
    return n;
}
// a List<T>'s backing array and count, whatever T is
static char* listRaw(Obj* list, int* n) {
    *n = 0;
    if (!list) return nullptr;
    const char* cn = il2cpp_class_get_name(il2cpp_object_get_class(list));
    size_t cl = strlen(cn);
    if (cl > 2 && cn[cl - 2] == '[' && cn[cl - 1] == ']') {  // a plain array (SpeedEvent[])
        Il2CppArray* a = (Il2CppArray*)list;
        *n = (int)a->max_length;
        return *n > 0 ? (char*)arrayData(a) : nullptr;
    }
    if (K.listItems < 0) return nullptr;
    Il2CppArray* items = fld<Il2CppArray*>(list, K.listItems);
    int c = fld<int>(list, K.listSize);
    if (!items || c <= 0 || (uintptr_t)c > items->max_length) return nullptr;
    *n = c;
    return (char*)arrayData(items);
}

// ---------------- where the intro ends ----------------
struct Ev {
    float s, e, f, v;
};
struct Line {
    Ev* ev;
    int n;
    float k;  // event time units -> seconds
};
static float floorAt(const Line& L, float t) {
    float u = t / L.k;
    int lo = 0, hi = L.n;  // last event starting at or before u
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (L.ev[mid].s <= u) lo = mid + 1;
        else hi = mid;
    }
    const Ev& e = L.ev[lo > 0 ? lo - 1 : 0];
    return e.f + e.v * (u - e.s) * L.k;
}
static bool readLine(Obj* jl, Line* L) {
    L->ev = nullptr;
    L->n = 0;
    L->k = 1;
    int n = 0;
    Obj* evs = fld<Obj*>(jl, K.jlSpeed);
    char* base = listRaw(evs, &n);
    if (!base || n <= 0) {
        static int logged = 0;
        if (logged++ < 2)
            LOG("skip: speed events of a line: %s, %d", evs ? il2cpp_class_get_name(il2cpp_object_get_class(evs)) : "null", n);
        return false;
    }
    L->ev = (Ev*)malloc(sizeof(Ev) * n);
    if (!L->ev) return false;
    for (int i = 0; i < n; i++) {
        char* p;
        if (K.evValueType) {
            p = base + (size_t)i * K.evStride - K.evAdj;  // field offsets count the object header
        } else {
            p = ((char**)base)[i];
            if (!p) return false;
        }
        L->ev[i] = Ev{fld<float>(p, K.evStart), fld<float>(p, K.evEnd), fld<float>(p, K.evFloor), fld<float>(p, K.evValue)};
    }
    L->n = n;
    return true;
}
// events in seconds (k = 1) or in the chart's beat units (k = a note's realTime / time): whichever fits the notes
static float fitK(Line& L, Obj* jl, float* errOut) {
    float ks[2] = {1, 0};
    Obj** notes = nullptr;
    int nn = listView(fld<Obj*>(jl, K.jlAbove), &notes);
    if (nn <= 0) nn = listView(fld<Obj*>(jl, K.jlBelow), &notes);
    if (nn <= 0) {
        *errOut = 0;
        return 1;
    }
    if (K.noteRaw >= 0)
        for (int i = 0; i < nn && !ks[1]; i++) {
            float raw = notes[i] ? fld<float>(notes[i], K.noteRaw) : 0, rt = notes[i] ? fld<float>(notes[i], K.noteTime) : 0;
            if (raw > 1e-3f && rt > 1e-3f) ks[1] = rt / raw;
        }
    float best = 1, bestErr = 1e30f;
    for (float k : ks) {
        if (k <= 0) continue;
        L.k = k;
        float err = 0;
        int m = 0;
        for (int i = 0; i < nn && m < 6; i++) {
            if (!notes[i]) continue;
            float d = floorAt(L, fld<float>(notes[i], K.noteTime)) - fld<float>(notes[i], K.noteFloor);
            err += d < 0 ? -d : d;
            m++;
        }
        if (m) err /= m;
        if (err < bestErr) bestErr = err, best = k;
    }
    L.k = best;
    *errOut = bestErr;
    return best;
}

static void computeIntro(Obj* pc) {
    Obj* jc = fld<Obj*>(pc, K.pcJc);
    Obj** sorted = nullptr;
    int ns = alive(jc) ? listView(fld<Obj*>(jc, K.jcSorted), &sorted) : 0;
    if (ns <= 0) return;  // chart not loaded yet: try again next frame
    float first = 1e30f;
    for (int i = 0; i < ns; i++)
        if (sorted[i]) {
            first = fld<float>(sorted[i], K.noteTime);
            break;
        }
    g_introState = 2;
    if (first < 3.f || first > 1e29f) {
        LOG("skip: first note at %.2f s, no intro skip", first < 1e29f ? first : -1);
        return;
    }
    float target = first - 1.5f, enter = -1;
    const char* how = "fallback";
    Obj** lines = nullptr;
    int nl = listView(fld<Obj*>(jc, K.jcLines), &lines);
    if (nl > 0 && nl <= 512 && K.jlSpeed >= 0 && K.evStart >= 0 && K.noteFloor >= 0 && K.jlAbove >= 0 && K.jlBelow >= 0) {
        Line* L = (Line*)calloc(nl, sizeof(Line));
        bool good = L != nullptr;
        float worst = 0;
        for (int l = 0; good && l < nl; l++) {
            if (!alive(lines[l]) || !readLine(lines[l], &L[l])) {
                good = false;
                break;
            }
            float err;
            fitK(L[l], lines[l], &err);
            if (err > worst) worst = err;
        }
        if (good && worst < 0.05f) {
            // from the start, the first moment a note of the first few seconds is on screen
            for (float t = 0; t < first && enter < 0; t += 0.02f)
                for (int l = 0; l < nl && enter < 0; l++)
                    for (int side = 0; side < 2 && enter < 0; side++) {
                        Obj** nn = nullptr;
                        int c = listView(fld<Obj*>(lines[l], side ? K.jlBelow : K.jlAbove), &nn);
                        for (int i = 0; i < c; i++) {
                            Obj* n = nn[i];
                            if (!n) continue;
                            float rt = fld<float>(n, K.noteTime);
                            if (rt > first + 3.f || rt < t) continue;
                            bool hold = K.noteType >= 0 && fld<int>(n, K.noteType) == 3;  // a hold's speed is its length
                            float sp = K.noteSpeed >= 0 && !hold ? fld<float>(n, K.noteSpeed) : 1;
                            float d = (fld<float>(n, K.noteFloor) - floorAt(L[l], t)) * sp * 0.6f;
                            if (d >= -0.05f && d <= 1.1f) {
                                enter = t;
                                break;
                            }
                        }
                    }
            if (enter >= 0) {
                target = enter - 0.3f;
                how = "speed events";
            }
        }
        if (enter < 0 && K.noteFloor >= 0 && K.jlAbove >= 0 && K.jlBelow >= 0) {
            // no usable speed events (some charts keep them elsewhere): per line, the scroll speed from its two
            // earliest notes, assumed steady through the intro
            for (int l = 0; l < nl; l++) {
                if (!alive(lines[l])) continue;
                Obj *a = nullptr, *b = nullptr;
                for (int side = 0; side < 2; side++) {
                    Obj** nn = nullptr;
                    int c = listView(fld<Obj*>(lines[l], side ? K.jlBelow : K.jlAbove), &nn);
                    for (int i = 0; i < c; i++) {
                        Obj* n = nn[i];
                        if (!n) continue;
                        float rt = fld<float>(n, K.noteTime);
                        if (!a || rt < fld<float>(a, K.noteTime)) b = a, a = n;
                        else if ((!b || rt < fld<float>(b, K.noteTime)) && rt > fld<float>(a, K.noteTime) + 0.05f) b = n;
                    }
                }
                if (!a || !b) continue;
                float t1 = fld<float>(a, K.noteTime), t2 = fld<float>(b, K.noteTime);
                if (t1 > first + 3.f || t2 <= t1) continue;
                float v = (fld<float>(b, K.noteFloor) - fld<float>(a, K.noteFloor)) / (t2 - t1);
                bool hold = K.noteType >= 0 && fld<int>(a, K.noteType) == 3;
                float sp = K.noteSpeed >= 0 && !hold ? fld<float>(a, K.noteSpeed) : 1;
                if (v <= 0.01f || sp <= 0.01f) continue;
                float e = t1 - 1.1f / (0.6f * v * sp);
                if (e < 0) e = 0;
                if (enter < 0 || e < enter) enter = e;
            }
            if (enter >= 0) {
                target = enter - 0.3f;
                how = "note spacing";
            }
        }
        LOG("skip: %d judge lines, event fit error %.4f (k of line 0: %.4f)%s", nl, worst, good && nl ? L[0].k : 0,
            good ? "" : ", events unreadable");
        if (L) {
            for (int l = 0; l < nl; l++) free(L[l].ev);
            free(L);
        }
    }
    if (target < 1.f) {
        LOG("skip: first note %.2f s, on screen from %.2f s: nothing worth skipping", first, enter);
        return;
    }
    g_introTarget = target;
    g_introState = 1;
    LOG("skip: intro skip to %.2f s (first note %.2f s, on screen from %.2f s, %s)", target, first, enter, how);
}

// ---------------- jumping ----------------
static float songLength(Obj* pc) {
    Obj* aas = fld<Obj*>(pc, K.pcAas);
    float len = 0;
    if (!alive(aas) || !K.aasLen || !invokeFloat(K.aasLen, aas, &len)) return -1;
    return len - fld<float>(pc, K.pcOffset);
}
static void jumpTo(Obj* pc, float t) {
    Obj* aas = fld<Obj*>(pc, K.pcAas);
    if (!alive(aas)) return;
    float clip = t + fld<float>(pc, K.pcOffset);
    if (clip < 0) clip = 0;
    void* args[] = {&clip};
    invokeSafe(K.aasSeek, aas, args);
    fld<float>(aas, K.aasTime) = clip;
    Obj* src = fld<Obj*>(aas, K.aasSrc);
    if (alive(src)) setAudioTime(src, clip);
    if (K.pcAudioTime >= 0) fld<float>(pc, K.pcAudioTime) = clip;
    fld<float>(pc, K.pcNow) = t;
    g_syncT = 1.5f;
    g_syncFixes = 0;
}

// A streamed song does not always land exactly where it was sent: for a moment after a jump, the game's clock (the
// wrapper's time) is put back on the music's real position whenever they drift apart.
static void resync(Obj* pc) {
    if (g_syncT <= 0) return;
    g_syncT -= unscaledDt();
    Obj* aas = fld<Obj*>(pc, K.pcAas);
    Obj* src = alive(aas) ? fld<Obj*>(aas, K.aasSrc) : nullptr;
    if (!alive(src) || !audioPlaying(src)) return;
    float real = audioTime(src), game = fld<float>(aas, K.aasTime), d = real - game;
    if ((d > 0.04f || d < -0.04f) && g_syncFixes < 5) {
        g_syncFixes++;
        void* args[] = {&real};
        invokeSafe(K.aasSeek, aas, args);
        fld<float>(aas, K.aasTime) = real;
        LOG("skip: music at %.3f, game clock at %.3f: clock moved to the music (%d)", real, game, g_syncFixes);
    }
}

// ---------------- the button ----------------
static void build() {
    Obj* root = overlayCanvas("PhiMeSkip", 30400);
    g_root.set(root);
    Vec2 s = screenSize();
    float H = canvasH(), W = s.y > 0 ? H * s.x / s.y : H * 16 / 9;
    float mx = 44, my = 40;
    g_pos = Vec2{W / 2 - mx - g_size.x / 2, -H / 2 + my + g_size.y / 2};
    Obj* border = image(root, "skip", SPR_ROUND, rgb(0xE8E6DC, 0.95f), Vec2{1, 0}, Vec2{-mx - g_size.x / 2, my + g_size.y / 2},
                        Vec2{g_size.x + 4, g_size.y + 4}, 32);
    Obj* fill = image(rt(border), "fill", SPR_ROUND, rgb(0xFAF9F5, 0.95f), Vec2{0.5f, 0.5f}, Vec2{0, 0}, g_size, 30);
    image(rt(fill), "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0, 0.5f}, Vec2{38, 1}, Vec2{40, 23});
    text(rt(fill), "SKIP  ›", 26, rgb(0xD97757), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{18, 1},
         Vec2{140, 44});
    Obj* cg = addCanvasGroup(gameObject(border));
    setGroupAlpha(cg, 0);
    setActive(gameObject(border), false);
    g_btn.set(cg);
    g_alpha = 0;
    g_built = true;
}

static void showButton(bool on) {
    Obj* cg = (Obj*)g_btn.get();
    if (!alive(cg)) return;
    float target = on ? 1.f : 0.f;
    if (g_alpha != target) {
        float step = unscaledDt() * 5;
        g_alpha = g_alpha < target ? (g_alpha + step > target ? target : g_alpha + step)
                                   : (g_alpha - step < target ? target : g_alpha - step);
        setGroupAlpha(cg, g_alpha);
        setActive(gameObject(cg), g_alpha > 0.001f);
    }
}

// a tap on the pill (touches are read directly: the level has no EventSystem)
static bool tapped() {
    Obj* cg = (Obj*)g_btn.get();
    if (!alive(cg) || g_alpha < 0.5f) {
        g_down = false;
        return false;
    }
    TouchInfo t[4];
    int n = touches(t, 4);
    for (int i = 0; i < n; i++) {
        Vec2 p = screenToCanvas(t[i].pos);
        float dx = p.x - g_pos.x, dy = p.y - g_pos.y;
        bool inside = dx > -g_size.x / 2 - 16 && dx < g_size.x / 2 + 16 && dy > -g_size.y / 2 - 16 && dy < g_size.y / 2 + 16;
        if (t[i].phase == 0 && inside) {
            g_down = true;
            setScale(goTransform(gameObject(cg)), 0.94f);
        } else if ((t[i].phase == 3 || t[i].phase == 4) && g_down) {
            g_down = false;
            setScale(goTransform(gameObject(cg)), 1);
            if (inside && t[i].phase == 3) return true;
        }
    }
    return false;
}

// all notes done: the last one's time has passed and no note object is left on the field
static bool notesDone(Obj* pc, float now) {
    Obj* jc = fld<Obj*>(pc, K.pcJc);
    if (!alive(jc)) return false;
    Obj** sorted = nullptr;
    int ns = listView(fld<Obj*>(jc, K.jcSorted), &sorted);
    if (ns <= 0 || !sorted[ns - 1] || now < fld<float>(sorted[ns - 1], K.noteTime) + 0.3f) return false;
    Obj* num = fld<Obj*>(jc, K.jcNum);
    if (!alive(num)) return false;
    for (int off : K.numLists) {
        Obj** e = nullptr;
        int n = listView(fld<Obj*>(num, off), &e);
        for (int i = 0; i < n; i++)
            if (alive(e[i])) return false;
    }
    return true;
}

static void skipFrame(void*) {
    Obj* pc = (Obj*)g_pc.get();
    if (!pc) return;
    if (!alive(pc)) {
        g_pc.clear();
        return;
    }
    if (g_prac.st != PR_OFF) return;
    resync(pc);
    if (!g_built) build();
    float now = fld<float>(pc, K.pcNow);
    if (g_introState == 0) computeIntro(pc);
    Obj* aas = fld<Obj*>(pc, K.pcAas);
    Obj* src = alive(aas) ? fld<Obj*>(aas, K.aasSrc) : nullptr;
    bool running = fld<bool>(pc, K.pcPlaying) && audioPlaying(src) && !resumeActive();

    int mode = 0;
    if (running && g_introState == 1 && !g_introDone && now < g_introTarget - 0.25f) mode = 1;
    if (running && !g_endDone && g_introState != 0) {
        g_endCheck -= unscaledDt();
        if (g_endCheck <= 0) {
            g_endCheck = 0.25f;
            float len = songLength(pc);
            g_endReady = len > 0 && len - now > 1.5f && notesDone(pc, now);
        }
        if (g_endReady) mode = 2;
    }
    if (mode) g_mode = mode;
    showButton(mode != 0);
    if (!mode || !tapped()) return;
    if (g_mode == 1) {
        g_introDone = true;
        LOG("skip: intro skipped %.2f -> %.2f s", now, g_introTarget);
        jumpTo(pc, g_introTarget);
    } else if (g_mode == 2) {
        g_endDone = true;
        float len = songLength(pc);
        LOG("skip: ending skipped at %.2f s (song %.2f s)", now, len);
        if (len > 0) jumpTo(pc, len - 0.02f);
    }
    g_alpha = 0.99f;  // fade out from here
}

void skipLevelStart(Obj* pc) {
    if (!K.ok) return;
    g_pc.set(pc);
    g_root.clear();
    g_btn.clear();
    g_built = g_introDone = g_endDone = g_endReady = g_down = false;
    g_introState = 0;
    g_endCheck = 0;
    g_alpha = 0;
    g_mode = 0;
    g_syncT = 0;
    static bool registered = false;
    if (!registered) {
        registered = true;
        onEveryFrame(skipFrame);
    }
}

bool skipInstall() {
    Il2CppClass* PC = findClass("", "ProgressControl");
    Il2CppClass* AAS = findClass("", "AddressableAudioSource");
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* JL = findClass("", "JudgeLineControl");
    Il2CppClass* NUM = findClass("", "NoteUpdateManager");
    Il2CppClass* Note = findClass("", "ChartNote");
    if (!PC || !AAS || !JC || !JL || !NUM || !Note) return false;
    K.pcNow = fieldOffset(PC, "nowTime");
    K.pcOffset = fieldOffset(PC, "_offset");
    K.pcAas = fieldOffset(PC, "audioSource");
    K.pcJc = fieldOffset(PC, "judgeControl");
    K.pcPlaying = fieldOffset(PC, "isPlaying");
    K.pcAudioTime = fieldOffset(PC, "audioTime");
    K.aasTime = fieldOffset(AAS, "time");
    K.aasSrc = fieldOffset(AAS, "audioSource");
    K.aasSeek = findMethod(AAS, "Seek", 1);
    K.aasLen = findMethod(AAS, "get_ClipLength", 0);
    K.jcSorted = fieldOffset(JC, "chartNoteSortByTime");
    K.jcLines = fieldOffset(JC, "judgeLineControls");
    K.jcNum = fieldOffset(JC, "noteUpdateManager");
    K.jlAbove = fieldOffset(JL, "notesAbove");
    K.jlBelow = fieldOffset(JL, "notesBelow");
    K.jlSpeed = fieldOffset(JL, "speedEvents");
    const char* lists[4] = {"clickControls", "dragControls", "holdControls", "flickControls"};
    for (int i = 0; i < 4; i++) K.numLists[i] = fieldOffset(NUM, lists[i]);
    K.noteTime = fieldOffset(Note, "realTime");
    K.noteFloor = fieldOffset(Note, "floorPosition");
    K.noteSpeed = fieldOffset(Note, "speed");
    K.noteType = fieldOffset(Note, "type");
    K.noteRaw = fieldOffset(Note, "time");
    K.SpeedEv = findClass("", "SpeedEvent");
    K.evStart = K.evEnd = K.evFloor = K.evValue = -1;
    if (K.SpeedEv) {
        K.evStart = fieldOffset(K.SpeedEv, "startTime");
        K.evEnd = fieldOffset(K.SpeedEv, "endTime");
        K.evFloor = fieldOffset(K.SpeedEv, "floorPosition");
        K.evValue = fieldOffset(K.SpeedEv, "value");
        K.evValueType = il2cpp_class_is_valuetype(K.SpeedEv);
        K.evAdj = K.evValueType ? (int)sizeof(Il2CppObject) : 0;
        K.evStride = K.evValueType ? il2cpp_class_instance_size(K.SpeedEv) - (int)sizeof(Il2CppObject) : 8;
        if (K.evStart < 0 || K.evEnd < 0 || K.evFloor < 0 || K.evValue < 0) K.evStart = -1;
    }
    bool core = K.pcNow >= 0 && K.pcOffset >= 0 && K.pcAas >= 0 && K.pcJc >= 0 && K.pcPlaying >= 0 && K.aasTime >= 0 &&
                K.aasSrc >= 0 && K.aasSeek && K.aasLen && K.jcSorted >= 0 && K.jcNum >= 0 && K.noteTime >= 0;
    for (int off : K.numLists) core = core && off >= 0;
    K.ok = core;
    LOG("skip: %s; intro from speed events %s (SpeedEvent %s, note speed %s, note time %s)", core ? "ready" : "off (fields missing)",
        K.evStart >= 0 && K.jlSpeed >= 0 && K.noteFloor >= 0 && K.jcLines >= 0 ? "on" : "off (fallback 1.5 s)",
        K.SpeedEv ? (K.evValueType ? "struct" : "class") : "missing", K.noteSpeed >= 0 ? "yes" : "no", K.noteRaw >= 0 ? "yes" : "no");
    return core;
}
