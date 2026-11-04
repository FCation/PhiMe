#include "unity.h"

using namespace il2cpp;

namespace unity {

static struct {
    Il2CppClass *GameObject, *RectTransform, *Texture2D, *Sprite, *Color32, *Image, *Text, *Button, *Canvas,
        *CanvasScaler, *GraphicRaycaster, *Shadow, *Font;
    const MethodInfo *goCtor, *goAddComponent, *goSetActive, *compGameObject, *compTransform, *setParent, *opImplicit, *destroy,
        *anchorMin, *anchorMax, *pivot, *anchoredPos, *sizeDelta, *localScale, *texCtor, *setPixels32, *texApply,
        *texWrap, *texFilter, *spriteCreate, *imgSprite, *imgType, *imgPPUMul, *gColor, *gRaycast, *txtFont, *txtGetFont,
        *txtText, *txtGetText, *txtGetSize, *txtGetAlign, *txtPrefH, *txtPrefW, *txtSize, *txtAlign, *txtStyle, *txtHOverflow, *txtVOverflow, *btnOnClick, *addListener,
        *targetGraphic, *cvMode, *cvOrder, *csMode, *csRef, *csMatch, *shColor, *shDist, *findObjOfType,
        *builtinRes, *unscaledDt, *prefGet, *prefSet, *prefSave, *prefGetStr, *loadScene, *activeScene, *goActiveSelf,
        *goTransform, *localEuler, *getRect, *animTrigger, *addWillRender, *getAnchorMin, *getAnchorMax,
        *getPivot, *getAnchoredPos, *getSizeDelta, *trParent, *bhEnabled, *touchCount, *getTouch, *scrW, *scrH, *scrDpi,
        *asGetTime, *asSetTime, *asPitch, *asPlaying, *getComp, *compsInChildren, *instantiate, *objName,
        *btnSetOnClick, *clickEvtCtor, *esCurrent, *trChildCount, *trGetChild, *gGetColor, *getLocalPos,
        *setLocalPosM, *frameCount, *worldCorners, *getPos, *setPosM, *levelTime, *cgAlpha, *cgIgnore, *cgBlocks, *goActiveHier,
        *cvRoot, *cvGetMode, *cvCam, *camMain, *camW2S, *cvGetOrder, *cgGetAlpha, *cgGetIgnore;
    Il2CppClass *WillRender, *Graphic, *Transform, *ClickEvt, *EventSystem, *InputModule, *Animator, *Vector3,
        *CanvasGroup;
    int tId, tPos, tPhase;  // UnityEngine.Touch field offsets (unboxed)
} U;

static Handle g_font;
static Handle g_sprites[SPR_COUNT];

bool init() {
    U.GameObject = findClass("UnityEngine", "GameObject");
    U.RectTransform = findClass("UnityEngine", "RectTransform");
    U.Texture2D = findClass("UnityEngine", "Texture2D");
    U.Sprite = findClass("UnityEngine", "Sprite");
    U.Color32 = findClass("UnityEngine", "Color32");
    U.Image = findClass("UnityEngine.UI", "Image");
    U.Text = findClass("UnityEngine.UI", "Text");
    U.Button = findClass("UnityEngine.UI", "Button");
    U.Canvas = findClass("UnityEngine", "Canvas");
    U.CanvasScaler = findClass("UnityEngine.UI", "CanvasScaler");
    U.GraphicRaycaster = findClass("UnityEngine.UI", "GraphicRaycaster");
    U.Shadow = findClass("UnityEngine.UI", "Shadow");
    U.Font = findClass("UnityEngine", "Font");
    Il2CppClass* Object = findClass("UnityEngine", "Object");
    Il2CppClass* Component = findClass("UnityEngine", "Component");
    Il2CppClass* Transform = findClass("UnityEngine", "Transform");
    Il2CppClass* Texture = findClass("UnityEngine", "Texture");
    Il2CppClass* Graphic = findClass("UnityEngine.UI", "Graphic");
    Il2CppClass* Selectable = findClass("UnityEngine.UI", "Selectable");
    Il2CppClass* UnityEvent = findClass("UnityEngine.Events", "UnityEvent");

    U.goCtor = findMethod(U.GameObject, ".ctor", 1, IL2CPP_TYPE_STRING);
    U.goAddComponent = findMethod(U.GameObject, "AddComponent", 1, IL2CPP_TYPE_CLASS);
    U.goSetActive = findMethod(U.GameObject, "SetActive", 1);
    U.compGameObject = findMethod(Component, "get_gameObject", 0);
    U.compTransform = findMethod(Component, "get_transform", 0);
    U.setParent = findMethod(Transform, "SetParent", 2);
    U.opImplicit = findMethod(Object, "op_Implicit", 1);
    U.destroy = findMethod(Object, "Destroy", 1);
    U.anchorMin = findMethod(U.RectTransform, "set_anchorMin", 1);
    U.anchorMax = findMethod(U.RectTransform, "set_anchorMax", 1);
    U.pivot = findMethod(U.RectTransform, "set_pivot", 1);
    U.anchoredPos = findMethod(U.RectTransform, "set_anchoredPosition", 1);
    U.sizeDelta = findMethod(U.RectTransform, "set_sizeDelta", 1);
    U.localScale = findMethod(Transform, "set_localScale", 1);
    U.texCtor = findMethod(U.Texture2D, ".ctor", 2, IL2CPP_TYPE_I4);
    U.setPixels32 = findMethod(U.Texture2D, "SetPixels32", 1);
    U.texApply = findMethod(U.Texture2D, "Apply", 0);
    U.texWrap = findMethod(Texture, "set_wrapMode", 1);
    U.texFilter = findMethod(Texture, "set_filterMode", 1);
    U.spriteCreate = findMethod(U.Sprite, "Create", 7);
    U.imgSprite = findMethod(U.Image, "set_sprite", 1);
    U.imgType = findMethod(U.Image, "set_type", 1);
    U.imgPPUMul = findMethod(U.Image, "set_pixelsPerUnitMultiplier", 1);
    U.gColor = findMethod(Graphic, "set_color", 1);
    U.gGetColor = findMethod(Graphic, "get_color", 0);
    U.gRaycast = findMethod(Graphic, "set_raycastTarget", 1);
    U.txtFont = findMethod(U.Text, "set_font", 1);
    U.txtGetFont = findMethod(U.Text, "get_font", 0);
    U.txtText = findMethod(U.Text, "set_text", 1);
    U.txtGetText = findMethod(U.Text, "get_text", 0);
    U.txtGetSize = findMethod(U.Text, "get_fontSize", 0);
    U.txtGetAlign = findMethod(U.Text, "get_alignment", 0);
    U.txtPrefH = findMethod(U.Text, "get_preferredHeight", 0);
    U.txtPrefW = findMethod(U.Text, "get_preferredWidth", 0);
    U.txtSize = findMethod(U.Text, "set_fontSize", 1);
    U.txtAlign = findMethod(U.Text, "set_alignment", 1);
    U.txtStyle = findMethod(U.Text, "set_fontStyle", 1);
    U.txtHOverflow = findMethod(U.Text, "set_horizontalOverflow", 1);
    U.txtVOverflow = findMethod(U.Text, "set_verticalOverflow", 1);
    U.btnOnClick = findMethod(U.Button, "get_onClick", 0);
    U.addListener = findMethod(UnityEvent, "AddListener", 1);
    U.targetGraphic = findMethod(Selectable, "set_targetGraphic", 1);
    U.cvMode = findMethod(U.Canvas, "set_renderMode", 1);
    U.cvOrder = findMethod(U.Canvas, "set_sortingOrder", 1);
    U.csMode = findMethod(U.CanvasScaler, "set_uiScaleMode", 1);
    U.csRef = findMethod(U.CanvasScaler, "set_referenceResolution", 1);
    U.csMatch = findMethod(U.CanvasScaler, "set_matchWidthOrHeight", 1);
    U.shColor = findMethod(U.Shadow, "set_effectColor", 1);
    U.shDist = findMethod(U.Shadow, "set_effectDistance", 1);
    U.findObjOfType = findMethod(Object, "FindObjectOfType", 1, IL2CPP_TYPE_CLASS);
    U.builtinRes = findMethod(findClass("UnityEngine", "Resources"), "GetBuiltinResource", 2, IL2CPP_TYPE_CLASS);
    U.unscaledDt = findMethod(findClass("UnityEngine", "Time"), "get_unscaledDeltaTime", 0);
    U.frameCount = findMethod(findClass("UnityEngine", "Time"), "get_frameCount", 0);
    U.levelTime = findMethod(findClass("UnityEngine", "Time"), "get_timeSinceLevelLoad", 0);
    Il2CppClass* PlayerPrefs = findClass("UnityEngine", "PlayerPrefs");
    U.prefGet = findMethod(PlayerPrefs, "GetInt", 2);
    U.prefSet = findMethod(PlayerPrefs, "SetInt", 2);
    U.prefSave = findMethod(PlayerPrefs, "Save", 0);
    U.prefGetStr = findMethod(PlayerPrefs, "GetString", 2);
    Il2CppClass* SceneManager = findClass("UnityEngine.SceneManagement", "SceneManager");
    U.loadScene = findMethod(SceneManager, "LoadScene", 1, IL2CPP_TYPE_STRING);
    U.activeScene = findMethod(SceneManager, "GetActiveScene", 0);
    U.goActiveSelf = findMethod(U.GameObject, "get_activeSelf", 0);
    U.goActiveHier = findMethod(U.GameObject, "get_activeInHierarchy", 0);
    U.goTransform = findMethod(U.GameObject, "get_transform", 0);
    U.localEuler = findMethod(Transform, "set_localEulerAngles", 1);
    U.getRect = findMethod(U.RectTransform, "get_rect", 0);
    U.animTrigger = findMethod(findClass("UnityEngine", "Animator"), "SetTrigger", 1, IL2CPP_TYPE_STRING);
    U.getAnchorMin = findMethod(U.RectTransform, "get_anchorMin", 0);
    U.getAnchorMax = findMethod(U.RectTransform, "get_anchorMax", 0);
    U.getPivot = findMethod(U.RectTransform, "get_pivot", 0);
    U.getAnchoredPos = findMethod(U.RectTransform, "get_anchoredPosition", 0);
    U.getSizeDelta = findMethod(U.RectTransform, "get_sizeDelta", 0);
    U.trParent = findMethod(Transform, "get_parent", 0);
    U.CanvasGroup = findClass("UnityEngine", "CanvasGroup");
    U.cgAlpha = U.CanvasGroup ? findMethod(U.CanvasGroup, "set_alpha", 1) : nullptr;
    U.cgIgnore = U.CanvasGroup ? findMethod(U.CanvasGroup, "set_ignoreParentGroups", 1) : nullptr;
    U.cgBlocks = U.CanvasGroup ? findMethod(U.CanvasGroup, "set_blocksRaycasts", 1) : nullptr;
    U.cgGetAlpha = U.CanvasGroup ? findMethod(U.CanvasGroup, "get_alpha", 0) : nullptr;
    U.cgGetIgnore = U.CanvasGroup ? findMethod(U.CanvasGroup, "get_ignoreParentGroups", 0) : nullptr;
    U.cvGetOrder = findMethod(U.Canvas, "get_sortingOrder", 0);
    U.worldCorners = findMethod(U.RectTransform, "GetWorldCorners", 1);
    U.Vector3 = findClass("UnityEngine", "Vector3");
    U.getPos = findMethod(Transform, "get_position", 0);
    U.setPosM = findMethod(Transform, "set_position", 1);
    U.getLocalPos = findMethod(Transform, "get_localPosition", 0);
    U.setLocalPosM = findMethod(Transform, "set_localPosition", 1);
    U.Animator = findClass("UnityEngine", "Animator");
    U.trChildCount = findMethod(Transform, "get_childCount", 0);
    U.trGetChild = findMethod(Transform, "GetChild", 1);
    U.bhEnabled = findMethod(findClass("UnityEngine", "Behaviour"), "set_enabled", 1);
    Il2CppClass* Input = findClass("UnityEngine", "Input");
    U.touchCount = findMethod(Input, "get_touchCount", 0);
    U.getTouch = findMethod(Input, "GetTouch", 1);
    Il2CppClass* Touch = findClass("UnityEngine", "Touch");
    U.tId = fieldOffset(Touch, "m_FingerId") - (int)sizeof(Il2CppObject);
    U.tPos = fieldOffset(Touch, "m_Position") - (int)sizeof(Il2CppObject);
    U.tPhase = fieldOffset(Touch, "m_Phase") - (int)sizeof(Il2CppObject);
    Il2CppClass* Screen = findClass("UnityEngine", "Screen");
    U.scrW = findMethod(Screen, "get_width", 0);
    U.scrH = findMethod(Screen, "get_height", 0);
    U.scrDpi = findMethod(Screen, "get_dpi", 0);
    Il2CppClass* AudioSource = findClass("UnityEngine", "AudioSource");
    U.asGetTime = findMethod(AudioSource, "get_time", 0);
    U.asSetTime = findMethod(AudioSource, "set_time", 1);
    U.asPitch = findMethod(AudioSource, "set_pitch", 1);
    U.asPlaying = findMethod(AudioSource, "get_isPlaying", 0);
    U.Graphic = Graphic;
    U.Transform = Transform;
    U.getComp = findMethod(U.GameObject, "GetComponent", 1, IL2CPP_TYPE_CLASS);
    {
        static const char* const tb[] = {"Type", "Boolean"};
        U.compsInChildren = findMethodSig(U.GameObject, "GetComponentsInChildren", 2, tb);
        static const char* const ot[] = {"Object", "Transform"};
        U.instantiate = findMethodSig(Object, "Instantiate", 2, ot);
    }
    U.objName = findMethod(Object, "get_name", 0);
    U.btnSetOnClick = findMethod(U.Button, "set_onClick", 1);
    U.ClickEvt = findNested(U.Button, "ButtonClickedEvent");
    U.clickEvtCtor = U.ClickEvt ? findMethod(U.ClickEvt, ".ctor", 0) : nullptr;
    U.EventSystem = findClass("UnityEngine.EventSystems", "EventSystem");
    U.InputModule = findClass("UnityEngine.EventSystems", "StandaloneInputModule");
    U.esCurrent = U.EventSystem ? findMethod(U.EventSystem, "get_current", 0) : nullptr;
    U.WillRender = findNested(U.Canvas, "WillRenderCanvases");
    U.addWillRender = findMethod(U.Canvas, "add_willRenderCanvases", 1);
    U.cvRoot = findMethod(U.Canvas, "get_rootCanvas", 0);
    U.cvGetMode = findMethod(U.Canvas, "get_renderMode", 0);
    U.cvCam = findMethod(U.Canvas, "get_worldCamera", 0);
    Il2CppClass* Camera = findClass("UnityEngine", "Camera");
    U.camMain = Camera ? findMethod(Camera, "get_main", 0) : nullptr;
    U.camW2S = Camera ? findMethod(Camera, "WorldToScreenPoint", 1) : nullptr;

    const void* must[] = {U.goCtor, U.goAddComponent, U.setParent, U.opImplicit, U.anchorMin, U.sizeDelta, U.texCtor,
                          U.setPixels32, U.spriteCreate, U.imgSprite, U.gColor, U.txtText, U.btnOnClick,
                          U.addListener, U.cvMode, U.prefGet, U.loadScene};
    for (const void* p : must)
        if (!p) return false;
    return true;
}

bool alive(Obj* o) { return o && callm<bool>(U.opImplicit, o); }
void destroy(Obj* o) {
    if (alive(o)) callm<void>(U.destroy, o);
}

Obj* gameObject(Obj* comp) { return callm<Obj*>(U.compGameObject, comp); }
// Accepts a Component or a GameObject (calling Component.get_transform on a GameObject crashes natively).
Obj* rt(Obj* o) {
    if (!o) return nullptr;
    if (il2cpp_object_get_class(o) == U.GameObject) return callm<Obj*>(U.goTransform, o);
    return callm<Obj*>(U.compTransform, o);
}
Obj* addComponent(Obj* go, Il2CppClass* k) { return callm<Obj*>(U.goAddComponent, go, typeObject(k)); }
void setActive(Obj* go, bool on) { callm<void>(U.goSetActive, go, on); }

Obj* newUI(const char* name, Obj* parent) {
    Obj* go = il2cpp_object_new(U.GameObject);
    callm<void>(U.goCtor, go, str(name));
    Obj* rt = addComponent(go, U.RectTransform);
    if (parent) callm<void>(U.setParent, rt, parent, false);
    return rt;
}

void place(Obj* rt, Vec2 anchor, Vec2 pivot, Vec2 pos, Vec2 size) {
    callm<void>(U.anchorMin, rt, anchor);
    callm<void>(U.anchorMax, rt, anchor);
    callm<void>(U.pivot, rt, pivot);
    callm<void>(U.sizeDelta, rt, size);
    callm<void>(U.anchoredPos, rt, pos);
}

void stretch(Obj* rt, float inset) {
    callm<void>(U.anchorMin, rt, Vec2{0, 0});
    callm<void>(U.anchorMax, rt, Vec2{1, 1});
    callm<void>(U.pivot, rt, Vec2{0.5f, 0.5f});
    callm<void>(U.sizeDelta, rt, Vec2{-2 * inset, -2 * inset});
    callm<void>(U.anchoredPos, rt, Vec2{0, 0});
}

void setPos(Obj* rt, Vec2 pos) { callm<void>(U.anchoredPos, rt, pos); }
void setScale(Obj* rt, float s) { callm<void>(U.localScale, rt, Vec3{s, s, 1}); }

// ---------- procedural sprites ----------
static inline float sq(float v) { return v * v; }

// coverage of a pixel by shape, 4x4 supersampled
template <typename F>
static float coverage(float px, float py, F inside) {
    int hit = 0;
    for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) hit += inside(px + (sx + 0.5f) / 4, py + (sy + 0.5f) / 4) ? 1 : 0;
    return hit / 16.f;
}

