#include "il2cpp.h"

namespace il2cpp {

#define PM_API(ret, name, args) ret(*name) args = nullptr;
#include "il2cpp_api.inl"
#undef PM_API

static void* libHandle() {
#if defined(__APPLE__)
    return PM_RTLD_DEFAULT;  // UnityFramework is already loaded into the process
#else
    return dlopen("libil2cpp.so", PM_RTLD_NOLOAD | PM_RTLD_NOW);
#endif
}

bool resolveApi() {
    void* h = libHandle();
    if (!h) return false;
    bool ok = true;
#define PM_API(ret, name, args)                              \
    name = (ret(*) args)dlsym(h, #name);                     \
    if (!name) { ok = false; }
#include "il2cpp_api.inl"
#undef PM_API
    return ok;
}

bool ready() {
    if (!il2cpp_get_corlib || !il2cpp_get_corlib()) return false;
    size_t n = 0;
    void** asms = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        const char* nm = il2cpp_image_get_name(il2cpp_assembly_get_image(asms[i]));
        if (nm && strcmp(nm, "Assembly-CSharp.dll") == 0) return true;
    }
    return false;
}

Il2CppClass* findClass(const char* ns, const char* name) {
    size_t n = 0;
    void** asms = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        Il2CppClass* k = il2cpp_class_from_name(il2cpp_assembly_get_image(asms[i]), ns, name);
        if (k) return k;
    }
    LOG("class not found: %s.%s", ns, name);
    return nullptr;
}

Il2CppClass* findNested(Il2CppClass* outer, const char* name) {
    if (!outer) return nullptr;
    void* it = nullptr;
    while (Il2CppClass* k = il2cpp_class_get_nested_types(outer, &it)) {
        if (strcmp(il2cpp_class_get_name(k), name) == 0) return k;
    }
    LOG("nested class not found: %s", name);
    return nullptr;
}

Il2CppClass* findNestedPrefix(Il2CppClass* outer, const char* prefix) {
    if (!outer) return nullptr;
    size_t n = strlen(prefix);
    void* it = nullptr;
    while (Il2CppClass* k = il2cpp_class_get_nested_types(outer, &it)) {
        if (memcmp(il2cpp_class_get_name(k), prefix, n) == 0) return k;
    }
    LOG("nested class not found: %s*", prefix);
    return nullptr;
}

const MethodInfo* findMethod(Il2CppClass* k, const char* name, int argc, int firstParamType) {
    // most-derived class first, so overrides win
    for (Il2CppClass* c = k; c; c = il2cpp_class_get_parent(c)) {
        void* it = nullptr;
        while (const MethodInfo* m = il2cpp_class_get_methods(c, &it)) {
            if (strcmp(il2cpp_method_get_name(m), name) != 0) continue;
            uint32_t pc = il2cpp_method_get_param_count(m);
            if (argc >= 0 && (int)pc != argc) continue;
            if (firstParamType >= 0 && (pc == 0 || il2cpp_type_get_type(il2cpp_method_get_param(m, 0)) != firstParamType))
                continue;
            return m;
        }
    }
    LOG("method not found: %s.%s/%d", k ? il2cpp_class_get_name(k) : "?", name, argc);
    return nullptr;
}

const MethodInfo* findMethodSig(Il2CppClass* k, const char* name, int argc, const char* const* paramClasses) {
    for (Il2CppClass* c = k; c; c = il2cpp_class_get_parent(c)) {
        void* it = nullptr;
        while (const MethodInfo* m = il2cpp_class_get_methods(c, &it)) {
            if (!m->methodPointer || strcmp(il2cpp_method_get_name(m), name) != 0) continue;
            if ((int)il2cpp_method_get_param_count(m) != argc) continue;
            bool ok = true;
            for (int i = 0; i < argc && ok; i++) {
                Il2CppClass* pc = il2cpp_class_from_type(il2cpp_method_get_param(m, (uint32_t)i));
                ok = pc && strcmp(il2cpp_class_get_name(pc), paramClasses[i]) == 0;
            }
            if (ok) return m;
        }
    }
    LOG("method not found: %s.%s(%d, exact)", k ? il2cpp_class_get_name(k) : "?", name, argc);
    return nullptr;
}

int fieldOffset(Il2CppClass* k, const char* name) {
    FieldInfo* f = k ? il2cpp_class_get_field_from_name(k, name) : nullptr;
    if (!f) {
        LOG("field not found: %s.%s", k ? il2cpp_class_get_name(k) : "?", name);
        return -1;
    }
    return (int)il2cpp_field_get_offset(f);
}

