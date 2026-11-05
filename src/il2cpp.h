// IL2CPP runtime access: resolved by name at runtime so the mod survives game updates.
#pragma once
#include "platform.h"

struct Il2CppObject { void* klass; void* monitor; };
struct Il2CppClass;
struct Il2CppImage;
struct Il2CppType;
struct FieldInfo;
struct Il2CppString;
struct Il2CppArray { Il2CppObject obj; void* bounds; uintptr_t max_length; };

// Leading part of MethodInfo (stable across Unity 2021-2023).
struct MethodInfo {
    void* methodPointer;
    void* virtualMethodPointer;
    void* invoker_method;
    const char* name;
    Il2CppClass* klass;
};
#define PM_METHODINFO_SIZE 0x58

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Color { float r, g, b, a; };
struct Rect { float x, y, w, h; };

namespace il2cpp {

// ---- API function pointers ----
#define PM_API(ret, name, args) extern ret(*name) args;
#include "il2cpp_api.inl"
#undef PM_API

bool resolveApi();
bool ready();  // VM initialised and Assembly-CSharp loaded

Il2CppClass* findClass(const char* ns, const char* name);
Il2CppClass* findNested(Il2CppClass* outer, const char* name);
Il2CppClass* findNestedPrefix(Il2CppClass* outer, const char* prefix);  // compiler-made names, e.g. "<Foo>d__"
// argc < 0 = any. firstParamType: IL2CPP_TYPE_* enum or -1 to ignore.
const MethodInfo* findMethod(Il2CppClass* k, const char* name, int argc, int firstParamType = -1);
// Exact overload by parameter class names (e.g. {"Object", "Transform"}); skips generic definitions.
const MethodInfo* findMethodSig(Il2CppClass* k, const char* name, int argc, const char* const* paramClasses);
int fieldOffset(Il2CppClass* k, const char* name);

// Redirect a method that the engine calls through MethodInfo (Unity messages, coroutines, delegates).
// Works without writing to code pages, so it is fine on non-jailbroken iOS.
void* hookMethod(const MethodInfo* m, void* replacement);

// Call a parameterless method through the VM; a managed exception is caught and logged instead of
// unwinding through native code. Returns false if it threw.
// params: pointers to each argument (value types) or the object pointer itself (reference types).
bool invokeSafe(const MethodInfo* m, void* obj, void** params = nullptr);
// Same, returning what il2cpp_runtime_invoke returns (an object, or a boxed value); *ok = false if it threw.
Il2CppObject* invokeObj(const MethodInfo* m, void* obj, void** params, bool* ok);
// Same, for a method returning a float; false (and *out untouched) if it threw.
bool invokeFloat(const MethodInfo* m, void* obj, float* out);

Il2CppString* str(const char* utf8);
int utf8(Il2CppString* s, char* out, int cap);
Il2CppObject* typeObject(Il2CppClass* k);
Il2CppArray* newArray(Il2CppClass* elem, uintptr_t n);
inline void** arrayData(Il2CppArray* a) { return (void**)((char*)a + sizeof(Il2CppArray)); }

// Managed delegate of a parameterless void delegate type (UnityAction, Canvas.WillRenderCanvases, ...)
// that calls back into native code.
typedef void (*NativeCallback)(void* user);
Il2CppObject* makeDelegate(Il2CppClass* delegateClass, NativeCallback cb, void* user);
Il2CppObject* makeUnityAction(NativeCallback cb, void* user);
// a void delegate with 3 parameters calling fn(target, a, b, c, const MethodInfo*) directly
Il2CppObject* makeDelegate3(Il2CppClass* delegateClass, void* fn);

// Redirect an engine internal call (icall). IL2CPP caches the resolved native function in a writable slot
// that the managed wrapper loads with `adrp; [add;] ldr`. We decode that from the wrapper's code, so this is
// not tied to fixed offsets. Returns the original native function, or null if the pattern isn't found.
void* hookIcall(const MethodInfo* wrapper, const char* icallName, void* replacement);
// virtual / interface dispatch goes through the class vtable: patch k's vtable entries of m (returns the original)
void* hookVirtual(Il2CppClass* k, const MethodInfo* m, void* replacement);

// Keep a managed object alive while native code holds it.
struct Handle {
    uint32_t h = 0;
    void set(void* obj);
    void* get() const;
    void clear();
};

}  // namespace il2cpp

enum { IL2CPP_TYPE_BOOLEAN = 0x02, IL2CPP_TYPE_I4 = 0x08, IL2CPP_TYPE_R4 = 0x0c, IL2CPP_TYPE_STRING = 0x0e,
       IL2CPP_TYPE_CLASS = 0x12, IL2CPP_TYPE_SZARRAY = 0x1d };

// Typed call helpers. Instance methods: pass `self` as the first argument.
template <typename R, typename... A>
inline R callm(const MethodInfo* m, A... a) {
    return ((R(*)(A..., const MethodInfo*))m->methodPointer)(a..., m);
}

template <typename T>
inline T& fld(void* obj, int off) { return *(T*)((char*)obj + off); }