struct RGBA { uint8_t r, g, b, a; };

// Clawd, from Claude Code's block-character mascot. Each cell is half a terminal column wide and half a
// line tall, so cells are drawn 1:2 (7 x 14 px). '#' body, 'o' eye.
static const char* const kClawd[5] = {
    "   ############   ",
    "   ##o######o##   ",
    " ################ ",
    "   ############   ",
    "    # #    # #    ",
};
static const int kClawdCols = 18, kClawdRows = 5, kCellW = 7, kCellH = 14;

// a white four-pointed star (concave sides) on transparent, filling the whole texture
Obj* starTexture() {
    static Handle h;
    if (alive((Obj*)h.get())) return (Obj*)h.get();
    if (!U.Texture2D || !U.texCtor || !U.setPixels32 || !U.texApply) return nullptr;
    const int S = 64;
    Obj* tex = il2cpp_object_new(U.Texture2D);
    callm<void>(U.texCtor, tex, S, S);
    if (U.texWrap) callm<void>(U.texWrap, tex, 1);  // clamp
    Il2CppArray* px = newArray(U.Color32, (uintptr_t)(S * S));
    RGBA* p = (RGBA*)arrayData(px);
    const float c = S / 2.f, r = S / 2.f - 0.5f;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float a = coverage((float)x, (float)y, [&](float fx, float fy) {
                float u = __builtin_fabsf(fx - c) / r, v = __builtin_fabsf(fy - c) / r;
                // sqrt-curved sides (sharp points) plus a small diamond so the middle has some body
                return __builtin_sqrtf(u) + __builtin_sqrtf(v) <= 1.f || u + v <= 0.32f;
            });
            p[y * S + x] = RGBA{255, 255, 255, (uint8_t)(a * 255 + 0.5f)};
        }
    callm<void>(U.setPixels32, tex, px);
    callm<void>(U.texApply, tex);
    h.set(tex);
    return tex;
}