void* hookMethod(const MethodInfo* cm, void* repl) {
    if (!cm) return nullptr;
    MethodInfo* m = (MethodInfo*)cm;
    void* orig = m->methodPointer;
    m->methodPointer = repl;
    m->virtualMethodPointer = repl;
    uint32_t iflags = 0;
    if (il2cpp_method_get_flags(m, &iflags) & 0x0040) {  // virtual: also patch the class vtable slot
        void** p = (void**)m->klass;
        for (int i = 0x100 / 8; i < 0x400 / 8; i += 1) {
            if (p[i] == orig && p[i + 1] == (void*)m) p[i] = repl;
        }
    }
    LOG("hooked %s.%s", il2cpp_class_get_name(m->klass), m->name);
    return orig;
}

bool invokeSafe(const MethodInfo* m, void* obj, void** params) {
    if (!m) return false;
    Il2CppObject* exc = nullptr;
    il2cpp_runtime_invoke(m, obj, params, &exc);
    if (exc) {
        LOG("%s threw %s", m->name, il2cpp_class_get_name(il2cpp_object_get_class(exc)));
        return false;
    }
    return true;
}

Il2CppObject* invokeObj(const MethodInfo* m, void* obj, void** params, bool* ok) {
    *ok = false;
    if (!m) return nullptr;
    Il2CppObject* exc = nullptr;
    Il2CppObject* r = il2cpp_runtime_invoke(m, obj, params, &exc);
    if (exc) {
        LOG("%s threw %s", m->name, il2cpp_class_get_name(il2cpp_object_get_class(exc)));
        return nullptr;
    }
    *ok = true;
    return r;
}

bool invokeFloat(const MethodInfo* m, void* obj, float* out) {
    if (!m) return false;
    Il2CppObject* exc = nullptr;
    Il2CppObject* boxed = il2cpp_runtime_invoke(m, obj, nullptr, &exc);
    if (exc || !boxed) {
        if (exc) LOG("%s threw %s", m->name, il2cpp_class_get_name(il2cpp_object_get_class(exc)));
        return false;
    }
    *out = *(float*)il2cpp_object_unbox(boxed);
    return true;
}

Il2CppString* str(const char* s) { return il2cpp_string_new(s); }

int utf8(Il2CppString* s, char* out, int cap) {
    int n = 0;
    if (s) {
        int len = il2cpp_string_length(s);
        uint16_t* c = il2cpp_string_chars(s);
        for (int i = 0; i < len && n < cap - 4; i++) {
            uint32_t u = c[i];
            if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) u = 0x10000 + ((u - 0xD800) << 10) + (c[++i] - 0xDC00);
            if (u < 0x80) out[n++] = (char)u;
            else if (u < 0x800) { out[n++] = 0xC0 | (u >> 6); out[n++] = 0x80 | (u & 63); }
            else if (u < 0x10000) { out[n++] = 0xE0 | (u >> 12); out[n++] = 0x80 | ((u >> 6) & 63); out[n++] = 0x80 | (u & 63); }
            else { out[n++] = 0xF0 | (u >> 18); out[n++] = 0x80 | ((u >> 12) & 63); out[n++] = 0x80 | ((u >> 6) & 63); out[n++] = 0x80 | (u & 63); }
        }
    }
    out[n] = 0;
    return n;
}

Il2CppObject* typeObject(Il2CppClass* k) { return il2cpp_type_get_object(il2cpp_class_get_type(k)); }

Il2CppArray* newArray(Il2CppClass* elem, uintptr_t n) { return il2cpp_array_new(elem, n); }

// ---- native-backed UnityAction ----
struct CbSlot { NativeCallback cb; void* user; };

static void actionTrampoline(Il2CppObject* /*target*/, const MethodInfo* mi) {
    CbSlot* s = (CbSlot*)((char*)mi + PM_METHODINFO_SIZE);
    if (s->cb) s->cb(s->user);
}

Il2CppObject* makeUnityAction(NativeCallback cb, void* user) {
    static Il2CppClass* actionClass = nullptr;
    if (!actionClass) actionClass = findClass("UnityEngine.Events", "UnityAction");
    return makeDelegate(actionClass, cb, user);
}

