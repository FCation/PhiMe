// Android only: JNI by function-table slot number (no jni.h), shared by the file picker and the cloud pull.
// The mod's Java classes (android/picker -> one dex) are loaded once by filepicker.cpp.
#pragma once
#include "mod.h"

typedef void* JObj;
union JValue {
    bool z;
    int i;
    long j;
    void* l;
};
struct JNINativeMethod {
    const char* name;
    const char* sig;
    void* fn;
};

// a JNIEnv by function-table slot (jni.h order)
struct J {
    void* e = nullptr;
    template <class F>
    F f(int slot) {
        return (F)(*(void***)e)[slot];
    }
    bool exc() {
        if (!f<unsigned char (*)(void*)>(228)(e)) return false;  // ExceptionCheck
        f<void (*)(void*)>(16)(e);                               // ExceptionDescribe (to logcat)
        f<void (*)(void*)>(17)(e);                               // ExceptionClear
        return true;
    }
    JObj findClass(const char* n) { return f<JObj (*)(void*, const char*)>(6)(e, n); }
    int pushFrame(int n) { return f<int (*)(void*, int)>(19)(e, n); }
    void popFrame() { f<JObj (*)(void*, JObj)>(20)(e, nullptr); }
    JObj globalRef(JObj o) { return f<JObj (*)(void*, JObj)>(21)(e, o); }
    JObj newObject(JObj c, JObj m, const JValue* a) { return f<JObj (*)(void*, JObj, JObj, const JValue*)>(30)(e, c, m, a); }
    JObj method(JObj c, const char* n, const char* s) { return f<JObj (*)(void*, JObj, const char*, const char*)>(33)(e, c, n, s); }
    JObj callObj(JObj o, JObj m, const JValue* a) { return f<JObj (*)(void*, JObj, JObj, const JValue*)>(36)(e, o, m, a); }
    void callVoid(JObj o, JObj m, const JValue* a) { f<void (*)(void*, JObj, JObj, const JValue*)>(63)(e, o, m, a); }
    JObj callStaticObj(JObj c, JObj m, const JValue* a) {
        return f<JObj (*)(void*, JObj, JObj, const JValue*)>(116)(e, c, m, a);
    }
    JObj staticMethod(JObj c, const char* n, const char* s) {
        return f<JObj (*)(void*, JObj, const char*, const char*)>(113)(e, c, n, s);
    }
    void callStaticVoid(JObj c, JObj m, const JValue* a) { f<void (*)(void*, JObj, JObj, const JValue*)>(143)(e, c, m, a); }
    JObj staticField(JObj c, const char* n, const char* s) {
        return f<JObj (*)(void*, JObj, const char*, const char*)>(144)(e, c, n, s);
    }
    JObj getStatic(JObj c, JObj fid) { return f<JObj (*)(void*, JObj, JObj)>(145)(e, c, fid); }
    JObj newString(const char* s) { return f<JObj (*)(void*, const char*)>(167)(e, s); }
    const char* chars(JObj s) { return s ? f<const char* (*)(void*, JObj, unsigned char*)>(169)(e, s, nullptr) : nullptr; }
    void release(JObj s, const char* c) {
        if (s && c) f<void (*)(void*, JObj, const char*)>(170)(e, s, c);
    }
    JObj newArray(int n, JObj c) { return f<JObj (*)(void*, int, JObj, JObj)>(172)(e, n, c, nullptr); }
    void setElement(JObj a, int i, JObj v) { f<void (*)(void*, JObj, int, JObj)>(174)(e, a, i, v); }
    int registerNatives(JObj c, const JNINativeMethod* m, int n) {
        return f<int (*)(void*, JObj, const JNINativeMethod*, int)>(215)(e, c, m, n);
    }
    JObj directBuffer(void* p, long n) { return f<JObj (*)(void*, void*, long)>(229)(e, p, n); }
};

bool jniEnv(J& j);                          // this thread's JNIEnv (attaches it when needed)
JObj jniActivity(J& j);                     // UnityPlayer.currentActivity (a local ref)
JObj jniModClass(J& j, const char* name);   // a class of the mod's dex ("phime.Cloud"), a global ref; null = none