static Obj* buildSprite(SpriteKind kind) {
    const int R = 48;  // corner radius of the 9-slice rounded rect
    int W = 128, H = 128;
    if (kind == SPR_ROUND) W = H = 2 * R + 4;
    if (kind == SPR_CLAWD) {
        W = kClawdCols * kCellW + 2;  // 128
        H = kClawdRows * kCellH + 2;  // 72
    }
    Obj* tex = il2cpp_object_new(U.Texture2D);
    callm<void>(U.texCtor, tex, W, H);
    if (U.texWrap) callm<void>(U.texWrap, tex, 1);                               // clamp
    if (U.texFilter && kind == SPR_CLAWD) callm<void>(U.texFilter, tex, 0);      // point: crisp pixels
    Il2CppArray* px = newArray(U.Color32, (uintptr_t)(W * H));
    RGBA* p = (RGBA*)arrayData(px);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            RGBA c{255, 255, 255, 0};
            if (kind == SPR_ROUND) {
                float a = coverage((float)x, (float)y, [&](float fx, float fy) {
                    float cx = fx < R + 2 ? R + 2 : (fx > W - R - 2 ? W - R - 2 : fx);
                    float cy = fy < R + 2 ? R + 2 : (fy > H - R - 2 ? H - R - 2 : fy);
                    return sq(fx - cx) + sq(fy - cy) <= sq((float)R);
                });
                c.a = (uint8_t)(a * 255 + 0.5f);
            } else if (kind == SPR_CIRCLE) {
                float a = coverage((float)x, (float)y, [&](float fx, float fy) { return sq(fx - 64) + sq(fy - 64) <= sq(62.f); });
                c.a = (uint8_t)(a * 255 + 0.5f);
            } else {  // Clawd, colours baked in (texture rows go bottom-up)
                int gx = (x - 1) / kCellW, gy = (H - 2 - y) / kCellH;
                if (x >= 1 && y >= 1 && gx < kClawdCols && gy >= 0 && gy < kClawdRows) {
                    char ch = kClawd[gy][gx];
                    if (ch == '#') c = RGBA{0xD9, 0x77, 0x57, 255};
                    else if (ch == 'o') c = RGBA{0x2D, 0x1E, 0x16, 255};
                }
            }
            p[y * W + x] = c;
        }
    }
    callm<void>(U.setPixels32, tex, px);
    callm<void>(U.texApply, tex);
    float b = kind == SPR_ROUND ? (float)(R + 2) : 0.f;
    return callm<Obj*>(U.spriteCreate, tex, Rect{0, 0, (float)W, (float)H}, Vec2{0.5f, 0.5f}, 100.f, 0u, 0,
                       Vec4{b, b, b, b});
}

static Obj* sprite(SpriteKind k) {
    Obj* s = (Obj*)g_sprites[k].get();
    if (!alive(s)) {
        s = buildSprite(k);
        g_sprites[k].set(s);
    }
    return s;
}

Obj* image(Obj* parent, const char* name, SpriteKind kind, Color c, Vec2 anchor, Vec2 pos, Vec2 size, float radius,
           bool raycast) {
    Obj* rt = newUI(name, parent);
    place(rt, anchor, Vec2{0.5f, 0.5f}, pos, size);
    Obj* img = addComponent(gameObject(rt), U.Image);
    if (kind != SPR_SOLID) callm<void>(U.imgSprite, img, sprite(kind));
    if (kind == SPR_ROUND) {
        callm<void>(U.imgType, img, 1);  // sliced
        if (U.imgPPUMul) callm<void>(U.imgPPUMul, img, 48.f / (radius > 0 ? radius : 24.f));
    }
    callm<void>(U.gColor, img, c);
    callm<void>(U.gRaycast, img, raycast);
    return img;
}

// Unity shrinks the borders of a sliced image per axis when they don't fit, which turns round corners into
// flat ellipses: the radius has to follow the image's size
void setRadius(Obj* img, float radius) {
    if (alive(img) && U.imgPPUMul && radius > 0) callm<void>(U.imgPPUMul, img, 48.f / radius);
}

