/* OHOS IME boundary using public C APIs; no hand-declared C++ vtables.
 * Loaded by OhImeBridge only in a forked Android app on its first IME request.
 * Edits are marshalled by Java to the Android UI thread's InputConnection.
 */
#include <jni.h>
#include <inputmethod/inputmethod_controller_capi.h>
#include <window_manager/oh_window.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <dlfcn.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
JavaVM* vm;
jclass bridge;
std::mutex mutex;
std::mutex callbackMutex; // Never held across OH IPC or a Java callback.
InputMethod_TextEditorProxy* editor;
InputMethod_InputMethodProxy* ime;
InputMethod_TextEditorProxy* callbackEditor;
jlong callbackGeneration;
int32_t callbackWindow;
std::atomic<bool> shown{false};

jlong generation(InputMethod_TextEditorProxy* source) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    return source == callbackEditor ? callbackGeneration : 0;
}
void identity(InputMethod_TextEditorProxy* source, jlong value, int32_t window) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackEditor = source; callbackGeneration = value; callbackWindow = window;
}

struct Env {
    JNIEnv* env = nullptr;
    bool attached = false;
    Env() {
        if (vm && vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            JavaVMAttachArgs args{JNI_VERSION_1_6, const_cast<char*>("oh-ime-capi"), nullptr};
            attached = vm->AttachCurrentThread(&env, &args) == JNI_OK;
            if (!attached) env = nullptr;
        }
    }
    ~Env() { if (attached) vm->DetachCurrentThread(); }
};

int32_t editedWindow() {
    std::lock_guard<std::mutex> lock(callbackMutex);
    return callbackWindow;
}
void active(bool value) {
    shown.store(value);
    auto set = reinterpret_cast<void (*)(int)>(dlsym(RTLD_DEFAULT, "oh_wm_set_ime_active"));
    if (set) set(value ? 1 : 0);
}

void exception(JNIEnv* env) {
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
}
// Android resizes or pans the edited window for the keyboard from the IME insets WMS sends it.
// OH reports occupied-area changes only through the session stage, which SceneBoard does not call
// for these windows, so once the panel is shown its avoid area over the edited window is read and
// handed to Java, which publishes it as the IME insets source. An empty area means it is gone.
void reportKeyboardArea(int32_t window, bool show) {
    if (window <= 0) return;
    std::thread([window, show]() {
        WindowManager_AvoidArea area{};
        bool found = false;
        for (int attempt = 0; show && attempt < 20 && !found; ++attempt) {
            usleep(50 * 1000);  // the panel is laid out after the SHOW status arrives
            found = OH_WindowManager_GetWindowAvoidArea(
                        window, WINDOW_MANAGER_AVOID_AREA_TYPE_KEYBOARD, &area) == 0
                    && area.bottomRect.height > 0;
        }
        // Not found: OH delivers the area through the session stage as well; a failed query must
        // not overwrite it with an empty one. Only a hidden keyboard reports an empty area.
        if (show && !found) {
            fprintf(stderr, "[OH_IME_CAPI] keyboard area window=%d not available\n", window);
            return;
        }
        const WindowManager_Rect rect = found ? area.bottomRect : WindowManager_Rect{0, 0, 0, 0};
        Env e;
        if (!e.env || !bridge) return;
        jmethodID id = e.env->GetStaticMethodID(bridge, "nativeOnKeyboardArea", "(IIIII)V");
        if (id) {
            e.env->CallStaticVoidMethod(bridge, id, window, rect.posX, rect.posY,
                                        static_cast<jint>(rect.width), static_cast<jint>(rect.height));
        }
        exception(e.env);
        fprintf(stderr, "[OH_IME_CAPI] keyboard area window=%d show=%d rect=%d,%d %ux%u\n",
                window, show ? 1 : 0, rect.posX, rect.posY, rect.width, rect.height);
    }).detach();
}

