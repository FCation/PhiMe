// Unity's own log (Debug.Log*, and exceptions nobody caught, e.g. inside the game's async cloud sync) copied into
// PhiMeMod.log, so a hang or failure in the game's code shows up in what users send.
// Application.logMessageReceivedThreaded: called on whatever thread logged, so nothing here touches Unity.
#include "mod.h"

using namespace il2cpp;

static volatile int g_plain = 0, g_errors = 0;

// LogCallback(string condition, string stackTrace, LogType type); LogType: 0 Error, 1 Assert, 2 Warning, 3 Log, 4 Exception
static void onUnityLog(Il2CppObject*, Il2CppString* condition, Il2CppString* stack, int type, const MethodInfo*) {
    bool bad = type == 0 || type == 1 || type == 4;
    if (bad ? g_errors++ >= 200 : g_plain++ >= 150) return;  // a game that logs every frame must not flood the file
    char c[400] = "", s[520] = "";
    if (condition) utf8(condition, c, sizeof c);
    if (bad && stack) utf8(stack, s, sizeof s);
    for (char* p = s; *p; p++)
        if (*p == '\n') *p = '|';  // the stack on one line
    static const char* const kinds[] = {"error", "assert", "warning", "log", "EXCEPTION"};
    const char* k = type >= 0 && type <= 4 ? kinds[type] : "?";
    if (s[0]) LOG("unity %s: %s  @ %s", k, c, s);
    else LOG("unity %s: %s", k, c);
}

// The game build strips Application.logMessageReceived*, so the default log handler's own icalls are hooked
// instead: DebugLogHandler.Internal_Log (Debug.Log*) and Internal_LogException (Debug.LogException, which UniTask
// uses for exceptions nobody awaited)
typedef void (*LogFn)(int, int, Il2CppString*, Il2CppObject*);
typedef void (*LogExFn)(Il2CppObject*, Il2CppObject*);
static LogFn o_log;
static LogExFn o_logEx;
static const MethodInfo* g_exToString;
static void h_log(int level, int options, Il2CppString* msg, Il2CppObject* obj) {
    onUnityLog(nullptr, msg, nullptr, level, nullptr);
    o_log(level, options, msg, obj);
}
static void h_logEx(Il2CppObject* ex, Il2CppObject* obj) {
    bool ok = false;
    Il2CppString* s = ex && g_exToString ? (Il2CppString*)invokeObj(g_exToString, ex, nullptr, &ok) : nullptr;
    if (ok && s) {
        char b[700] = "";
        utf8(s, b, sizeof b);
        for (char* p = b; *p; p++)
            if (*p == '\n') *p = '|';
        if (g_errors++ < 200) LOG("unity EXCEPTION: %s", b);
    } else if (ex) {
        if (g_errors++ < 200) LOG("unity EXCEPTION: %s", il2cpp_class_get_name(il2cpp_object_get_class(ex)));
    }
    o_logEx(ex, obj);
}

bool unityLogInstall() {
    Il2CppClass* App = findClass("UnityEngine", "Application");
    Il2CppClass* CB = App ? findNested(App, "LogCallback") : nullptr;
    const MethodInfo* add = App ? findMethod(App, "add_logMessageReceivedThreaded", 1) : nullptr;
    if (!add && App) add = findMethod(App, "add_logMessageReceived", 1);
    Il2CppObject* del = CB && add ? makeDelegate3(CB, (void*)onUnityLog) : nullptr;
    bool ok = false;
    if (del) {
        void* args[] = {del};
        ok = invokeSafe(add, nullptr, args);
    }
    if (!ok) {
        Il2CppClass* H = findClass("UnityEngine", "DebugLogHandler");
        Il2CppClass* Ex = findClass("System", "Exception");
        g_exToString = Ex ? findMethod(Ex, "ToString", 0) : nullptr;
        if (H) {
            o_log = (LogFn)hookIcall(findMethod(H, "Internal_Log", 4),
                                     "UnityEngine.DebugLogHandler::Internal_Log(UnityEngine.LogType,UnityEngine.LogOption,"
                                     "System.String,UnityEngine.Object)", (void*)h_log);
            o_logEx = (LogExFn)hookIcall(findMethod(H, "Internal_LogException", 2),
                                         "UnityEngine.DebugLogHandler::Internal_LogException(System.Exception,"
                                         "UnityEngine.Object)", (void*)h_logEx);
        }
        ok = o_log || o_logEx;
        LOG("unitylog: log handler icalls: Internal_Log %s, Internal_LogException %s", o_log ? "on" : "off",
            o_logEx ? "on" : "off");
    }
    LOG("unitylog: Unity's log %s", ok ? "copied into this log" : "not available");
    return ok;
}