// ---------- drawn text ----------
// The mod's own texts are drawn by glyphs.cpp (Source Serif 4 + Noto Sans SC) into a texture shown by a RawImage
// child of the Text: Unity's fonts left glyphs out on the iPad. The Text component stays (empty) so everything
// that positions, colours or fades it keeps working; the functions below keep the drawing in step with it. A Text
// that is given a game font (setTextFont) goes back to Unity's own drawing.
}  // namespace unity
namespace glyphs {
bool ready();
float measure(const char* s, float px, bool bold, float wrap, float* height);
bool render(const char* s, float px, bool bold, float wrap, int align, int pad, unsigned char** rgba, int* w, int* h,
            float* textW, float* textH);
}  // namespace glyphs
namespace unity {

struct DText {
    Handle text, raw, tex;
    char* s;
    int size, align, texW, texH;
    bool bold, wrap;
    float boxW, width;  // width = drawn text width, in the Text's units
};
static DText* g_dt = nullptr;
static int g_dtN = 0, g_dtCap = 0;
static Il2CppClass* g_RawImage = nullptr;
static const MethodInfo *g_rawTex = nullptr, *g_tex4 = nullptr;

static char* dupStr(const char* s) {
    size_t n = s ? strlen(s) : 0;
    char* p = (char*)malloc(n + 1);
    if (p) {
        if (n) memcpy(p, s, n);
        p[n] = 0;
    }
    return p;
}

static DText* dtFind(Obj* t) {
    if (!t) return nullptr;
    for (int i = 0; i < g_dtN; i++)
        if (g_dt[i].text.get() == t) return &g_dt[i];
    return nullptr;
}

static void dtDrop(int i) {
    DText& e = g_dt[i];
    Obj* tex = (Obj*)e.tex.get();
    if (alive(tex)) destroy(tex);
    e.text.clear();
    e.raw.clear();
    e.tex.clear();
    free(e.s);
    g_dt[i] = g_dt[g_dtN - 1];
    memset(&g_dt[g_dtN - 1], 0, sizeof(DText));
    g_dtN--;
}

static void dtSweep() {  // texts that are gone (closed panel, new scene): free their textures
    for (int i = g_dtN - 1; i >= 0; i--)
        if (!alive((Obj*)g_dt[i].text.get())) dtDrop(i);
}

static bool dtReady() {
    static int st = 0;
    if (st) return st > 0;
    st = -1;
    g_RawImage = findClass("UnityEngine.UI", "RawImage");
    g_rawTex = g_RawImage ? findMethod(g_RawImage, "set_texture", 1) : nullptr;
    static const char* const sig[] = {"Int32", "Int32", "TextureFormat", "Boolean"};
    g_tex4 = findMethodSig(U.Texture2D, ".ctor", 4, sig);
    if (!g_RawImage || !g_rawTex || !g_tex4 || !U.Color32 || !glyphs::ready()) {
        LOG("text: own drawing unavailable (RawImage %d, Texture2D ctor %d), Unity fonts used", g_rawTex != nullptr,
            g_tex4 != nullptr);
        return false;
    }
    st = 1;
    return true;
}

// screen pixels per unit of this Text (at least the overlay canvas's), so the glyphs are drawn sharp
static float dtDensity(Obj* t) {
    Vec2 s = screenSize();
    float d = s.y > 0 ? s.y / canvasH() : 1;
    Rect r{0, 0, 0, 0};
    float x0, x1, y0, y1;
    if (rectOf(rt(t), &r) && r.h > 1 && screenBounds(rt(t), &x0, &x1, &y0, &y1)) {
        float m = (y1 - y0) / r.h;
        if (m > d && m < 8) d = m;
    }
    return d < 1 ? 1 : d > 4 ? 4 : d;
}

static void dtRender(DText& e) {
    Obj* t = (Obj*)e.text.get();
    if (!alive(t)) return;
    float d = dtDensity(t);
    float px = e.size * d;
    Rect cur{0, 0, 0, 0};  // the box as it is now (callers resize Texts after making them)
    if (rectOf(rt(t), &cur) && cur.w > 1) e.boxW = cur.w;
    float wrapPx = e.wrap && e.boxW > 0 ? e.boxW * d : 0;
    if (!e.wrap && e.boxW > 0) {  // one line that does not fit its box: smaller, down to 55 %
        float w = glyphs::measure(e.s, px, e.bold, 0, nullptr);
        if (w > e.boxW * d) {
            float k = e.boxW * d / w;
            px *= k < 0.55f ? 0.55f : k;
        }
    }
    const int pad = 2;
    int col = e.align % 3, row = e.align / 3;
    unsigned char* rgba;
    int W, H;
    float tw, th;
    if (!glyphs::render(e.s, px, e.bold, wrapPx, col, pad, &rgba, &W, &H, &tw, &th)) return;
    e.width = tw / d;
    // the texture: reused while the size stays the same (a readout that changes every frame)
    Obj* tex = (Obj*)e.tex.get();
    if (!alive(tex) || e.texW != W || e.texH != H) {
        if (alive(tex)) destroy(tex);
        tex = (Obj*)il2cpp_object_new(U.Texture2D);
        int fmt = 4;  // RGBA32
        bool mips = false;
        void* a[] = {&W, &H, &fmt, &mips};
        if (!invokeSafe(g_tex4, tex, a)) {
            free(rgba);
            return;
        }
        if (U.texWrap) callm<void>(U.texWrap, tex, 1);  // clamp
        e.tex.set(tex);
        e.texW = W;
        e.texH = H;
    }
    Il2CppArray* px32 = newArray(U.Color32, (uintptr_t)(W * H));
    unsigned char* dst = (unsigned char*)arrayData(px32);
    for (int y = 0; y < H; y++) memcpy(dst + (size_t)y * W * 4, rgba + (size_t)(H - 1 - y) * W * 4, (size_t)W * 4);  // bottom row first
    free(rgba);
    callm<void>(U.setPixels32, tex, px32);
    callm<void>(U.texApply, tex);
    Obj* raw = (Obj*)e.raw.get();
    if (!alive(raw)) {
        Obj* r = newUI("PhiMeText", rt(t));
        raw = addComponent(gameObject(r), g_RawImage);
        callm<void>(U.gRaycast, raw, false);
        e.raw.set(raw);
    }
    callm<void>(g_rawTex, raw, tex);
    callm<void>(U.gColor, raw, getColor(t));
    // the drawn block sits where Unity would have put the text: same alignment inside the Text's rect
    float ax = col * 0.5f, ay = row == 0 ? 1 : row == 1 ? 0.5f : 0;
    float off = (pad + 1) / d;
    Vec2 pos{col == 0 ? -off : col == 2 ? off : 0, row == 0 ? off : row == 2 ? -off : 0};
    place(rt(raw), Vec2{ax, ay}, Vec2{ax, ay}, pos, Vec2{W / d, H / d});
}

Obj* rawImage(Obj* parent, const char* name, Vec2 anchor, Vec2 pos, Vec2 size) {
    if (!dtReady()) return nullptr;
    Obj* r = newUI(name, parent);
    place(r, anchor, Vec2{0.5f, 0.5f}, pos, size);
    Obj* raw = addComponent(gameObject(r), g_RawImage);
    if (raw) callm<void>(U.gRaycast, raw, false);
    return raw;
}

uint8_t* pixelsBegin(Pixels& p, int w, int h) {
    if (!dtReady() || w <= 0 || h <= 0) return nullptr;
    Obj* tex = (Obj*)p.tex.get();
    Il2CppArray* arr = (Il2CppArray*)p.arr.get();
    if (!alive(tex) || !arr || p.w != w || p.h != h) {
        pixelsFree(p);
        tex = (Obj*)il2cpp_object_new(U.Texture2D);
        int fmt = 4;  // RGBA32
        bool mips = false;
        void* a[] = {&w, &h, &fmt, &mips};
        if (!invokeSafe(g_tex4, tex, a)) return nullptr;
        if (U.texWrap) callm<void>(U.texWrap, tex, 1);  // clamp
        arr = newArray(U.Color32, (uintptr_t)w * h);
        if (!arr) return nullptr;
        p.tex.set(tex);
        p.arr.set(arr);
        p.w = w;
        p.h = h;
    }
    return (uint8_t*)arrayData(arr);
}

void pixelsEnd(Pixels& p, Obj* raw) {
    Obj* tex = (Obj*)p.tex.get();
    Il2CppArray* arr = (Il2CppArray*)p.arr.get();
    if (!alive(tex) || !arr) return;
    callm<void>(U.setPixels32, tex, arr);
    callm<void>(U.texApply, tex);
    if (alive(raw)) callm<void>(g_rawTex, raw, tex);
}

void pixelsFree(Pixels& p) {
    Obj* tex = (Obj*)p.tex.get();
    if (alive(tex)) destroy(tex);
    p.tex.clear();
    p.arr.clear();
    p.w = p.h = 0;
}

static DText* dtAdd(Obj* t, const char* s, int size, bool bold, int align, float boxW) {
    dtSweep();
    if (g_dtN == g_dtCap) {
        int c = g_dtCap ? g_dtCap * 2 : 64;
        DText* n = (DText*)calloc(c, sizeof(DText));
        if (!n) return nullptr;
        if (g_dt) {
            memcpy(n, g_dt, sizeof(DText) * g_dtN);
            free(g_dt);
        }
        g_dt = n;
        g_dtCap = c;
    }
    DText& e = g_dt[g_dtN++];
    memset(&e, 0, sizeof e);
    e.text.set(t);
    e.s = dupStr(s);
    e.size = size;
    e.bold = bold;
    e.align = align;
    e.boxW = boxW;
    dtRender(e);
    return &e;
}

void setColor(Obj* g, Color c) {
    callm<void>(U.gColor, g, c);
    if (g_dtN) {
        DText* e = dtFind(g);
        Obj* raw = e ? (Obj*)e->raw.get() : nullptr;
        if (alive(raw)) callm<void>(U.gColor, raw, c);
    }
}
Color getColor(Obj* g) { return U.gGetColor ? callm<Color>(U.gGetColor, g) : Color{0, 0, 0, 0.5f}; }
void setRaycast(Obj* g, bool on) { callm<void>(U.gRaycast, g, on); }

static Obj* ownFont(int size, bool bold, const char* s, bool* realBold);

Obj* text(Obj* parent, const char* s, int size, Color c, int align, bool bold, Vec2 anchor, Vec2 pivot, Vec2 pos,
          Vec2 box) {
    Obj* rt = newUI("Text", parent);
    place(rt, anchor, pivot, pos, box);
    Obj* t = addComponent(gameObject(rt), U.Text);
    bool drawn = dtReady();
    if (!drawn) {
        bool realBold = false;
        Obj* f = ownFont(size, bold, s, &realBold);
        if (!f) f = fontFor(size, bold);
        if (f) callm<void>(U.txtFont, t, f);
        callm<void>(U.txtStyle, t, bold && !realBold ? 1 : 0);
    }
    callm<void>(U.txtSize, t, size);
    callm<void>(U.txtAlign, t, align);
    if (U.txtHOverflow) callm<void>(U.txtHOverflow, t, 1);
    if (U.txtVOverflow) callm<void>(U.txtVOverflow, t, 1);
    callm<void>(U.gColor, t, c);
    callm<void>(U.gRaycast, t, false);
    if (drawn) dtAdd(t, s, size, bold, align, box.x);
    else callm<void>(U.txtText, t, str(s));
    return t;
}

// new text in the current language (drawn text: drawn again; Unity text: the font is chosen again)
void retext(Obj* t, const char* s, int size, bool bold) {
    if (!alive(t)) return;
    if (DText* e = dtFind(t)) {
        free(e->s);
        e->s = dupStr(s);
        e->size = size;
        e->bold = bold;
        dtRender(*e);
        return;
    }
    bool realBold = false;
    Obj* f = ownFont(size, bold, s, &realBold);
    if (!f) f = fontFor(size, bold);
    if (f) callm<void>(U.txtFont, t, f);
    callm<void>(U.txtSize, t, size);
    callm<void>(U.txtStyle, t, bold && !realBold ? 1 : 0);
    callm<void>(U.txtText, t, str(s));
}

void setTextSize(Obj* t, int size) {
    if (!alive(t)) return;
    if (DText* e = dtFind(t)) {
        if (e->size != size) {
            e->size = size;
            dtRender(*e);
        }
        return;
    }
    callm<void>(U.txtSize, t, size);
}
void setText(Obj* t, const char* s) {
    if (DText* e = dtFind(t)) {
        if (e->s && !strcmp(e->s, s ? s : "")) return;
        free(e->s);
        e->s = dupStr(s);
        dtRender(*e);
        return;
    }
    callm<void>(U.txtText, t, str(s));
}
void setTextWrap(Obj* t, bool wrap) {
    if (DText* e = dtFind(t)) {
        if (e->wrap != wrap) {
            e->wrap = wrap;
            dtRender(*e);
        }
        return;
    }
    if (alive(t) && U.txtHOverflow) callm<void>(U.txtHOverflow, t, wrap ? 0 : 1);
}
float textWidth(const char* s, int size, bool bold) {
    if (dtReady()) return glyphs::measure(s, (float)size, bold, 0, nullptr);
    float w = 0;  // estimate: CJK about 1 em, the rest about 0.55 em
    for (const unsigned char* p = (const unsigned char*)s; p && *p; p++) {
        if ((*p & 0xC0) == 0x80) continue;
        w += *p >= 0x80 ? size : size * 0.55f;
    }
    return w;
}
float textHeight(const char* s, int size, bool bold, float wrapW) {
    if (!s || !*s) return 0;
    if (dtReady()) {
        float h = 0;
        glyphs::measure(s, (float)size, bold, wrapW, &h);
        if (h > 0) return h;
    }
    // estimate: each paragraph's width over wrapW, lines about 1.3 em apart
    int lines = 0;
    const char* p = s;
    while (true) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        char buf[1024];
        int n = (int)(e - p) < (int)sizeof buf - 1 ? (int)(e - p) : (int)sizeof buf - 1;
        memcpy(buf, p, n);
        buf[n] = 0;
        float w = textWidth(buf, size, bold);
        int k = wrapW > 0 ? (int)(w / wrapW) + 1 : 1;
        lines += k;
        if (!*e) break;
        p = e + 1;
    }
    return lines * size * 1.3f;
}
Il2CppString* textStr(Obj* t) { return alive(t) && U.txtGetText ? callm<Il2CppString*>(U.txtGetText, t) : nullptr; }
Obj* textFont(Obj* t) { return alive(t) && U.txtGetFont ? callm<Obj*>(U.txtGetFont, t) : nullptr; }
void setTextFont(Obj* t, Obj* f) {
    if (!alive(t) || !f) return;
    for (int i = 0; i < g_dtN; i++)  // a game font: back to Unity's own drawing, with the same string
        if (g_dt[i].text.get() == t) {
            Obj* raw = (Obj*)g_dt[i].raw.get();
            if (alive(raw)) destroy(gameObject(raw));
            callm<void>(U.txtText, t, str(g_dt[i].s));
            callm<void>(U.txtStyle, t, g_dt[i].bold ? 1 : 0);
            dtDrop(i);
            break;
        }
    callm<void>(U.txtFont, t, f);
}
int textFontSize(Obj* t) { return alive(t) && U.txtGetSize ? callm<int>(U.txtGetSize, t) : 0; }
int textAlignment(Obj* t) { return alive(t) && U.txtGetAlign ? callm<int>(U.txtGetAlign, t) : MiddleCenter; }
float textPreferredWidth(Obj* t) {
    if (DText* e = dtFind(t)) return e->width;
    return alive(t) && U.txtPrefW ? callm<float>(U.txtPrefW, t) : 0;
}
float textPreferredHeight(Obj* t) { return alive(t) && U.txtPrefH ? callm<float>(U.txtPrefH, t) : 0; }
void setTextStr(Obj* t, Il2CppString* s) {
    if (!alive(t) || !s) return;
    if (dtFind(t)) {
        char b[2048];
        utf8(s, b, sizeof b);
        setText(t, b);
        return;
    }
    callm<void>(U.txtText, t, s);
}
Obj* addCanvasGroup(Obj* go) { return U.CanvasGroup ? addComponent(go, U.CanvasGroup) : nullptr; }
void setGroupAlpha(Obj* cg, float a) {
    if (alive(cg) && U.cgAlpha) callm<void>(U.cgAlpha, cg, a);
}
void setIgnoreParentGroups(Obj* cg, bool on) {
    if (alive(cg) && U.cgIgnore) callm<void>(U.cgIgnore, cg, on);
}
void setBlocksRaycasts(Obj* cg, bool on) {
    if (alive(cg) && U.cgBlocks) callm<void>(U.cgBlocks, cg, on);
}
void hideUi(Obj* go) {
    if (!alive(go) || !U.CanvasGroup) return;
    Obj* cg = getComponent(go, U.CanvasGroup);
    if (!cg) cg = addComponent(go, U.CanvasGroup);
    setGroupAlpha(cg, 0);
    setBlocksRaycasts(cg, false);
}

void shadow(Obj* g, Color c, Vec2 d) {
    if (!U.shColor || !U.shDist) return;
    Obj* sh = addComponent(gameObject(g), U.Shadow);
    callm<void>(U.shColor, sh, c);
    callm<void>(U.shDist, sh, d);
}

void onClick(Obj* go, Obj* targetGraphic, NativeCallback cb, void* user) {
    Obj* btn = addComponent(go, U.Button);
    if (targetGraphic && U.targetGraphic) callm<void>(U.targetGraphic, btn, targetGraphic);
    Obj* evt = callm<Obj*>(U.btnOnClick, btn);
    Obj* action = makeUnityAction(cb, user);
    if (evt && action) callm<void>(U.addListener, evt, action);
}