int getInt(const char* method) {
    Env e;
    if (!e.env || !bridge) return 0;
    jmethodID id = e.env->GetStaticMethodID(bridge, method, "()I");
    int value = id ? e.env->CallStaticIntMethod(bridge, id) : 0;
    exception(e.env);
    return value;
}

bool operation(InputMethod_TextEditorProxy* source, int op, int arg = 0,
               const char16_t* text = nullptr, size_t length = 0, int end = -1) {
    const jlong epoch = generation(source);
    if (!epoch || (!text && length) || length > 8192) return false;
    Env e;
    if (!e.env || !bridge) return false;
    jstring value = text ? e.env->NewString(reinterpret_cast<const jchar*>(text), length) : nullptr;
    jmethodID id = e.env->GetStaticMethodID(bridge, "nativeOnOperation", "(JIILjava/lang/String;I)V");
    if (id && !e.env->ExceptionCheck()) e.env->CallStaticVoidMethod(bridge, id, epoch, op, arg, value, end);
    const bool ok = id && !e.env->ExceptionCheck();
    exception(e.env);
    if (value) e.env->DeleteLocalRef(value);
    return ok;
}

void config(InputMethod_TextEditorProxy* source, InputMethod_TextConfig* config) {
    int32_t windowId;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (source != callbackEditor || !callbackGeneration) return;
        windowId = callbackWindow;
    }
    const int type = getInt("nativeGetInputType");
    const int variation = type & 0xff0;
    InputMethod_TextInputType mapped = IME_TEXT_INPUT_TYPE_TEXT;
    switch (type & 0xf) {
        case 2: mapped = variation == 0x10 ? IME_TEXT_INPUT_TYPE_NUMBER_PASSWORD
                : (type & 0x2000) ? IME_TEXT_INPUT_TYPE_NUMBER_DECIMAL : IME_TEXT_INPUT_TYPE_NUMBER; break;
        case 3: mapped = IME_TEXT_INPUT_TYPE_PHONE; break;
        case 4: mapped = IME_TEXT_INPUT_TYPE_DATETIME; break;
        default:
            if (variation == 0x80 || variation == 0xe0) mapped = IME_TEXT_INPUT_TYPE_NEW_PASSWORD;
            else if (variation == 0x90) mapped = IME_TEXT_INPUT_TYPE_VISIBLE_PASSWORD;
            else if (variation == 0x20 || variation == 0xd0) mapped = IME_TEXT_INPUT_TYPE_EMAIL_ADDRESS;
            else if (variation == 0x10) mapped = IME_TEXT_INPUT_TYPE_URL;
            else if (type & 0x20000) mapped = IME_TEXT_INPUT_TYPE_MULTILINE;
    }
    OH_TextConfig_SetInputType(config, mapped);
    // Android GO/SEARCH/SEND/NEXT/DONE/PREVIOUS values match the OH enum.
    int action = getInt("nativeGetImeOptions") & 0xff;
    if (action > 7) action = 0;
    OH_TextConfig_SetEnterKeyType(config, static_cast<InputMethod_EnterKeyType>(action));
    OH_TextConfig_SetPreviewTextSupport(config, true);
    const int cursor = getInt("nativeGetCursor");
    OH_TextConfig_SetSelection(config, cursor, getInt("nativeGetSelectionEnd"));
    // This is the app's registered child session, not the host's main window.
    // IMSA verifies ownership/focus; never borrow another process's window ID.
    if (windowId > 0) OH_TextConfig_SetWindowId(config, windowId);
}

void insert(InputMethod_TextEditorProxy* source, const char16_t* text, size_t length) {
    operation(source, 0, 0, text ? text : u"", length);
}

