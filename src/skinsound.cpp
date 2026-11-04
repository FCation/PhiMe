// A skin's hit sounds (click / drag / flick .ogg or .wav) in place of the game's.
//
// The game plays hit sounds through SEPlayer (static): PlayHitFx(index) -> the E7 NativeAudio pointer
// pointers[index] (low latency), or the Unity AudioClip nativeAudioClips[index]; 7 = tap (and hold heads),
// 8 = drag, 9 = flick. Those three slots of both tables are swapped for the skin's sounds, so every path that ends
// in PlayHitFx plays the skin: the game's own hits and the on-time sounds of hitsound.cpp alike.
//   PCM: audiodec.cpp (stb_vorbis / WAV) -> AudioClip.Create + SetData -> NativeAudio.Load(clip) for the pointer.
// The game's entries are kept and put back when no skin (or a skin without that sound) is in use.
#include "mod.h"
#include "skinpack.h"
#include "audiodec.h"

using namespace il2cpp;
using namespace unity;

static struct {
    bool ok, tried;
    Il2CppClass *SE, *Clip, *Single, *NA;
    FieldInfo *clips, *native;
    const MethodInfo *create, *setData, *naLoad, *hide, *getData, *samples, *channels, *frequency;
    int naLoadArgs;
} A;

static const int kSlot[3] = {7, 8, 9};  // tap, drag, flick
static PcmAudio g_pcm[3];
static char g_packName[128];
static Handle g_ourClip[3], g_ourPtr[3], g_gameClip[3], g_gamePtr[3];
static bool g_haveGame = false, g_applied = false;
static int g_builtSrc[3];     // what each slot's clip of ours was made from (0 = none, 1 = skin, 2 = game)
static float g_builtGain[3];  // and at which volume

static void resolve() {
    if (A.tried) return;
    A.tried = true;
    A.SE = findClass("", "SEPlayer");
    A.Clip = findClass("UnityEngine", "AudioClip");
    A.Single = findClass("System", "Single");
    A.NA = findClass("E7.Native", "NativeAudio");
    if (A.SE) {
        // (audioClips holds Addressables keys: String[]; the clips are nativeAudioClips, their NativeAudio pointers
        // are in pointers: 0.29.2 log)
        A.clips = il2cpp_class_get_field_from_name(A.SE, "nativeAudioClips");
        A.native = il2cpp_class_get_field_from_name(A.SE, "pointers");
    }
    if (A.Clip) {
        A.create = findMethod(A.Clip, "Create", 5);
        A.setData = findMethod(A.Clip, "SetData", 2);
        A.getData = findMethod(A.Clip, "GetData", 2);
        A.samples = findMethod(A.Clip, "get_samples", 0);
        A.channels = findMethod(A.Clip, "get_channels", 0);
        A.frequency = findMethod(A.Clip, "get_frequency", 0);
    }
    if (A.NA) {
        const char* one[1] = {"AudioClip"};
        A.naLoad = findMethodSig(A.NA, "Load", 1, one);
        A.naLoadArgs = 1;
        if (!A.naLoad) {
            const char* two[2] = {"AudioClip", "LoadOptions"};
            A.naLoad = findMethodSig(A.NA, "Load", 2, two);
            A.naLoadArgs = 2;
        }
    }
    Il2CppClass* UObj = findClass("UnityEngine", "Object");
    A.hide = UObj ? findMethod(UObj, "set_hideFlags", 1) : nullptr;
    A.ok = A.SE && A.Clip && A.Single && A.create && A.setData && (A.clips || A.native);
    LOG("skinsound: %s (SEPlayer %s, clips %s, native %s, AudioClip.Create %s, SetData %s, NativeAudio.Load %s/%d)",
        A.ok ? "ready" : "OFF", A.SE ? "yes" : "no", A.clips ? "yes" : "no", A.native ? "yes" : "no",
        A.create ? "yes" : "no", A.setData ? "yes" : "no", A.naLoad ? "yes" : "no", A.naLoadArgs);
}

// called by skin.cpp while a pack's files are still in memory (the sounds are decoded once, kept as PCM)
void skinSoundsLoad(const SkinPack& p) {
    for (PcmAudio& a : g_pcm) audioFree(&a);
    snprintf(g_packName, sizeof g_packName, "%s", p.name);
    static const char* const names[3] = {"click", "drag", "flick"};
    for (int i = 0; i < 3; i++) {
        const SkinBlob& b = p.files[SK_SND_CLICK + i];
        g_ourClip[i].clear();
        g_ourPtr[i].clear();
        g_builtSrc[i] = 0;
        if (!b.data) continue;
        AudioError err;
        char why[160] = "";
        if (audioDecode(b.data, b.size, &g_pcm[i], &err, why, sizeof why))
            LOG("skinsound: %s %u frames, %d ch, %d Hz", names[i], g_pcm[i].frames, g_pcm[i].channels, g_pcm[i].rate);
        else
            LOG("skinsound: %s not decoded: %s %s", names[i], audioErrorName(err), why);
    }
}