// Phones: our cards were laid out on an iPad, where 1080 units are ~15 cm; on a phone they are ~7 cm, so the text
// came out tiny. Decided by the aspect ratio (phones are 16:9 and wider, tablets 16:10 and narrower); Screen.dpi
// is only logged: on the first phone tried it made the phone look like a tablet.
static int g_phone = -1;
bool isPhone() {
    if (g_phone >= 0) return g_phone == 1;
    Vec2 s = screenSize();
    float h = s.x < s.y ? s.x : s.y, w = s.x < s.y ? s.y : s.x;
    if (h < 1) return false;  // not known yet: ask again later
    float dpi = U.scrDpi ? callm<float>(U.scrDpi) : 0;
    float inches = dpi >= 100 && dpi <= 900 ? h / dpi : 0;
    g_phone = w / h >= 1.7f;
    LOG("ui: screen %.0fx%.0f, dpi %.0f (%.2f in tall) -> %s, ui scale %.2f", w, h, dpi, inches,
        g_phone ? "phone" : "tablet", g_phone ? 1.3f : 1.f);
    return g_phone == 1;
}
float uiScale() { return isPhone() ? 1.3f : 1.f; }
float canvasH() { return 1080.f / uiScale(); }

static void fadeAdd(Obj* rt);
Obj* overlayCanvas(const char* name, int order, float needH) {
    Obj* rt = newUI(name, nullptr);
    Obj* go = gameObject(rt);
    Obj* cv = addComponent(go, U.Canvas);
    callm<void>(U.cvMode, cv, 0);  // ScreenSpaceOverlay
    callm<void>(U.cvOrder, cv, order);
    Obj* cs = addComponent(go, U.CanvasScaler);
    float k = uiScale();
    if (needH > 0 && k > 1080.f / needH) k = 1080.f / needH > 1 ? 1080.f / needH : 1;
    if (U.csMode) callm<void>(U.csMode, cs, 1);  // ScaleWithScreenSize
    if (U.csRef) callm<void>(U.csRef, cs, Vec2{1920 / k, 1080 / k});
    if (U.csMatch) callm<void>(U.csMatch, cs, 1.f);  // follow screen height
    addComponent(go, U.GraphicRaycaster);
    fadeAdd(rt);
    return rt;
}

// ---------- fades of our overlay canvases ----------
// Every overlay canvas of ours gets a CanvasGroup on its root: it fades in when made, uiFadeAway fades it out and
// then destroys it, and the game's scene wipe (the CutOut / CutIn prefabs) is lifted above our canvases so it covers
// our UI the way it covers the game's (before: the screen went black under our buttons, which vanished later).
struct FadeEntry {
    Handle root, group;
    float a, target;
    bool kill;
};
static FadeEntry g_fades[48];
static bool g_fadeTicking = false;
static const float kFadeIn = 0.22f, kFadeOut = 0.18f;

static void fadeTick(void*) {
    float dt = unscaledDt();
    if (dt > 0.1f) dt = 0.1f;  // the first frame after a scene load
    for (FadeEntry& e : g_fades) {
        Obj* r = (Obj*)e.root.get();
        if (!r) continue;
        if (!alive(r)) {
            e.root.clear();
            e.group.clear();
            continue;
        }
        if (e.a == e.target) continue;
        float step = dt / (e.target < e.a ? kFadeOut : kFadeIn);
        e.a = e.a < e.target ? (e.a + step > e.target ? e.target : e.a + step)
                             : (e.a - step < e.target ? e.target : e.a - step);
        float s = e.a * e.a * (3 - 2 * e.a);  // ease in-out
        setGroupAlpha((Obj*)e.group.get(), s);
        if (e.kill && e.a <= 0) {
            destroy(gameObject(r));
            e.root.clear();
            e.group.clear();
        }
    }
}

static void fadeAdd(Obj* rt) {
    Obj* g = alive(rt) ? addCanvasGroup(gameObject(rt)) : nullptr;
    if (!g) return;
    for (FadeEntry& e : g_fades) {
        if (alive((Obj*)e.root.get())) continue;
        e.root.set(rt);
        e.group.set(g);
        e.a = 0;
        e.target = 1;
        e.kill = false;
        setGroupAlpha(g, 0);
        if (!g_fadeTicking) {
            g_fadeTicking = true;
            onEveryFrame(fadeTick);
        }
        return;
    }
    // no free slot: shown at once, as before
}

void uiFadeAway(Obj* rt) {
    if (!alive(rt)) return;
    for (FadeEntry& e : g_fades) {
        if (e.root.get() != rt) continue;
        e.target = 0;
        e.kill = true;
        setBlocksRaycasts((Obj*)e.group.get(), false);  // taps go through while it fades
        return;
    }
    destroy(gameObject(rt));
}

// the game's scene wipe: CutOut (leaving a scene) / CutIn (entering one), from the Instantiate hook
void uiFadeOnClone(Obj* src, Obj* clone) {
    if (!src || !clone || !U.GameObject || il2cpp_object_get_class(src) != U.GameObject) return;
    char n[24] = "";
    objName(src, n, sizeof n);
    bool out = !memcmp(n, "CutOut", 6), in = !memcmp(n, "CutIn", 5);
    if (!out && !in) return;
    Il2CppArray* cs = componentsInChildren(clone, U.Canvas);
    int nc = cs ? (int)cs->max_length : 0, raised = 0;
    for (int i = 0; i < nc; i++) {
        Obj* cv = (Obj*)arrayData(cs)[i];
        if (!alive(cv)) continue;
        int mode = U.cvGetMode ? callm<int>(U.cvGetMode, cv) : -1;
        int order = U.cvGetOrder ? callm<int>(U.cvGetOrder, cv) : 0;
        static int logged = 0;
        if (logged < 6) {
            logged++;
            LOG("ui: scene wipe '%s', canvas %d: mode %d order %d%s", n, i, mode, order,
                mode == 0 ? " -> above ours (32767)" : "");
        }
        if (mode == 0) {  // overlay: drawn after ours once its order is higher
            callm<void>(U.cvOrder, cv, 32767);
            raised++;
        }
    }
    if (out && !raised) {  // a wipe we cannot draw over ours: ours fade out with it instead
        for (FadeEntry& e : g_fades)
            if (alive((Obj*)e.root.get())) e.target = 0;
        LOG("ui: scene wipe '%s' (%d canvases, none overlay): our overlays fade out", n, nc);
    }
}

// how visible a piece of the game's UI is: inactive 0, else its Graphic's alpha x the CanvasGroups above it
float uiAlphaOf(Obj* t) {
    if (!alive(t) || !activeInHierarchy(gameObject(t))) return 0;
    float a = 1;
    Obj* g = U.Graphic ? getComponent(gameObject(t), U.Graphic) : nullptr;
    if (g) a *= getColor(g).a;
    for (int guard = 0; t && guard < 32; guard++, t = parentOf(t)) {
        Obj* cg = U.CanvasGroup ? getComponent(gameObject(t), U.CanvasGroup) : nullptr;
        if (!cg) continue;
        if (U.cgGetAlpha) a *= callm<float>(U.cgGetAlpha, cg);
        if (U.cgGetIgnore && callm<bool>(U.cgGetIgnore, cg)) break;
    }
    return a;
}

// ---------- font ----------
// Our labels are Chinese. Borrowing "whatever font a Text on screen uses" broke on iOS: that font only had
// part of the glyphs (missing characters). So every candidate is scored by how many of the characters we
// use it really has, and the best one wins:
//   1. CJK system fonts by name (PingFang on iOS, Noto CJK on Android, ...)
//   2. every Font asset the game has loaded
//   3. Unity's built-in font (OS fallback)
static const char* const kProbe =
    "镜像练习自动继续前回放谱面准备时间预备秒小助手设置选段取消开始变速不调循环播放准度显示计算实最高大字判定详情连击快速加载曲包其他";

static int decodeUtf8(const char* s, uint16_t* out, int cap) {
    int n = 0;
    const unsigned char* p = (const unsigned char*)s;
    while (*p && n < cap) {
        uint32_t c = *p;
        if (c < 0x80) p += 1;
        else if ((c & 0xE0) == 0xC0) c = ((c & 0x1F) << 6) | (p[1] & 0x3F), p += 2;
        else if ((c & 0xF0) == 0xE0) c = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F), p += 3;
        else {
            p += 4;
            continue;
        }
        out[n++] = (uint16_t)c;
    }
    return n;
}

static int fontScore(Obj* f, const MethodInfo* has, const uint16_t* cs, int n) {
    if (!alive(f) || !has) return -1;
    int hit = 0;
    for (int i = 0; i < n; i++) {
        uint16_t c = cs[i];
        void* args[] = {&c};
        Il2CppObject* exc = nullptr;
        Il2CppObject* r = il2cpp_runtime_invoke(has, f, args, &exc);
        if (exc || !r) return -1;
        if (*(bool*)il2cpp_object_unbox(r)) hit++;
    }
    return hit;
}

// The names are a fallback chain: the first one found is the font, the rest cover the glyphs it lacks.
// Chinese UI: PingFang SC (64/64 of our characters on the iPad; Hiragino Sans GB came out as a Japanese face).
// English UI: a serif (Charter / Iowan Old Style / Georgia on iOS, Noto Serif on Android), CJK after it for
// song names and the like.
static bool g_fontEn = false;
static const char* const kCjkNames[] = {
    "PingFang SC", "PingFangSC-Regular", ".PingFang SC", "Heiti SC",
    "STHeitiSC-Light", "Noto Sans CJK SC", "NotoSansCJK-Regular", "Noto Sans SC", "Source Han Sans SC",
    "Droid Sans Fallback", "Arial Unicode MS", "sans-serif"};
static const char* const kSerifNames[] = {"Charter", "Charter-Roman", "Iowan Old Style", "IowanOldStyle-Roman",
                                          "Georgia", "Times New Roman", "Noto Serif", "NotoSerif-Regular", "serif"};

static Obj* osCjkFont() {
    const MethodInfo* create = findMethod(U.Font, "CreateDynamicFontFromOSFont", 2, IL2CPP_TYPE_SZARRAY);
    Il2CppClass* String = findClass("System", "String");
    if (!create || !String) return nullptr;
    const int nc = (int)(sizeof(kCjkNames) / sizeof(kCjkNames[0]));
    const int ns = g_fontEn ? (int)(sizeof(kSerifNames) / sizeof(kSerifNames[0])) : 0;
    const int n = ns + nc;
    Il2CppArray* arr = newArray(String, n);
    if (!arr) return nullptr;
    for (int i = 0; i < ns; i++) arrayData(arr)[i] = str(kSerifNames[i]);
    for (int i = 0; i < nc; i++) arrayData(arr)[ns + i] = str(kCjkNames[i]);
    int size = 48;
    void* args[] = {arr, &size};
    Il2CppObject* exc = nullptr;
    Il2CppObject* f = il2cpp_runtime_invoke(create, nullptr, args, &exc);
    return exc ? nullptr : (Obj*)f;
}