void before(InputMethod_TextEditorProxy* source, int length) { operation(source, 1, length); }
void after(InputMethod_TextEditorProxy* source, int length) { operation(source, 2, length); }
void enter(InputMethod_TextEditorProxy* source, InputMethod_EnterKeyType key) { operation(source, 3, key); }
void move(InputMethod_TextEditorProxy* source, InputMethod_Direction direction) { operation(source, 8, direction); }
void extend(InputMethod_TextEditorProxy* source, InputMethod_ExtendAction action) { operation(source, 7, action); }
void status(InputMethod_TextEditorProxy* source, InputMethod_KeyboardStatus value) {
    if (!generation(source)) return;
    active(value == IME_KEYBOARD_STATUS_SHOW);
    fprintf(stderr, "[OH_IME_CAPI] keyboard status=%d\n", value);
    reportKeyboardArea(editedWindow(), value == IME_KEYBOARD_STATUS_SHOW);
}
void selection(InputMethod_TextEditorProxy* source, int start, int end) {
    operation(source, 6, start, nullptr, 0, end);
}
void surrounding(InputMethod_TextEditorProxy* source, const char* method, int number, char16_t* text, size_t* length, bool left) {
    if (!length) return;
    *length = 0;
    const jlong epoch = generation(source);
    if (!epoch || number <= 0 || !text) return;
    Env e;
    if (!e.env || !bridge) return;
    jmethodID id = e.env->GetStaticMethodID(bridge, method, "()Ljava/lang/String;");
    jstring value = id ? static_cast<jstring>(e.env->CallStaticObjectMethod(bridge, id)) : nullptr;
    if (value && !e.env->ExceptionCheck()) {
        jsize size = e.env->GetStringLength(value);
        jsize count = std::min(size, static_cast<jsize>(number));
        e.env->GetStringRegion(value, left ? size - count : 0, count, reinterpret_cast<jchar*>(text));
        if (!e.env->ExceptionCheck() && generation(source) == epoch) *length = count;
    }
    exception(e.env);
    if (value) e.env->DeleteLocalRef(value);
}
void left(InputMethod_TextEditorProxy* source, int n, char16_t* text, size_t* length) {
    surrounding(source, "nativeGetBefore", n, text, length, true);
}
void right(InputMethod_TextEditorProxy* source, int n, char16_t* text, size_t* length) {
    surrounding(source, "nativeGetAfter", n, text, length, false);
}
int32_t cursor(InputMethod_TextEditorProxy* source) { return generation(source) ? getInt("nativeGetCursor") : 0; }
int32_t preview(InputMethod_TextEditorProxy* source, const char16_t* text, size_t length, int start, int end) {
    return operation(source, 4, start, text ? text : u"", length, end) ? 0 : -1;
}
void finish(InputMethod_TextEditorProxy* source) {
    operation(source, 5);
}
int32_t privateCommand(InputMethod_TextEditorProxy*, InputMethod_PrivateCommand*[], size_t) {
    // No vendor-private Android/OHOS command mapping has been negotiated.
    // Register the required callback but report unsupported, not success.
    return -1;
}

bool setup() {
    if (editor) return true;
    editor = OH_TextEditorProxy_Create();
    if (!editor) return false;
#define SET(name, callback) if (OH_TextEditorProxy_Set##name##Func(editor, callback) != IME_ERR_OK) { \
    OH_TextEditorProxy_Destroy(editor); editor = nullptr; return false; }
    SET(GetTextConfig, config);
    SET(InsertText, insert);
    SET(DeleteForward, after);
    SET(DeleteBackward, before);
    SET(SendKeyboardStatus, status);
    SET(SendEnterKey, enter);
    SET(MoveCursor, move);
    SET(HandleSetSelection, selection);
    SET(HandleExtendAction, extend);
    SET(GetLeftTextOfCursor, left);
    SET(GetRightTextOfCursor, right);
    SET(GetTextIndexAtCursor, cursor);
    SET(ReceivePrivateCommand, privateCommand);
    SET(SetPreviewText, preview);
    SET(FinishTextPreview, finish);