// The clip for one slot from a source (1 = the skin's sound, 2 = the game's own clip) at a volume (1 = as it is),
// and its NativeAudio pointer; made again only when the source or the volume changes
static void build(int i, int src, float gain) {
    if (g_builtSrc[i] == src && g_builtGain[i] == gain && g_ourClip[i].h) return;
    g_ourClip[i].clear();
    g_ourPtr[i].clear();
    g_builtSrc[i] = 0;
    static const char* const names[3] = {"PhiMeTap", "PhiMeDrag", "PhiMeFlick"};
    int frames = 0, ch = 0, rate = 0;
    Il2CppArray* f = nullptr;
    bool ok;
    if (src == 1) {
        PcmAudio& a = g_pcm[i];
        if (!a.samples) return;
        frames = (int)a.frames, ch = a.channels, rate = a.rate;
        f = newArray(A.Single, (uintptr_t)frames * ch);
        float* d = (float*)arrayData(f);
        for (int k = 0; k < frames * ch; k++) d[k] = a.samples[k] / 32768.f;
    } else {
        Obj* gc = (Obj*)g_gameClip[i].get();
        if (!alive(gc) || !A.getData || !A.samples || !A.channels || !A.frequency) return;
        frames = callm<int>(A.samples, gc), ch = callm<int>(A.channels, gc), rate = callm<int>(A.frequency, gc);
        if (frames <= 0 || ch <= 0 || rate <= 0 || (long)frames * ch > 20L * 192000 * 2) return;
        f = newArray(A.Single, (uintptr_t)frames * ch);
        int off = 0;
        void* ga[] = {f, &off};
        Obj* r = invokeObj(A.getData, gc, ga, &ok);
        if (!ok || (r && !*(bool*)((char*)r + sizeof(Il2CppObject)))) {
            LOG("skinsound: the game's %s sound could not be read (volume stays as it is)", names[i]);
            return;
        }
    }
    float* d = (float*)arrayData(f);
    for (int k = 0; k < frames * ch; k++) {  // the volume, clipped at full scale
        float v = d[k] * gain;
        d[k] = v > 1.f ? 1.f : v < -1.f ? -1.f : v;
    }
    bool stream = false;
    void* args[] = {str(names[i]), &frames, &ch, &rate, &stream};
    Obj* clip = invokeObj(A.create, nullptr, args, &ok);
    if (!ok || !clip) {
        LOG("skinsound: AudioClip.Create failed for %s", names[i]);
        return;
    }
    int offset = 0;
    void* sargs[] = {f, &offset};
    invokeObj(A.setData, clip, sargs, &ok);
    if (!ok) {
        LOG("skinsound: SetData failed for %s", names[i]);
        return;
    }
    if (A.hide) callm<void>(A.hide, clip, 32);  // DontUnloadUnusedAsset
    g_ourClip[i].set(clip);
    if (A.naLoad) {
        Obj* ptr = nullptr;
        void* la1[] = {clip};
        void* la2[] = {clip, nullptr};
        ptr = invokeObj(A.naLoad, nullptr, A.naLoadArgs == 1 ? la1 : la2, &ok);
        if (ok && ptr) g_ourPtr[i].set(ptr);
        else LOG("skinsound: NativeAudio.Load failed for %s (the Unity clip is still used)", names[i]);
    }
    g_builtSrc[i] = src;
    g_builtGain[i] = gain;
    LOG("skinsound: %s from the %s, volume %d%%", names[i], src == 1 ? "skin" : "game", (int)(gain * 100 + 0.5f));
}
// the SEPlayer that owns instance fields: a static field of its own type (a singleton), else the scene's component
static Obj* sePlayer() {
    for (Il2CppClass* c = A.SE; c; c = nullptr) {
        void* it = nullptr;
        while (FieldInfo* fi = il2cpp_class_get_fields(c, &it)) {
            if (!(il2cpp_field_get_flags(fi) & 0x10) || il2cpp_class_from_type(il2cpp_field_get_type(fi)) != A.SE) continue;
            Obj* o = nullptr;
            il2cpp_field_static_get_value(fi, &o);
            if (o) return o;
        }
    }
    bool unityObject = false;
    for (Il2CppClass* c = A.SE; c && !unityObject; c = il2cpp_class_get_parent(c))
        unityObject = !strcmp(il2cpp_class_get_name(c), "MonoBehaviour");
    return unityObject ? findObjectOfType(A.SE) : nullptr;
}