static bool g_fontIsOs = false;  // the chosen font is one of our OS font instances (more can be made)

void rememberFont(Obj* f) {
    if (alive(f)) g_font.set(f);
}

Obj* findFont() {
    Obj* f = (Obj*)g_font.get();
    if (alive(f)) return f;
    if (g_fontEn) {  // English UI: the serif chain, no probing for Chinese characters
        Obj* s = osCjkFont();
        if (alive(s)) {
            g_font.set(s);
            g_fontIsOs = true;
            LOG("font: serif chain for the English UI");
            return s;
        }
    }
    uint16_t cs[64];
    int n = decodeUtf8(kProbe, cs, 64);
    static const MethodInfo* has = nullptr;
    if (!has) {
        static const char* const ch[] = {"Char"};
        has = findMethodSig(U.Font, "HasCharacter", 1, ch);
    }
    Obj* best = nullptr;
    int bestScore = -1;
    char bestName[64] = "?";
    auto consider = [&](Obj* c, const char* tag) {
        int s = fontScore(c, has, cs, n);
        if (s > bestScore) {
            best = c;
            bestScore = s;
            char nm[48] = "";
            objName(c, nm, sizeof nm);
            snprintf(bestName, sizeof bestName, "%s:%s", tag, nm);
        }
    };
    consider(osCjkFont(), "os");
    if (bestScore < n) {
        const MethodInfo* all = findMethod(findClass("UnityEngine", "Resources"), "FindObjectsOfTypeAll", 1,
                                           IL2CPP_TYPE_CLASS);
        Il2CppArray* fonts = all ? callm<Il2CppArray*>(all, typeObject(U.Font)) : nullptr;
        int nf = fonts ? (int)fonts->max_length : 0;
        for (int i = 0; i < nf && i < 64 && bestScore < n; i++) consider((Obj*)arrayData(fonts)[i], "game");
    }
    if (bestScore < n && U.builtinRes)
        consider(callm<Obj*>(U.builtinRes, typeObject(U.Font), str("LegacyRuntime.ttf")), "builtin");
    if (!best && U.findObjOfType) {  // no HasCharacter: old behaviour
        Obj* t = callm<Obj*>(U.findObjOfType, typeObject(U.Text));
        if (alive(t)) best = callm<Obj*>(U.txtGetFont, t);
    }
    if (alive(best)) {
        g_font.set(best);
        g_fontIsOs = bestName[0] == 'o' && bestName[1] == 's' && bestName[2] == ':';
        LOG("font: %s, %d/%d of our characters", bestName, bestScore, n);
        return best;
    }
    return nullptr;
}

// One dynamic font keeps every glyph it shows, at every pixel size and style in use, in one texture. On an iPad
// the canvases scale up ~1.5-1.9x, so the glyphs are 2-4x larger than on a phone and the texture runs full:
// Unity then leaves the characters that do not fit blank ("Failed to generate dynamic font texture"). So each
// size / style gets a separate instance of the same OS font with a texture of its own (sharing a few per
// texture still ran full on an iPad Pro), up to kFontPool; sizes beyond that share.
static const int kFontPool = 24;
static Handle g_fontPool[kFontPool];
static int g_variantKey[64], g_variantN = 0;

// ---- the mod's own font files (assets.cpp): no more guessing which system font iOS hands out ----
// v0.14 on an iPad lost exactly the simplified-only characters (镜 练 习 动), so the OS gave a Japanese /
// traditional face for "PingFang SC" / "Hiragino Sans GB". Now the fonts come with the mod: written once to
// <persistentDataPath>/PhiMeFonts and opened with Font.Internal_CreateFontFromPath.
//   English UI: Source Serif 4 (Regular / Bold); a string with CJK in it uses the Chinese font instead
//   Chinese UI: Noto Sans SC (Regular; Bold where it has every character of the string)
// Each size gets its own instance (own glyph texture), a few per font at most.
bool embeddedFont(int which, const unsigned char** data, int* size, const char** name);
enum { EF_SERIF_R, EF_SERIF_B, EF_CJK_R, EF_CJK_B, EF_N };
static char g_efPath[EF_N][512];
static int g_efState = 0;  // 0 not tried, 1 ready, -1 unavailable (old system-font path)
static const MethodInfo *g_efFromPath = nullptr, *g_efHas = nullptr;
static const int kEfCap[EF_N] = {12, 8, 6, 6};  // instances per font (the CJK file is 2.5 MB)
struct EfSlot {
    int which, key;
    Handle font;
};
static EfSlot g_ef[48];
static int g_efN = 0;
static Handle g_efProbeBold, g_efMatSrc;  // g_efMatSrc: a system font's material, copied into ours if they need one
static const MethodInfo* g_efSetMat = nullptr;
static bool efUsable(int which, const char* sample, uint16_t ch);
static Obj* copyMaterial(Obj* src);

static bool efReady() {
    if (g_efState) return g_efState > 0;
    g_efState = -1;
    g_efFromPath = findMethod(U.Font, "Internal_CreateFontFromPath", 2);
    static const char* const ch[] = {"Char"};
    g_efHas = findMethodSig(U.Font, "HasCharacter", 1, ch);
    const MethodInfo* pdp = findMethod(findClass("UnityEngine", "Application"), "get_persistentDataPath", 0);
    char dir[400];
    if (!g_efFromPath || !pdp || utf8(callm<Il2CppString*>(pdp), dir, 360) <= 0) {
        LOG("font: own fonts unavailable (CreateFontFromPath %d), using system fonts", g_efFromPath != nullptr);
        return false;
    }
    snprintf(dir + strlen(dir), 40, "/PhiMeFonts");
    mkdir(dir, 0755);
    for (int i = 0; i < EF_N; i++) {
        const unsigned char* d;
        int n;
        const char* name;
        if (!embeddedFont(i, &d, &n, &name)) return false;
        snprintf(g_efPath[i], sizeof g_efPath[i], "%s/%d-%s", dir, n, name);  // the size in the name: new file = new name
        int fd = open(g_efPath[i], 0);
        if (fd >= 0) {
            close(fd);
            continue;
        }
        char tmp[540];
        snprintf(tmp, sizeof tmp, "%s.tmp", g_efPath[i]);
        fd = open(tmp, PM_O_REPLACE, 0644);
        bool ok = fd >= 0 && write(fd, d, n) == n;
        if (fd >= 0) close(fd);
        if (!ok || rename(tmp, g_efPath[i]) != 0) {
            LOG("font: could not write %s", g_efPath[i]);
            return false;
        }
        LOG("font: wrote %s (%d bytes)", g_efPath[i], n);
    }
    // 0.14.1 on an iPad: fonts opened this way drew nothing at all. So one is tested first: it must really put
    // a glyph into its texture, or every font goes back to the system ones.
    g_efState = 1;
    if (!efUsable(EF_SERIF_R, "A", 'A') || !efUsable(EF_CJK_R, "中", 0x4E2D)) {
        g_efState = -1;
        LOG("font: own font files do not render here, using system fonts");
        return false;
    }
    return true;
}

static Obj* efNew(int which) {
    Obj* f = (Obj*)il2cpp_object_new(U.Font);
    void* args[] = {f, str(g_efPath[which])};
    Il2CppObject* exc = nullptr;
    il2cpp_runtime_invoke(g_efFromPath, nullptr, args, &exc);
    if (exc || !alive(f)) {
        LOG("font: %s did not open", g_efPath[which]);
        return nullptr;
    }
    if (g_efMatSrc.h && g_efSetMat) {  // the test font only worked with a material: each font its own copy
        Obj* m = copyMaterial((Obj*)g_efMatSrc.get());
        if (m) callm<void>(g_efSetMat, f, m);
    }
    return f;
}

static Obj* copyMaterial(Obj* src) {
    static Il2CppClass* Mat = findClass("UnityEngine", "Material");
    static const char* const sig[] = {"Material"};  // not the (Shader) one
    static const MethodInfo* ctor = Mat ? findMethodSig(Mat, ".ctor", 1, sig) : nullptr;
    if (!ctor || !alive(src)) return nullptr;
    Obj* m = (Obj*)il2cpp_object_new(Mat);
    void* a[] = {src};
    return invokeSafe(ctor, m, a) ? m : nullptr;
}

// does a font made from a file draw? Request a glyph, then ask for it back (a font without a texture / material
// never has one). Tried once more with a material copied from a system font.
static bool efUsable(int which, const char* sample, uint16_t ch) {
    const MethodInfo* req = findMethod(U.Font, "RequestCharactersInTexture", 3);
    const MethodInfo* gci = findMethod(U.Font, "GetCharacterInfo", 4);
    const MethodInfo* getMat = findMethod(U.Font, "get_material", 0);
    const MethodInfo* dyn = findMethod(U.Font, "get_dynamic", 0);
    g_efSetMat = findMethod(U.Font, "set_material", 1);
    Obj* f = efNew(which);
    if (!f || !req || !gci || !getMat) {
        LOG("font: test of %s impossible (font %d, methods %d %d %d)", g_efPath[which], f != nullptr, req != nullptr,
            gci != nullptr, getMat != nullptr);
        return false;
    }
    auto test = [&](const char* tag) {
        unsigned char info[128] = {0};
        Il2CppObject* exc = nullptr;
        int size = 40, style = 0;
        void* ra[] = {str(sample), &size, &style};
        il2cpp_runtime_invoke(req, f, ra, &exc);
        if (exc) return false;
        uint16_t c = ch;
        void* ga[] = {&c, info, &size, &style};
        Il2CppObject* r = il2cpp_runtime_invoke(gci, f, ga, &exc);
        bool got = !exc && r && *(bool*)il2cpp_object_unbox(r);
        Obj* mat = callm<Obj*>(getMat, f);
        LOG("font: test %s (%s): dynamic %d, material %s, glyph %s", g_efPath[which], tag,
            dyn ? (int)callm<bool>(dyn, f) : -1, alive(mat) ? "yes" : "none", got ? "yes" : "NO");
        return got && alive(mat);
    };
    if (test("as opened")) return true;
    // no material: borrow a copy of a system font's
    Obj* os = osCjkFont();
    Obj* osMat = alive(os) ? callm<Obj*>(getMat, os) : nullptr;
    Obj* m = alive(osMat) && g_efSetMat ? copyMaterial(osMat) : nullptr;
    if (!m) return false;
    callm<void>(g_efSetMat, f, m);
    if (!test("with a copied material")) return false;
    g_efMatSrc.set(osMat);  // later instances get their own copy (efNew)
    return true;
}