#undef SET
    return true;
}
} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* machine, void*) {
    vm = machine;
    Env e;
    if (!e.env) return JNI_ERR;
    jclass cls = e.env->FindClass("adapter/window/OhImeBridge");
    if (!cls) return JNI_ERR;
    bridge = static_cast<jclass>(e.env->NewGlobalRef(cls));
    e.env->DeleteLocalRef(cls);
    return bridge ? JNI_VERSION_1_6 : JNI_ERR;
}

extern "C" JNIEXPORT jboolean JNICALL Java_adapter_window_OhImeBridge_nativeShowKeyboardCapi(
        JNIEnv*, jclass, jlong epoch, jint windowId) {
    std::lock_guard<std::mutex> lock(mutex);
    if (epoch <= 0 || windowId <= 0) return JNI_FALSE;
    if (ime && generation(editor) != epoch) {
        // A previous detach may have failed after Java invalidated its session.
        // Retrying must recover that native handle, never bind a new generation
        // to its old callback identity or leave all future attaches blocked.
        identity(nullptr, 0, 0);
        if (OH_InputMethodController_Detach(ime) != IME_ERR_OK) return JNI_FALSE;
        ime = nullptr;
        if (editor) { OH_TextEditorProxy_Destroy(editor); editor = nullptr; }
        active(false);
    }
    if (!setup()) return JNI_FALSE;
    identity(editor, epoch, windowId);
    // Focus is delivered by the ability-owned session. The legacy WMS focus
    // interface is not served by SceneBoard and must not be used to force it.
    InputMethod_ErrorCode rc = IME_ERR_OK;
    if (ime) rc = OH_InputMethodProxy_ShowKeyboard(ime);
    if (!ime || rc != IME_ERR_OK) {
        if (ime) {
            if (OH_InputMethodController_Detach(ime) != IME_ERR_OK) return JNI_FALSE;
            ime = nullptr;
        }
        InputMethod_AttachOptions* options = OH_AttachOptions_Create(true);
        if (!options) return JNI_FALSE;
        rc = OH_InputMethodController_Attach(editor, options, &ime);
        OH_AttachOptions_Destroy(options);
    }
    if (rc == IME_ERR_OK) active(true);
    else identity(nullptr, 0, 0);
    fprintf(stderr, "[OH_IME_CAPI] attach/show=%d\n", rc);
    return rc == IME_ERR_OK ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_adapter_window_OhImeBridge_nativeHideKeyboardCapi(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(mutex);
    reportKeyboardArea(editedWindow(), false);
    // Invalidate before IPC: an in-flight callback already has its own epoch,
    // and Java rejects it even if a different editor is now focused.
    identity(nullptr, 0, 0);
    if (!ime) {
        if (editor) { OH_TextEditorProxy_Destroy(editor); editor = nullptr; }
        active(false); return JNI_TRUE;
    }
    const auto rc = OH_InputMethodController_Detach(ime);
    if (rc == IME_ERR_OK) {
        ime = nullptr;
        // Public C API permits releasing the proxy after successful detach.
        OH_TextEditorProxy_Destroy(editor); editor = nullptr;
        active(false);
    }
    fprintf(stderr, "[OH_IME_CAPI] detach=%d\n", rc);
    return rc == IME_ERR_OK ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_adapter_window_OhImeBridge_nativeNotifySelectionCapi(
        JNIEnv* env, jclass, jstring text, jint start, jint end) {
    if (!text) return;
    const jsize length = env->GetStringLength(text);
    if (length > 8192 || start < 0 || end < 0 || start > length || end > length) return;
    // Copy before IPC, without retaining GetStringChars or critical pins.
    std::vector<char16_t> value(length + 1);
    env->GetStringRegion(text, 0, length, reinterpret_cast<jchar*>(value.data()));
    if (env->ExceptionCheck()) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (!ime) return;
    const auto rc = OH_InputMethodProxy_NotifySelectionChange(ime, value.data(), length, start, end);
    if (rc != IME_ERR_OK) fprintf(stderr, "[OH_IME_CAPI] selection update failed=%d\n", rc);
}
