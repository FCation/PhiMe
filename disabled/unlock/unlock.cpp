// "解锁全部歌曲" (iOS and Android). The all-unlocked empty save turns it on: main chapters, the later songs of a
// chapter and IN / AT charts open by score in the game (GameInformation.CheckSongUnlock: chapter flag song or
// previous song >= 880000, previous difficulty >= 920000, IN also at RKS >= 11), which an empty save can't meet.
// Phigros has no real-money purchases: songs open through story progress or in-game Data. This opens them
// for display/play at runtime only. Nothing is written to the save (scores still save as usual): turning the
// switch off and re-entering the screen restores the normal state.
//
// How the song list is built (4.0.x): SongSelector.Start calls GameInformation.CheckSongUnlock(id, out
// unlockType, out bool[] chartUnlocked, out appeared, out tip) per song, skips songs that have not
// "appeared" yet, and passes the rest to new SongSelectorItem(...):
//   Unlocked            = unlockType == -1   (1 = buy with Data, others = story / special events)
//   PlayableLevelCount  = 4, or 3 when chartUnlocked[3] (AT) is false, or 2 when chartUnlocked[2] (IN) is false
//   SelectorTargetWidth = 100 / 115 / 150 to match
// Those calls are direct branches in compiled code, which a MethodInfo hook cannot catch, so we fix the
// built items after Start instead:
//   - Songs:    SongSelectorItem.Unlock() (Unlocked + unlockType -1 + lock visuals; the persistent unlock is
//               GameInformation.GetKey, which we never call), clear InfoHidden ("???" songs)
//   - IN / AT:  PlayableLevelCount / SelectorTargetWidth raised to the charts the song really has
//   - Chapters: after ChapterSelector.Start, show chapters the way unlocked ones look (lock mask hidden,
//               banner and info shown). Opening a chapter does not re-check the lock. Before the main story is
//               finished (SaveManagement string "GameCompleted" empty and bool "chapter8Passed" false) the
//               chapter scroller is sized one chapter short (childCount - 1), so the last one can't be
//               reached; we size it for every chapter.
// Songs that have not "appeared" are never built into the list, so those stay hidden for now.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_SS_Start, orig_CS_Start;

static struct {
    const MethodInfo *ssItems, *ssSetText, *itemUnlocked, *itemUnlock;
    int csInfos, ciLocked, ciBanner, ciMoreInfo, csPanel;
    int itSong, itLevelCount, itTargetWidth, itInfoHidden, itUnlockType, songLevels, songDiff, songId;
    int listItems = -1, listSize = -1;
} K;

// chapter scroller layout from ChapterSelector.Start: width = chapters * 295 + 790
static const int kChapterStep = 295, kChapterPad = 790;
// difficulty selector width for 2 / 3 / 4 playable charts (SongSelectorItem ctor constants)
static int targetWidth(int levels) { return levels >= 4 ? 100 : levels == 3 ? 115 : 150; }

static bool isClass(Obj* o, const char* name) {
    return o && strcmp(il2cpp_class_get_name(il2cpp_object_get_class(o)), name) == 0;
}

// Charts the song really has among EZ / HD / IN / AT (Legacy is separate): a level name (string[] levels)
// and, when the chart constants are a float[], a constant above 0. Never more than what exists, so a song
// without an AT chart never gets an empty AT slot.
static int chartCount(Obj* song) {
    Il2CppArray* lv = K.songLevels >= 0 ? fld<Il2CppArray*>(song, K.songLevels) : nullptr;
    if (!isClass((Obj*)lv, "String[]")) return 0;
    Il2CppArray* df = K.songDiff >= 0 ? fld<Il2CppArray*>(song, K.songDiff) : nullptr;
    if (!isClass((Obj*)df, "Single[]")) df = nullptr;
    Il2CppString** names = (Il2CppString**)arrayData(lv);
    int n = 0;
    for (uintptr_t i = 0; i < lv->max_length && i < 4; i++) {
        Il2CppString* s = names[i];
        if (!s || *(int*)((char*)s + 0x10) <= 0) break;  // String.length
        if (df && (i >= df->max_length || ((float*)arrayData(df))[i] <= 0)) break;
        n = (int)i + 1;
    }
    return n;
}

