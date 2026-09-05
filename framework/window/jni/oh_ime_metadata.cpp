// Metadata-only inner API bridge. Load only in a forked app, never prefork.
// Use official OH declarations and owned sptr/shared_ptr values, not vtables.
#include <jni.h>
#include "input_method_controller.h"
#include <vector>
#include <string>
#include <cstddef>
#include <cstdio>
using OHOS::MiscServices::InputMethodController;
using OHOS::MiscServices::Property;
using OHOS::MiscServices::SubProperty;
// Confirmed against board GetCurrentInputMethod's Parcelable construction.
static_assert(sizeof(std::string) == 24 && sizeof(Property) == 144);
static_assert(sizeof(SubProperty) == 216);
namespace {
jobjectArray row(JNIEnv* env, const Property& prop, const SubProperty* sub) {
    jclass string = env->FindClass("java/lang/String");
    if (!string) return nullptr;
    jobjectArray result = env->NewObjectArray(6, string, nullptr);
    env->DeleteLocalRef(string);
    std::string values[] = {prop.name, prop.id, prop.label,
        sub ? sub->id : "", sub ? sub->locale : "", sub ? sub->language : ""};
    for (int i=0; result && i<6 && !env->ExceptionCheck(); ++i) {
        jstring value=env->NewStringUTF(values[i].c_str());
        env->SetObjectArrayElement(result,i,value);
        env->DeleteLocalRef(value);
    }
    return result;
}
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_adapter_window_OhImeMetadata_nativeCurrent(JNIEnv* env,jclass) {
    auto controller=InputMethodController::GetInstance();
    if (!controller) return nullptr;
    auto prop=controller->GetCurrentInputMethod();
    auto sub=controller->GetCurrentInputMethodSubtype();
    return prop ? row(env,*prop,sub.get()) : nullptr;
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_adapter_window_OhImeMetadata_nativeList(JNIEnv* env,jclass,jboolean enabled) {
    auto controller=InputMethodController::GetInstance();
    if (!controller) return nullptr;
    std::vector<Property> properties;
    int rc=enabled ? controller->ListInputMethod(true,properties) : controller->ListInputMethod(properties);
    if (rc || properties.size()>128) return nullptr;
    std::vector<std::pair<size_t, SubProperty>> subtypes;
    for (size_t i=0;i<properties.size();++i) {
        std::vector<SubProperty> items;
        if (controller->ListInputMethodSubtype(properties[i],items) || items.empty())
            items.emplace_back();
        if (items.size()>128 || subtypes.size()+items.size()>1024) return nullptr;
        for (const auto& item:items) subtypes.emplace_back(i,item);
    }
    jclass array=env->FindClass("[Ljava/lang/String;");
    if (!array) return nullptr;
    jobjectArray result=env->NewObjectArray(subtypes.size(),array,nullptr);
    env->DeleteLocalRef(array);
    for (size_t i=0;result && i<subtypes.size() && !env->ExceptionCheck();++i) {
        jobjectArray value=row(env,properties[subtypes[i].first],&subtypes[i].second);
        env->SetObjectArrayElement(result,i,value);env->DeleteLocalRef(value);
    }
    return result;
}
extern "C" JNIEXPORT void JNICALL Java_adapter_window_OhImeMetadata_nativeSelection(
        JNIEnv* env,jclass,jstring value,jint start,jint end) {
    if (!value) return;
    const jsize length=env->GetStringLength(value);
    // Bound IPC payload without ever labelling a truncated window as a document.
    if (length>262144 || start<0 || end<0 || start>length || end>length) return;
    const jchar* chars=env->GetStringChars(value,nullptr);
    if (!chars) return;
    std::u16string text(reinterpret_cast<const char16_t*>(chars),length);
    env->ReleaseStringChars(value,chars);
    auto controller=InputMethodController::GetInstance();
    if (controller) {
        controller->OnSelectionChange(text,start,end);
        fprintf(stderr,"[OH_IME_META] large selection length=%d start=%d end=%d\n",length,start,end);
    }
}
extern "C" JNIEXPORT jboolean JNICALL Java_adapter_window_OhImeMetadata_nativePicker(JNIEnv*,jclass) {
    auto controller=InputMethodController::GetInstance();
    return controller && controller->DisplayOptionalInputMethod()==0 ? JNI_TRUE : JNI_FALSE;
}