// SEPlayer's two tables (arrays or lists) as raw element slots. A static field is read as one; an instance field from
// the SEPlayer object (0.29.0 read instance fields as static ones: garbage, and the level never loaded)
static Obj** slots(FieldInfo* f, int* n) {
    *n = 0;
    Obj* t = nullptr;
    if (!f) return nullptr;
    if (il2cpp_field_get_flags(f) & 0x10) {
        il2cpp_field_static_get_value(f, &t);
    } else {
        Obj* se = sePlayer();
        static int logged = 0;
        if (logged++ < 2) LOG("skinsound: %s is an instance field, SEPlayer object %s", il2cpp_field_get_name(f), se ? "found" : "NOT found");
        if (!se) return nullptr;
        t = fld<Obj*>(se, (int)il2cpp_field_get_offset(f));
    }
    if (!t) return nullptr;
    const char* cn = il2cpp_class_get_name(il2cpp_object_get_class(t));
    size_t l = strlen(cn);
    if (l > 2 && cn[l - 2] == '[' && cn[l - 1] == ']') {
        // only the element types we expect (a struct array would not hold object pointers)
        if (strcmp(cn, "AudioClip[]") && strcmp(cn, "NativeAudioPointer[]")) {
            LOG("skinsound: table of type %s left alone", cn);
            return nullptr;
        }
        *n = (int)((Il2CppArray*)t)->max_length;
        return (Obj**)arrayData((Il2CppArray*)t);
    }
    static int items = -1, size = -1;
    if (items < 0) {
        items = fieldOffset(il2cpp_object_get_class(t), "_items");
        size = fieldOffset(il2cpp_object_get_class(t), "_size");
    }
    if (items < 0 || size < 0 || memcmp(cn, "List`1", 7)) return nullptr;
    Il2CppArray* a = fld<Il2CppArray*>(t, items);
    *n = a ? fld<int>(t, size) : 0;
    return a ? (Obj**)arrayData(a) : nullptr;
}

// a bool field of SEPlayer, static or on its object (for the log: which path plays the sounds)
static int seBool(const char* name) {
    FieldInfo* f = A.SE ? il2cpp_class_get_field_from_name(A.SE, name) : nullptr;
    if (!f || il2cpp_type_get_type(il2cpp_field_get_type(f)) != IL2CPP_TYPE_BOOLEAN) return -1;
    bool v = false;
    if (il2cpp_field_get_flags(f) & 0x10) {
        il2cpp_field_static_get_value(f, &v);
    } else {
        Obj* se = sePlayer();
        if (!se) return -1;
        v = fld<bool>(se, (int)il2cpp_field_get_offset(f));
    }
    return v ? 1 : 0;
}

// level start: the skin's sounds in (or the game's back)
void skinSoundsApply(bool on) {
    resolve();
    if (!A.ok) return;
    int nc = 0, nn = 0;
    Obj** cs = slots(A.clips, &nc);
    // NativeAudioPointer as a struct would not be an object slot: leave that table alone then
    static int naValue = -1;
    if (naValue < 0) {
        Il2CppClass* P = findClass("E7.Native", "NativeAudioPointer");
        naValue = P && il2cpp_class_is_valuetype(P) ? 1 : 0;
        if (naValue) LOG("skinsound: NativeAudioPointer is a struct, the native table is left alone");
    }
    Obj** ns = naValue ? nullptr : slots(A.native, &nn);
    if (!g_haveGame) {  // the game's own entries, once
        for (int i = 0; i < 3; i++) {
            if (cs && kSlot[i] < nc) g_gameClip[i].set(cs[kSlot[i]]);
            if (ns && kSlot[i] < nn) g_gamePtr[i].set(ns[kSlot[i]]);
        }
        g_haveGame = (cs && nc > 9) || (ns && nn > 9);
        static int logged = 0;
        if (logged++ < 2)
            LOG("skinsound: SEPlayer tables: clips %d (%s), pointers %d (%s); useCriware %d, nativeAudioReady %d", nc,
                cs && nc > 7 && cs[7] ? il2cpp_class_get_name(il2cpp_object_get_class(cs[7])) : "-", nn,
                ns && nn > 7 && ns[7] ? il2cpp_class_get_name(il2cpp_object_get_class(ns[7])) : "-", seBool("useCriware"),
                seBool("nativeAudioReady"));
        if (!g_haveGame) return;
    }
    int used = 0;
    const int vols[3] = {g_cfg.volTap, g_cfg.volDrag, g_cfg.volFlick};
    for (int i = 0; i < 3; i++) {
        float gain = vols[i] / 100.f;
        int src = on && g_pcm[i].samples ? 1 : gain != 1.f ? 2 : 0;  // the skin's, the game's at another volume, as is
        if (src) build(i, src, gain);
        bool mine = src && g_ourClip[i].h;
        Obj* clip = mine ? (Obj*)g_ourClip[i].get() : (Obj*)g_gameClip[i].get();
        Obj* ptr = mine && g_ourPtr[i].h ? (Obj*)g_ourPtr[i].get() : (Obj*)g_gamePtr[i].get();
        if (cs && kSlot[i] < nc && clip) cs[kSlot[i]] = clip;
        if (ns && kSlot[i] < nn && ptr) ns[kSlot[i]] = ptr;
        if (mine) used++;
    }
    if (used || g_applied)
        LOG("skinsound: %d sound(s) of ours in use%s", used, used ? "" : " (the game's are back)");
    g_applied = used > 0;
}