static bool efHas(Obj* f, uint16_t c) {
    if (!g_efHas || !alive(f)) return false;
    void* args[] = {&c};
    Il2CppObject* exc = nullptr;
    Il2CppObject* r = il2cpp_runtime_invoke(g_efHas, f, args, &exc);
    return !exc && r && *(bool*)il2cpp_object_unbox(r);
}

static Obj* efFont(int which, int key) {
    int have = 0, first = -1;
    for (int i = 0; i < g_efN; i++) {
        if (g_ef[i].which != which) continue;
        if (alive((Obj*)g_ef[i].font.get())) {
            if (g_ef[i].key == key) return (Obj*)g_ef[i].font.get();
            have++;
            if (first < 0) first = i;
        }
    }
    if (have >= kEfCap[which] || g_efN >= 48) return first >= 0 ? (Obj*)g_ef[first].font.get() : nullptr;
    Obj* f = efNew(which);
    if (!f) return first >= 0 ? (Obj*)g_ef[first].font.get() : nullptr;
    g_ef[g_efN].which = which;
    g_ef[g_efN].key = key;
    g_ef[g_efN].font.set(f);
    g_efN++;
    return f;
}

static bool hasCjk(const char* s) {
    // UTF-8 lead bytes: E3..EF = U+3000..U+FFFF (CJK, kana, fullwidth), E2 BA..BF = U+2E80..U+2FFF, F0+ = emoji etc.
    for (const unsigned char* p = (const unsigned char*)s; p && *p; p++)
        if (*p >= 0xE3 || (*p == 0xE2 && p[1] >= 0xBA)) return true;
    return false;
}

// the font for a Text showing `s` (may be null); *realBold = the font itself is bold (no synthetic bold)
static Obj* ownFont(int size, bool bold, const char* s, bool* realBold) {
    *realBold = false;
    if (!efReady()) return nullptr;
    int key = size * 2 + (bold ? 1 : 0);
    if (g_fontEn && !hasCjk(s)) {
        Obj* f = efFont(bold ? EF_SERIF_B : EF_SERIF_R, key);
        if (f) *realBold = bold;
        return f;
    }
    if (bold && s) {  // the bold CJK file only has the mod's own characters
        Obj* probe = (Obj*)g_efProbeBold.get();
        if (!alive(probe)) {
            probe = efNew(EF_CJK_B);
            g_efProbeBold.set(probe);
        }
        uint16_t cs[256];
        int n = decodeUtf8(s, cs, 256), ok = probe ? 1 : 0;
        for (int i = 0; i < n && ok; i++)
            if (cs[i] > ' ' && !efHas(probe, cs[i])) ok = 0;
        if (ok) {
            Obj* f = efFont(EF_CJK_B, key);
            if (f) {
                *realBold = true;
                return f;
            }
        }
    }
    return efFont(EF_CJK_R, key);
}

void setFontLanguage(bool en) {
    if (en == g_fontEn) return;
    g_fontEn = en;
    g_font.clear();
    for (int i = 0; i < kFontPool; i++) g_fontPool[i].clear();
    g_variantN = 0;
    LOG("font: UI language %s", en ? "English (Source Serif 4)" : "Chinese (Noto Sans SC)");
}
bool fontLanguageEn() { return g_fontEn; }

Obj* fontFor(int size, bool bold) {
    bool rb;
    Obj* own = ownFont(size, bold, nullptr, &rb);
    if (own) return own;
    Obj* base = findFont();
    if (!base || !g_fontIsOs) return base;  // a game / built-in font cannot be duplicated
    int key = size * 2 + (bold ? 1 : 0), slot = -1;
    for (int i = 0; i < g_variantN; i++)
        if (g_variantKey[i] == key) slot = i % kFontPool;
    if (slot < 0) {
        slot = (g_variantN < 64 ? g_variantN : key) % kFontPool;
        if (g_variantN < 64) g_variantKey[g_variantN++] = key;
    }
    if (slot == 0) return base;
    Obj* f = (Obj*)g_fontPool[slot].get();
    if (alive(f)) return f;
    f = osCjkFont();
    if (!alive(f)) return base;
    g_fontPool[slot].set(f);
    LOG("font: own texture #%d for %d px%s", slot, size, bold ? " bold" : "");
    return f;
}

int activeSceneHandle() { return U.activeScene ? callm<int>(U.activeScene) : 0; }  // Scene is a struct { int handle; }
bool activeSelf(Obj* go) { return go && callm<bool>(U.goActiveSelf, go); }
bool activeInHierarchy(Obj* go) { return alive(go) && U.goActiveHier && callm<bool>(U.goActiveHier, go); }
Obj* goTransform(Obj* go) { return callm<Obj*>(U.goTransform, go); }
void setEuler(Obj* t, Vec3 e) {
    if (U.localEuler) callm<void>(U.localEuler, t, e);
}
bool rectOf(Obj* t, Rect* out) {
    if (!t || !U.getRect || il2cpp_object_get_class(t) != U.RectTransform) return false;
    *out = callm<Rect>(U.getRect, t);
    return true;
}
void setTrigger(Obj* animator, const char* name) {
    if (alive(animator) && U.animTrigger) callm<void>(U.animTrigger, animator, str(name));
}
Obj* findObjectOfType(Il2CppClass* k) {
    if (!U.findObjOfType || !k) return nullptr;
    Obj* o = callm<Obj*>(U.findObjOfType, typeObject(k));
    return alive(o) ? o : nullptr;
}

bool rectInfo(Obj* t, RectInfo* o) {
    if (!t || il2cpp_object_get_class(t) != U.RectTransform) return false;
    o->anchorMin = callm<Vec2>(U.getAnchorMin, t);
    o->anchorMax = callm<Vec2>(U.getAnchorMax, t);
    o->pivot = callm<Vec2>(U.getPivot, t);
    o->pos = callm<Vec2>(U.getAnchoredPos, t);
    o->size = callm<Vec2>(U.getSizeDelta, t);
    return true;
}
Obj* parentOf(Obj* t) { return t && U.trParent ? callm<Obj*>(U.trParent, t) : nullptr; }
bool worldBounds(Obj* t, float* minX, float* maxX, float* minY, float* maxY) {
    if (!alive(t) || il2cpp_object_get_class(t) != U.RectTransform || !U.worldCorners || !U.Vector3) return false;
    Il2CppArray* corners = newArray(U.Vector3, 4);
    callm<void>(U.worldCorners, t, corners);
    Vec3* c = (Vec3*)arrayData(corners);
    *minX = *maxX = c[0].x;
    *minY = *maxY = c[0].y;
    for (int i = 1; i < 4; i++) {
        if (c[i].x < *minX) *minX = c[i].x;
        if (c[i].x > *maxX) *maxX = c[i].x;
        if (c[i].y < *minY) *minY = c[i].y;
        if (c[i].y > *maxY) *maxY = c[i].y;
    }
    return *maxX > *minX;
}
// The camera that draws `t`'s canvas: null for an overlay canvas (world = screen pixels), and for a camera canvas
// without a camera (Unity draws that as an overlay too). false = a world-space canvas with no camera at all.
// the nearest Canvas at or above t, walking up the parents (Component.GetComponentInParent(Type) is stripped
// from this game)
static Obj* canvasAbove(Obj* t) {
    for (int i = 0; alive(t) && i < 64; i++) {
        Obj* c = U.getComp ? callm<Obj*>(U.getComp, gameObject(t), typeObject(U.Canvas)) : nullptr;
        if (alive(c)) return c;
        t = parentOf(t);
    }
    return nullptr;
}

static bool canvasCamera(Obj* t, Obj** cam) {
    *cam = nullptr;
    Obj* cv = canvasAbove(t);
    if (alive(cv) && U.cvRoot) {
        Obj* root = callm<Obj*>(U.cvRoot, cv);
        if (alive(root)) cv = root;
    }
    int mode = alive(cv) && U.cvGetMode ? callm<int>(U.cvGetMode, cv) : 0;
    if (mode == 0) return true;
    Obj* c = U.cvCam ? callm<Obj*>(U.cvCam, cv) : nullptr;
    if (!alive(c) && mode == 2 && U.camMain) c = callm<Obj*>(U.camMain);
    if (!alive(c)) return mode == 1;
    *cam = c;
    return true;
}

Obj* canvasTransform(Obj* t) {
    Obj* cv = canvasAbove(t);
    return alive(cv) ? callm<Obj*>(U.compTransform, cv) : nullptr;
}

bool screenToLocal(Obj* rt, Vec2 screen, Vec2* local) {
    static const MethodInfo* m = nullptr;
    if (!m) m = findMethod(findClass("UnityEngine", "RectTransformUtility"), "ScreenPointToLocalPointInRectangle", 4);
    Obj* cam;
    if (!m || !alive(rt) || !canvasCamera(rt, &cam)) return false;
    return callm<bool>(m, rt, screen, cam, local);
}

void ignoreLayout(Obj* go) {
    static Il2CppClass* LE = findClass("UnityEngine.UI", "LayoutElement");
    static const MethodInfo* set = LE ? findMethod(LE, "set_ignoreLayout", 1) : nullptr;
    if (!set || !alive(go)) return;
    Obj* le = addComponent(go, LE);
    if (le) callm<void>(set, le, true);
}

void setLastSibling(Obj* t) {
    static const MethodInfo* m = findMethod(U.Transform, "SetAsLastSibling", 0);
    if (m && alive(t)) callm<void>(m, t);
}
int siblingIndex(Obj* t) {
    static const MethodInfo* m = findMethod(U.Transform, "GetSiblingIndex", 0);
    return m && alive(t) ? callm<int>(m, t) : -1;
}
void setSiblingIndex(Obj* t, int i) {
    static const MethodInfo* m = findMethod(U.Transform, "SetSiblingIndex", 1);
    if (m && alive(t) && i >= 0) callm<void>(m, t, i);
}