// Elements of either T[] or List<T>
static int elements(Obj* coll, Obj*** out) {
    if (!coll) return 0;
    Il2CppClass* k = il2cpp_object_get_class(coll);
    const char* n = il2cpp_class_get_name(k);
    int len = (int)strlen(n);
    if (len > 2 && n[len - 2] == '[' && n[len - 1] == ']') {
        Il2CppArray* a = (Il2CppArray*)coll;
        *out = (Obj**)arrayData(a);
        return (int)a->max_length;
    }
    if (K.listItems < 0) {
        K.listItems = fieldOffset(k, "_items");
        K.listSize = fieldOffset(k, "_size");
        if (K.listItems < 0 || K.listSize < 0) return 0;
    }
    Il2CppArray* items = fld<Il2CppArray*>(coll, K.listItems);
    int size = fld<int>(coll, K.listSize);
    if (!items || size <= 0 || (uintptr_t)size > items->max_length) return 0;
    *out = (Obj**)arrayData(items);
    return size;
}

static void hk_SS_Start(Obj* self, const MethodInfo* m) {
    orig_SS_Start(self, m);
    configLoad();
    if (!g_cfg.unlockAll) return;
    Obj* list = callm<Obj*>(K.ssItems, self);
    Obj** items = nullptr;
    int n = elements(list, &items), opened = 0, revealed = 0, charts = 0;
    for (int i = 0; i < n; i++) {
        Obj* it = items[i];
        if (!it) continue;
        int type = K.itUnlockType >= 0 ? fld<int>(it, K.itUnlockType) : 0;
        if (!callm<bool>(K.itemUnlocked, it)) {
            invokeSafe(K.itemUnlock, it);  // sets Unlocked + unlockType -1 first, then updates its visuals
            opened++;
            if (type != 1) {  // not a plain Data purchase: log it, story / event songs need checking
                char id[96] = "?";
                Obj* song = K.itSong >= 0 ? fld<Obj*>(it, K.itSong) : nullptr;
                if (song && K.songId >= 0) utf8(fld<Il2CppString*>(song, K.songId), id, sizeof id);
                LOG("unlock: %s (unlock type %d) opened", id, type);
            }
        }
        if (K.itInfoHidden >= 0 && fld<bool>(it, K.itInfoHidden)) {  // "???" song: show its info
            fld<bool>(it, K.itInfoHidden) = false;
            revealed++;
        }
        // IN / AT: as many charts as the song has
        Obj* song = K.itSong >= 0 ? fld<Obj*>(it, K.itSong) : nullptr;
        int want = song ? chartCount(song) : 0;
        if (K.itLevelCount >= 0 && want >= 2 && fld<int>(it, K.itLevelCount) < want) {
            fld<int>(it, K.itLevelCount) = want;
            if (K.itTargetWidth >= 0) fld<int>(it, K.itTargetWidth) = targetWidth(want);
            charts++;
        }
    }
    if (opened || revealed || charts) invokeSafe(K.ssSetText, self);
    LOG("unlock: %d of %d songs opened, %d revealed, %d got their IN / AT back", opened, n, revealed, charts);
}

