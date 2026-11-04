// Thin wrappers over UnityEngine / UnityEngine.UI, called through IL2CPP. Main thread only.
#pragma once
#include "il2cpp.h"

typedef Il2CppObject Obj;

namespace unity {

bool init();  // resolve classes and methods (safe off the main thread)

// lifetime
bool alive(Obj* unityObject);
void destroy(Obj* unityObject);

// objects / transforms
Obj* newUI(const char* name, Obj* parentRT);  // GameObject with a RectTransform, returns the RectTransform
Obj* gameObject(Obj* component);
Obj* rt(Obj* component);  // its (Rect)Transform
Obj* addComponent(Obj* go, Il2CppClass* k);
void setActive(Obj* go, bool on);
void place(Obj* rt, Vec2 anchor, Vec2 pivot, Vec2 pos, Vec2 size);
void stretch(Obj* rt, float inset = 0);
void setPos(Obj* rt, Vec2 pos);
void setScale(Obj* rt, float s);

// visuals
enum SpriteKind { SPR_ROUND, SPR_CIRCLE, SPR_CLAWD, SPR_COUNT, SPR_SOLID = SPR_COUNT /* plain rect, no sprite */ };
// Clawd is drawn at a 128x72 texture (aspect 16:9); keep that ratio when sizing it.
Obj* image(Obj* parentRT, const char* name, SpriteKind kind, Color c, Vec2 anchor, Vec2 pos, Vec2 size,
           float radius = 0, bool raycast = false);
void setRadius(Obj* roundImage, float radius);  // corner radius of an SPR_ROUND image (keep it <= half its height)
void setColor(Obj* graphic, Color c);
Color getColor(Obj* graphic);
void setRaycast(Obj* graphic, bool on);
Obj* text(Obj* parentRT, const char* s, int size, Color c, int align, bool bold, Vec2 anchor, Vec2 pivot, Vec2 pos,
          Vec2 box);
void setText(Obj* textComp, const char* s);
void setTextSize(Obj* textComp, int size);
void setTextWrap(Obj* textComp, bool wrap);           // wrap at the Text's width (default: one line, shrunk to fit)
float textWidth(const char* s, int size, bool bold);  // how wide the mod draws s at that size, in the same units
float textHeight(const char* s, int size, bool bold, float wrapW);  // how tall, wrapped at wrapW (same units)
void retext(Obj* textComp, const char* s, int size, bool bold);  // text + font again (language switch)
void setTextStr(Obj* textComp, Il2CppString* s);  // no allocation: reuse a cached string
// readback of a game Text (null / 0 when missing)
Il2CppString* textStr(Obj* textComp);
Obj* textFont(Obj* textComp);
void setTextFont(Obj* textComp, Obj* font);
int textFontSize(Obj* textComp);
int textAlignment(Obj* textComp);
float textPreferredHeight(Obj* textComp);
float textPreferredWidth(Obj* textComp);
// CanvasGroup: fade a whole subtree
Obj* addCanvasGroup(Obj* go);
void setGroupAlpha(Obj* canvasGroup, float a);
void setIgnoreParentGroups(Obj* canvasGroup, bool on);
void setBlocksRaycasts(Obj* canvasGroup, bool on);
// Make a piece of game UI invisible and click-through without deactivating it (its animations keep running)
void hideUi(Obj* go);
void shadow(Obj* graphic, Color c, Vec2 dist);
void onClick(Obj* go, Obj* targetGraphic, il2cpp::NativeCallback cb, void* user);

// our own pixels on screen: a RawImage showing an RGBA32 texture (texture + pixel array reused while the size stays)
Obj* rawImage(Obj* parentRT, const char* name, Vec2 anchor, Vec2 pos, Vec2 size);  // null if unavailable
struct Pixels {
    il2cpp::Handle tex, arr;
    int w = 0, h = 0;
};
uint8_t* pixelsBegin(Pixels& p, int w, int h);  // w*h RGBA to fill (bottom row first); null on failure
void pixelsEnd(Pixels& p, Obj* raw);             // upload, and show on the RawImage
void pixelsFree(Pixels& p);                      // destroys the texture

// canvas. Our overlay canvases are 1080 units tall on tablets; on phones everything is drawn uiScale() times bigger
// (the canvas is 1080 / uiScale() tall). needH > 0 = the content needs that many 1080-units of height: the scale is
// lowered so it still fits.
Obj* overlayCanvas(const char* name, int sortingOrder, float needH = 0);  // returns root RectTransform
bool isPhone();       // a phone-sized screen (the dpi, else the aspect ratio)
float uiScale();      // 1 on tablets, bigger on phones
float canvasH();      // height of a default overlay canvas in its own units (1080 / uiScale())
// Our overlay canvases fade in when made. Closing one: uiFadeAway (fades out, then destroys it).
void uiFadeAway(Obj* overlayRoot);
void uiFadeOnClone(Obj* src, Obj* clone);  // the Instantiate hook: puts the game's scene wipe above our canvases
float uiAlphaOf(Obj* transform);           // how visible a piece of game UI is (0 when inactive)

// scene / objects
int activeSceneHandle();
bool activeSelf(Obj* go);
bool activeInHierarchy(Obj* go);  // false for null / destroyed
Obj* goTransform(Obj* go);
void setEuler(Obj* transform, Vec3 e);
bool rectOf(Obj* transform, Rect* out);  // false when it is not a RectTransform
void setTrigger(Obj* animator, const char* name);
Obj* findObjectOfType(Il2CppClass* k);

// layout readback
struct RectInfo { Vec2 anchorMin, anchorMax, pivot, pos, size; };
bool rectInfo(Obj* rectTransform, RectInfo* out);
Obj* parentOf(Obj* transform);
// world space
bool worldBounds(Obj* rectTransform, float* minX, float* maxX, float* minY, float* maxY);
Vec3 worldPos(Obj* transform);
void setWorldPos(Obj* transform, Vec3 p);
Vec3 localPos(Obj* transform);
void setLocalPos(Obj* transform, Vec3 p);
Il2CppClass* animatorClass();
int childCount(Obj* transform);
Obj* childAt(Obj* transform, int i);
void setSize(Obj* rt, Vec2 size);
// vertical scroll area at pos / size (centre-anchored in parentRT): returns its content, anchored to the top,
// contentH tall (scrolls only when taller than size.y). The viewport is the content's parent.
Obj* scrollArea(Obj* parentRT, const char* name, Vec2 pos, Vec2 size, float contentH);
void scrollAreaSetHeight(Obj* content, float contentH);  // after filling it: its real height
void setLayout(Obj* rt, Vec2 anchorMin, Vec2 anchorMax, Vec2 pivot);
void setEnabled(Obj* behaviour, bool on);

// input: touches in screen pixels (origin bottom-left)
struct TouchInfo { int id; Vec2 pos; int phase; };  // phase: 0 began 1 moved 2 stationary 3 ended 4 canceled
int touches(TouchInfo* out, int max);
Vec2 screenSize();
// screen pixels -> units of a default overlay canvas (canvasH() tall, origin at the centre)
Vec2 screenToCanvas(Vec2 px);

// audio (UnityEngine.AudioSource)
float audioTime(Obj* src);
void setAudioTime(Obj* src, float t);
void setPitch(Obj* src, float p);
bool audioPlaying(Obj* src);

// components / cloning
Il2CppClass* buttonClass();
Il2CppClass* graphicClass();
Il2CppClass* transformClass();
Obj* getComponent(Obj* go, Il2CppClass* k);
Il2CppArray* componentsInChildren(Obj* go, Il2CppClass* k);  // includes inactive
Obj* instantiate(Obj* original, Obj* parentTransform);
Obj* starTexture();  // 64 px white four-pointed star (Texture2D), made once
int objName(Obj* unityObject, char* out, int cap);
// Point a Button at our callback only (drops listeners baked into the scene, e.g. on a cloned button)
void rewireButton(Obj* button, il2cpp::NativeCallback cb, void* user);
// UGUI needs an EventSystem to receive taps; some scenes (the level) may not have one
void ensureEventSystem();

// global per-frame callback (Canvas.willRenderCanvases: after animation, before rendering)
void onEveryFrame(il2cpp::NativeCallback cb);

// misc
Obj* findFont();
Obj* fontFor(int size, bool bold);  // findFont, spread over a few instances (one glyph texture each)
void rememberFont(Obj* font);
void setFontLanguage(bool english);  // English: serif font chain; switching drops the cached fonts
bool fontLanguageEn();
// a RectTransform's rect in screen pixels (origin bottom-left), whatever kind of canvas it is on
bool screenBounds(Obj* rectTransform, float* minX, float* maxX, float* minY, float* maxY);
bool screenToLocal(Obj* rectTransform, Vec2 screenPx, Vec2* local);  // relative to its pivot
Obj* canvasTransform(Obj* t);  // the transform of the nearest Canvas above t
void ignoreLayout(Obj* go);    // a LayoutElement with ignoreLayout: layout groups leave it alone
void setLastSibling(Obj* t);
int siblingIndex(Obj* t);                // -1 if unknown
void setSiblingIndex(Obj* t, int i);     // drawn after the siblings before index i, under the rest
float unscaledDt();
int frameCount();
float timeSinceLevelLoad();
int prefGetInt(const char* key, int def);
void prefSetInt(const char* key, int v);
void prefSave();
int prefGetString(const char* key, char* out, int cap);  // "" when missing
void loadScene(const char* name);

// TextAnchor values
enum { UpperLeft = 0, UpperCenter = 1, MiddleLeft = 3, MiddleCenter = 4, MiddleRight = 5 };

inline Color rgb(int hex, float a = 1.f) {
    return Color{((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a};
}

}  // namespace unity