bool screenBounds(Obj* t, float* minX, float* maxX, float* minY, float* maxY) {
    if (!alive(t) || il2cpp_object_get_class(t) != U.RectTransform || !U.worldCorners || !U.Vector3) return false;
    Il2CppArray* corners = newArray(U.Vector3, 4);
    callm<void>(U.worldCorners, t, corners);
    Vec3* c = (Vec3*)arrayData(corners);
    Obj* cam;
    if (!canvasCamera(t, &cam)) return false;
    if (cam && !U.camW2S) return false;
    for (int i = 0; i < 4; i++) {
        Vec3 s = cam ? callm<Vec3>(U.camW2S, cam, c[i]) : c[i];
        if (i == 0 || s.x < *minX) *minX = s.x;
        if (i == 0 || s.x > *maxX) *maxX = s.x;
        if (i == 0 || s.y < *minY) *minY = s.y;
        if (i == 0 || s.y > *maxY) *maxY = s.y;
    }
    return *maxX > *minX;
}
Vec3 worldPos(Obj* t) { return alive(t) && U.getPos ? callm<Vec3>(U.getPos, t) : Vec3{0, 0, 0}; }
void setWorldPos(Obj* t, Vec3 p) {
    if (alive(t) && U.setPosM) callm<void>(U.setPosM, t, p);
}
Vec3 localPos(Obj* t) { return alive(t) && U.getLocalPos ? callm<Vec3>(U.getLocalPos, t) : Vec3{0, 0, 0}; }
void setLocalPos(Obj* t, Vec3 p) {
    if (alive(t) && U.setLocalPosM) callm<void>(U.setLocalPosM, t, p);
}
Il2CppClass* animatorClass() { return U.Animator; }
int childCount(Obj* t) { return alive(t) && U.trChildCount ? callm<int>(U.trChildCount, t) : 0; }
Obj* childAt(Obj* t, int i) { return alive(t) && U.trGetChild ? callm<Obj*>(U.trGetChild, t, i) : nullptr; }
void setSize(Obj* rt, Vec2 s) { callm<void>(U.sizeDelta, rt, s); }

// A vertical scroll area (UGUI ScrollRect): the viewport clips (RectMask2D) and catches drags anywhere on it (a
// clear Image); a drag that starts on a button scrolls instead of clicking it.
static Il2CppClass *SR = nullptr, *Mask = nullptr;
static const MethodInfo *setContent, *setViewport, *setH, *setV, *setMove, *setSens;
static bool g_srLooked = false;

void scrollAreaSetHeight(Obj* content, float contentH) {
    setSize(content, Vec2{0, contentH});
    setPos(content, Vec2{0, 0});
    Obj* vp = parentOf(content);
    Rect r{0, 0, 0, 0};
    Obj* sr = SR && alive(vp) ? getComponent(gameObject(vp), SR) : nullptr;
    if (sr && setV && rectOf(vp, &r)) callm<void>(setV, sr, contentH > r.h + 1);
}

Obj* scrollArea(Obj* parentRT, const char* name, Vec2 pos, Vec2 size, float contentH) {
    if (!g_srLooked) {
        g_srLooked = true;
        SR = findClass("UnityEngine.UI", "ScrollRect");
        Mask = findClass("UnityEngine.UI", "RectMask2D");
        if (SR) {
            setContent = findMethod(SR, "set_content", 1);
            setViewport = findMethod(SR, "set_viewport", 1);
            setH = findMethod(SR, "set_horizontal", 1);
            setV = findMethod(SR, "set_vertical", 1);
            setMove = findMethod(SR, "set_movementType", 1);
            setSens = findMethod(SR, "set_scrollSensitivity", 1);
        }
        LOG("scrollArea: ScrollRect %s, RectMask2D %s", SR ? "found" : "missing", Mask ? "found" : "missing");
    }
    Obj* vp = newUI(name, parentRT);
    place(vp, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, pos, size);
    Obj* content = newUI("content", vp);
    setLayout(content, Vec2{0, 1}, Vec2{1, 1}, Vec2{0.5f, 1});
    setSize(content, Vec2{0, contentH});
    setPos(content, Vec2{0, 0});
    if (!SR || !Mask || !setContent || !setViewport) return content;  // no scrolling, still laid out
    Obj* img = addComponent(gameObject(vp), U.Image);
    if (img) setColor(img, Color{1, 1, 1, 0});
    addComponent(gameObject(vp), Mask);
    Obj* sr = addComponent(gameObject(vp), SR);
    if (!sr) return content;
    callm<void>(setContent, sr, content);
    callm<void>(setViewport, sr, vp);
    if (setH) callm<void>(setH, sr, false);
    if (setV) callm<void>(setV, sr, contentH > size.y + 1);
    if (setMove) callm<void>(setMove, sr, 1);  // Elastic
    if (setSens) callm<void>(setSens, sr, 30.f);
    return content;
}
void setLayout(Obj* rt, Vec2 aMin, Vec2 aMax, Vec2 pivot) {
    callm<void>(U.anchorMin, rt, aMin);
    callm<void>(U.anchorMax, rt, aMax);
    callm<void>(U.pivot, rt, pivot);
}
void setEnabled(Obj* b, bool on) {
    if (alive(b) && U.bhEnabled) callm<void>(U.bhEnabled, b, on);
}

// Touch is a ~68 byte struct returned through x8; give it room to spare.
struct TouchBuf { unsigned char b[160]; };
int touches(TouchInfo* out, int max) {
    if (!U.touchCount || !U.getTouch || U.tPos < 0) return 0;
    int n = callm<int>(U.touchCount);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        TouchBuf t = callm<TouchBuf>(U.getTouch, i);
        out[i].id = *(int*)(t.b + U.tId);
        out[i].pos = *(Vec2*)(t.b + U.tPos);
        out[i].phase = *(int*)(t.b + U.tPhase);
    }
    return n;
}
Vec2 screenSize() { return Vec2{(float)callm<int>(U.scrW), (float)callm<int>(U.scrH)}; }
Vec2 screenToCanvas(Vec2 px) {
    Vec2 s = screenSize();
    float H = canvasH(), k = s.y > 0 ? s.y / H : 1;
    return Vec2{px.x / k - s.x / k / 2, px.y / k - H / 2};
}

float audioTime(Obj* s) { return alive(s) ? callm<float>(U.asGetTime, s) : 0; }
void setAudioTime(Obj* s, float t) {
    if (alive(s)) callm<void>(U.asSetTime, s, t);
}
void setPitch(Obj* s, float p) {
    if (alive(s)) callm<void>(U.asPitch, s, p);
}
bool audioPlaying(Obj* s) { return alive(s) && callm<bool>(U.asPlaying, s); }

Il2CppClass* buttonClass() { return U.Button; }
Il2CppClass* graphicClass() { return U.Graphic; }
Il2CppClass* transformClass() { return U.Transform; }

Obj* getComponent(Obj* go, Il2CppClass* k) {
    if (!alive(go) || !k || !U.getComp) return nullptr;
    Obj* c = callm<Obj*>(U.getComp, go, typeObject(k));
    return alive(c) ? c : nullptr;
}
Il2CppArray* componentsInChildren(Obj* go, Il2CppClass* k) {
    if (!alive(go) || !k || !U.compsInChildren) return nullptr;
    return callm<Il2CppArray*>(U.compsInChildren, go, typeObject(k), true);
}
Obj* instantiate(Obj* original, Obj* parent) {
    if (!alive(original) || !U.instantiate) return nullptr;
    return callm<Obj*>(U.instantiate, original, parent);
}
int objName(Obj* o, char* out, int cap) {
    out[0] = 0;
    if (!alive(o) || !U.objName) return 0;
    return utf8(callm<Il2CppString*>(U.objName, o), out, cap);
}
void rewireButton(Obj* btn, NativeCallback cb, void* user) {
    if (!alive(btn) || !U.ClickEvt || !U.clickEvtCtor || !U.btnSetOnClick) return;
    Obj* evt = il2cpp_object_new(U.ClickEvt);  // fresh event: no persistent (scene) listeners
    callm<void>(U.clickEvtCtor, evt);
    callm<void>(U.btnSetOnClick, btn, evt);
    Obj* action = makeUnityAction(cb, user);
    if (action) callm<void>(U.addListener, evt, action);
}
void ensureEventSystem() {
    if (!U.esCurrent || !U.EventSystem || !U.InputModule) return;
    if (alive(callm<Obj*>(U.esCurrent))) return;
    Obj* go = gameObject(newUI("PhiMeEventSystem", nullptr));
    addComponent(go, U.EventSystem);
    addComponent(go, U.InputModule);
    LOG("no EventSystem in this scene: added one");
}

// One managed delegate fans out to every native per-frame callback.
static NativeCallback g_frameCbs[32];
static int g_frameCbCount = 0;
static Handle g_frameDelegate;
static void frameDispatch(void*) {
    for (int i = 0; i < g_frameCbCount; i++) g_frameCbs[i](nullptr);
}
void onEveryFrame(NativeCallback cb) {
    // callers register on every level / visit: keep one entry per callback, or it runs N times a frame
    bool known = false;
    for (int i = 0; i < g_frameCbCount; i++)
        if (g_frameCbs[i] == cb) known = true;
    if (!known && g_frameCbCount < 32) g_frameCbs[g_frameCbCount++] = cb;
    if (g_frameDelegate.h || !U.WillRender || !U.addWillRender) return;
    Obj* d = makeDelegate(U.WillRender, frameDispatch, nullptr);
    if (!d) return;
    g_frameDelegate.set(d);
    callm<void>(U.addWillRender, d);
    LOG("per-frame callback registered");
}

int frameCount() { return U.frameCount ? callm<int>(U.frameCount) : 0; }
float timeSinceLevelLoad() { return U.levelTime ? callm<float>(U.levelTime) : 0; }
float unscaledDt() { return U.unscaledDt ? callm<float>(U.unscaledDt) : 1.f / 60; }
int prefGetInt(const char* k, int d) { return callm<int>(U.prefGet, str(k), d); }
void prefSetInt(const char* k, int v) { callm<void>(U.prefSet, str(k), v); }
int prefGetString(const char* k, char* out, int cap) {
    out[0] = 0;
    if (!U.prefGetStr) return 0;
    return utf8(callm<Il2CppString*>(U.prefGetStr, str(k), str("")), out, cap);
}
void prefSave() {
    if (U.prefSave) callm<void>(U.prefSave);
}
void loadScene(const char* name) { callm<void>(U.loadScene, str(name)); }

}  // namespace unity