static void hk_CS_Start(Obj* self, const MethodInfo* m) {
    orig_CS_Start(self, m);
    configLoad();
    if (!g_cfg.unlockAll) return;
    Obj** infos = nullptr;
    int n = elements(fld<Obj*>(self, K.csInfos), &infos), opened = 0;
    for (int i = 0; i < n; i++) {
        Obj* ci = infos[i];
        if (!alive(ci)) continue;
        Obj* locked = fld<Obj*>(ci, K.ciLocked);
        if (!alive(locked) || !activeSelf(locked)) continue;
        setActive(locked, false);
        Obj* banner = fld<Obj*>(ci, K.ciBanner);
        if (alive(banner)) setActive(gameObject(banner), true);
        Obj* more = fld<Obj*>(ci, K.ciMoreInfo);
        if (alive(more)) setActive(more, true);
        opened++;
    }
    // the chapter kept for after the main story: size the scroller for every chapter so it can be reached.
    // Inactive chapter cards are only logged: they are not in _chapterInfos (OpenChapter indexes into it).
    int hidden = 0;
    Obj* panel = K.csPanel >= 0 ? fld<Obj*>(self, K.csPanel) : nullptr;
    if (alive(panel)) {
        int count = childCount(panel);
        for (int i = 0; i < count; i++) {
            Obj* go = gameObject(childAt(panel, i));
            if (alive(go) && !activeSelf(go)) {
                char nm[64] = "?";
                objName(go, nm, sizeof nm);
                LOG("unlock: chapter card %d (%s) is inactive, left as is", i, nm);
                hidden++;
            }
        }
        RectInfo ri;
        float want = (float)(count * kChapterStep + kChapterPad);
        if (rectInfo(panel, &ri) && ri.size.x < want - 1) {
            setSize(panel, Vec2{want, ri.size.y});
            LOG("unlock: chapter scroller %.0f -> %.0f (%d chapters)", ri.size.x, want, count);
        }
    }
    LOG("unlock: %d of %d chapters opened (%d inactive cards)", opened, n, hidden);
}

bool unlockInstall() {
    Il2CppClass* SS = findClass("", "SongSelector");
    Il2CppClass* SSI = findClass("", "SongSelectorItem");
    Il2CppClass* CS = findClass("", "ChapterSelector");
    Il2CppClass* CI = findClass("", "ChapterInfo");
    if (!SS || !SSI || !CS || !CI) return false;
    K.ssItems = findMethod(SS, "get_SongItems", 0);
    K.ssSetText = findMethod(SS, "SetText", 0);
    K.itemUnlocked = findMethod(SSI, "get_Unlocked", 0);
    K.itemUnlock = findMethod(SSI, "Unlock", 0);
    K.csInfos = fieldOffset(CS, "_chapterInfos");
    K.ciLocked = fieldOffset(CI, "locked");
    K.ciBanner = fieldOffset(CI, "banner");
    K.ciMoreInfo = fieldOffset(CI, "moreInfo");
    K.csPanel = fieldOffset(CS, "chapterPanel");
    K.itSong = fieldOffset(SSI, "songInfo");
    K.itLevelCount = fieldOffset(SSI, "<PlayableLevelCount>k__BackingField");
    K.itTargetWidth = fieldOffset(SSI, "<SelectorTargetWidth>k__BackingField");
    K.itInfoHidden = fieldOffset(SSI, "<InfoHidden>k__BackingField");
    K.itUnlockType = fieldOffset(SSI, "unlockType");
    if (Il2CppClass* Song = findClass("", "SongsItem")) {
        K.songLevels = fieldOffset(Song, "levels");
        K.songDiff = fieldOffset(Song, "difficulty");
        K.songId = fieldOffset(Song, "songsId");
    } else {
        K.songLevels = K.songDiff = K.songId = -1;
    }
    const MethodInfo* ssStart = findMethod(SS, "Start", 0);
    const MethodInfo* csStart = findMethod(CS, "Start", 0);
    bool ok = true;
    if (K.ssItems && K.itemUnlocked && K.itemUnlock && ssStart)
        orig_SS_Start = (VoidFn)hookMethod(ssStart, (void*)hk_SS_Start);
    else
        ok = false;
    if (K.csInfos >= 0 && K.ciLocked >= 0 && csStart)
        orig_CS_Start = (VoidFn)hookMethod(csStart, (void*)hk_CS_Start);
    else
        ok = false;
    return ok;
}