Il2CppObject* makeDelegate(Il2CppClass* delegateClass, NativeCallback cb, void* user) {
    static const MethodInfo* tmpl = nullptr;
    // any instance, non-virtual, parameterless void method works as a template
    if (!tmpl) tmpl = findMethod(findClass("UnityEngine", "MonoBehaviour"), "CancelInvoke", 0);
    const MethodInfo* ctor = delegateClass ? findMethod(delegateClass, ".ctor", 2) : nullptr;
    if (!ctor || !tmpl) return nullptr;
    Il2CppClass* actionClass = delegateClass;
    char* mem = (char*)calloc(1, PM_METHODINFO_SIZE + sizeof(CbSlot));  // intentionally never freed
    memcpy(mem, tmpl, PM_METHODINFO_SIZE);
    MethodInfo* fake = (MethodInfo*)mem;
    fake->methodPointer = (void*)actionTrampoline;
    fake->virtualMethodPointer = (void*)actionTrampoline;
    CbSlot* slot = (CbSlot*)(mem + PM_METHODINFO_SIZE);
    slot->cb = cb;
    slot->user = user;
    Il2CppObject* del = il2cpp_object_new(actionClass);
    Il2CppObject* target = (Il2CppObject*)il2cpp_string_new("PhiMeMod");
    callm<void>(ctor, del, target, (intptr_t)fake);
    return del;
}

// Find the icall cache slot in a wrapper like:
//   adrp x19, page ; [add x19, x19, #lo] ; ldr x2, [x19, #off] ; cbz x2, resolve ; br x2
static void** decodeIcallSlot(const void* fn) {
    const uint32_t* p = (const uint32_t*)fn;
    uintptr_t regs[32];
    bool have[32] = {};
    for (int i = 0; i < 24; i++) {
        uint32_t ins = p[i];
        uintptr_t pc = (uintptr_t)&p[i];
        if ((ins & 0x9F000000u) == 0x90000000u) {  // adrp
            int rd = ins & 31;
            long imm = (long)((((ins >> 5) & 0x7FFFFu) << 2) | ((ins >> 29) & 3u));
            if (imm & (1L << 20)) imm -= 1L << 21;
            regs[rd] = (pc & ~(uintptr_t)0xFFF) + (uintptr_t)(imm << 12);
            have[rd] = true;
        } else if ((ins & 0xFF800000u) == 0x91000000u) {  // add xd, xn, #imm
            int rd = ins & 31, rn = (ins >> 5) & 31;
            if (have[rn]) {
                regs[rd] = regs[rn] + ((uintptr_t)((ins >> 10) & 0xFFF) << ((ins & (1u << 22)) ? 12 : 0));
                have[rd] = true;
            }
        } else if ((ins & 0xFFC00000u) == 0xF9400000u) {  // ldr xt, [xn, #imm]
            int rn = (ins >> 5) & 31;
            if (have[rn]) return (void**)(regs[rn] + ((ins >> 10) & 0xFFF) * 8);
        } else if (ins == 0xD65F03C0u) {  // ret
            break;
        }
    }
    return nullptr;
}

void* hookIcall(const MethodInfo* wrapper, const char* icallName, void* repl) {
    if (!wrapper || !wrapper->methodPointer) return nullptr;
    void** slot = decodeIcallSlot(wrapper->methodPointer);
    void* native = il2cpp_resolve_icall(icallName);
    if (!slot || !native) {
        LOG("icall hook failed: %s (slot=%p native=%p)", icallName, (void*)slot, native);
        return nullptr;
    }
    void* cur = *slot;
    if (cur && cur != native) {  // not the slot we expected: leave it alone
        LOG("icall hook skipped: %s (slot holds %p, expected %p)", icallName, cur, native);
        return nullptr;
    }
    *slot = repl;
    LOG("hooked icall %s", icallName);
    return native;
}

// Virtual / interface calls read the method pointer from the class's vtable (VirtualInvokeData {methodPtr,
// method}), not from the MethodInfo, so hookMethod misses them. The vtable is in the runtime-allocated class (heap,
// writable on iOS too): every entry of `m` in k's vtable gets `repl`. Returns the original pointer, or null.
void* hookVirtual(Il2CppClass* k, const MethodInfo* m, void* repl) {
    if (!k || !m || !m->methodPointer) return nullptr;
    il2cpp_object_new(k);  // makes sure the class is initialised (vtable filled)
    void* orig = (void*)m->methodPointer;
    uintptr_t* p = (uintptr_t*)k;
    int patched = 0;
    for (int i = 0; i < 0x2000 / 8 - 1; i++)
        if (p[i] == (uintptr_t)orig && p[i + 1] == (uintptr_t)m) {
            p[i] = (uintptr_t)repl;
            patched++;
        }
    LOG("hooked virtual %s.%s (%d vtable entries)", il2cpp_class_get_name(k), m->name, patched);
    return patched ? orig : nullptr;
}

void Handle::set(void* obj) {
    clear();
    if (obj) h = il2cpp_gchandle_new((Il2CppObject*)obj, false);
}
void* Handle::get() const { return h ? il2cpp_gchandle_get_target(h) : nullptr; }
void Handle::clear() {
    if (h) il2cpp_gchandle_free(h);
    h = 0;
}

}  // namespace il2cpp
