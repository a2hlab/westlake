/*
 * oh_notification_client.cpp
 *
 * adapter.core.OhNotifications' natives: an Android app's notification shown in OH's notification centre,
 * through OH's notification client (NotificationHelper in libans_innerkits.z.so), the call a HAP's
 * notificationManager.publish reaches (westlake ADR-0009 decision 1).
 *
 * OH sees every Westlake process as the host application, so all Android apps publish as one bundle. Each
 * Android notification therefore carries a label of its own (its package and tag) and is cancelled by label
 * and id; CancelAllNotifications would cancel every app's.
 *
 * nativePublish(label, id, slot, content, flags, texts, lines, numbers) returns OH's result, 0 when published.
 * nativeCancel(label, id) returns OH's result.
 *
 * Sound and vibration are closed on every request: Westlake plays a notification's sound itself, by Android's
 * channel rules (LocalNotifications). The banner is closed unless the Java side asks for it; OH shows one only
 * when the user has allowed the host application's banners (its default slot flags leave them off).
 *
 * Reference paths (OpenHarmony-v6.1-Release, distributed_notification_service):
 *   interfaces/inner_api/notification_helper.h, notification_request.h, notification_flags.h,
 *   notification_normal_content.h, notification_long_text_content.h, notification_multiline_content.h
 */
#include <jni.h>
#include <android/log.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "notification_content.h"
#include "notification_flags.h"
#include "notification_long_text_content.h"
#include "notification_multiline_content.h"
#include "notification_normal_content.h"
#include "notification_request.h"
#include "wl_notification_helper.h"

#define NOTIFY_TAG "OH_Notification"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, NOTIFY_TAG, __VA_ARGS__)

using OHOS::Notification::NotificationConstant;
using OHOS::Notification::NotificationContent;
using OHOS::Notification::NotificationFlags;
using OHOS::Notification::NotificationHelper;
using OHOS::Notification::NotificationLongTextContent;
using OHOS::Notification::NotificationMultiLineContent;
using OHOS::Notification::NotificationNormalContent;
using OHOS::Notification::NotificationRequest;

namespace {

// Must match adapter.core.OhNotifications.
enum Content { CONTENT_NORMAL = 0, CONTENT_LONG_TEXT = 1, CONTENT_MULTI_LINE = 2 };
enum Flag {
    FLAG_SHOW_TIME = 1,
    FLAG_TAP_DISMISSED = 2,
    FLAG_ALERT_ONCE = 4,
    FLAG_BANNER = 8,
    FLAG_PROGRESS = 16,
    FLAG_INDETERMINATE = 32,
};
enum Text { TEXT_TITLE, TEXT_TEXT, TEXT_ADDITIONAL, TEXT_LONG, TEXT_BRIEF, TEXT_EXPANDED_TITLE, TEXT_GROUP, TEXT_COUNT };
enum Number { NUMBER_DELIVERY_TIME, NUMBER_PROGRESS, NUMBER_PROGRESS_MAX, NUMBER_BADGE, NUMBER_COUNT };
constexpr jint RESULT_BAD_ARGUMENT = -1;

// UTF-8 from the string's UTF-16, as OH expects; GetStringUTFChars gives modified UTF-8, which splits
// characters outside the BMP (emoji) into surrogates. An unpaired surrogate becomes U+FFFD.
std::string Utf8(JNIEnv* env, jstring value)
{
    std::string out;
    if (value == nullptr) {
        return out;
    }
    const jsize length = env->GetStringLength(value);
    std::vector<jchar> units(length);
    env->GetStringRegion(value, 0, length, units.data());
    out.reserve(length);
    for (jsize i = 0; i < length; i++) {
        uint32_t c = units[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < length && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (units[++i] - 0xDC00);
        } else if (c >= 0xD800 && c < 0xE000) {
            c = 0xFFFD;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::vector<std::string> Strings(JNIEnv* env, jobjectArray array, jsize count)
{
    std::vector<std::string> out(count);
    if (array == nullptr) {
        return out;
    }
    const jsize length = std::min(count, env->GetArrayLength(array));
    for (jsize i = 0; i < length; i++) {
        jstring item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        out[i] = Utf8(env, item);
        if (item != nullptr) {
            env->DeleteLocalRef(item);
        }
    }
    return out;
}

template <typename T>
void SetBasic(const std::shared_ptr<T>& content, const std::vector<std::string>& texts)
{
    content->SetTitle(texts[TEXT_TITLE]);
    content->SetText(texts[TEXT_TEXT]);
    if (!texts[TEXT_ADDITIONAL].empty()) {
        content->SetAdditionalText(texts[TEXT_ADDITIONAL]);
    }
}

std::shared_ptr<NotificationContent> Body(jint kind, const std::vector<std::string>& texts,
    const std::vector<std::string>& lines)
{
    if (kind == CONTENT_LONG_TEXT) {
        auto content = std::make_shared<NotificationLongTextContent>();
        SetBasic(content, texts);
        content->SetLongText(texts[TEXT_LONG]);
        content->SetBriefText(texts[TEXT_BRIEF]);
        content->SetExpandedTitle(texts[TEXT_EXPANDED_TITLE]);
        return std::make_shared<NotificationContent>(content);
    }
    if (kind == CONTENT_MULTI_LINE) {
        auto content = std::make_shared<NotificationMultiLineContent>();
        SetBasic(content, texts);
        content->SetBriefText(texts[TEXT_BRIEF]);
        content->SetExpandedTitle(texts[TEXT_EXPANDED_TITLE]);
        for (const auto& line : lines) {
            content->AddSingleLine(line);
        }
        return std::make_shared<NotificationContent>(content);
    }
    auto content = std::make_shared<NotificationNormalContent>();
    SetBasic(content, texts);
    return std::make_shared<NotificationContent>(content);
}

jint NativePublish(JNIEnv* env, jclass, jstring jlabel, jint id, jint slot, jint kind, jint flags,
    jobjectArray jtexts, jobjectArray jlines, jlongArray jnumbers)
{
    if (jlabel == nullptr || slot < NotificationConstant::SlotType::SOCIAL_COMMUNICATION ||
        slot >= NotificationConstant::SlotType::ILLEGAL_TYPE) {
        return RESULT_BAD_ARGUMENT;
    }
    const std::string label = Utf8(env, jlabel);
    const std::vector<std::string> texts = Strings(env, jtexts, TEXT_COUNT);
    const std::vector<std::string> lines =
        Strings(env, jlines, jlines != nullptr ? env->GetArrayLength(jlines) : 0);
    jlong numbers[NUMBER_COUNT] = {};
    if (jnumbers != nullptr) {
        env->GetLongArrayRegion(jnumbers, 0, std::min<jsize>(NUMBER_COUNT, env->GetArrayLength(jnumbers)), numbers);
    }

    NotificationRequest request(id);
    // OH keys a notification by the request's own label: the service does not copy PublishNotification's label
    // argument into the request, and a cancel by label would then find nothing (ERR_ANS_NOTIFICATION_NOT_EXISTS).
    request.SetLabel(label);
    request.SetSlotType(static_cast<NotificationConstant::SlotType>(slot));
    request.SetContent(Body(kind, texts, lines));
    auto reminders = std::make_shared<NotificationFlags>();
    reminders->SetSoundEnabled(NotificationConstant::FlagStatus::CLOSE);
    reminders->SetVibrationEnabled(NotificationConstant::FlagStatus::CLOSE);
    if ((flags & FLAG_BANNER) == 0) {
        reminders->SetBannerEnabled(NotificationConstant::FlagStatus::CLOSE);
    }
    request.SetFlags(reminders);
    if (numbers[NUMBER_DELIVERY_TIME] > 0) {
        request.SetDeliveryTime(numbers[NUMBER_DELIVERY_TIME]);
    }
    request.SetShowDeliveryTime((flags & FLAG_SHOW_TIME) != 0);
    request.SetTapDismissed((flags & FLAG_TAP_DISMISSED) != 0);
    request.SetAlertOneTime((flags & FLAG_ALERT_ONCE) != 0);
    if ((flags & FLAG_PROGRESS) != 0) {
        request.SetProgressBar(static_cast<int32_t>(numbers[NUMBER_PROGRESS]),
            static_cast<int32_t>(numbers[NUMBER_PROGRESS_MAX]), (flags & FLAG_INDETERMINATE) != 0);
    }
    if (!texts[TEXT_GROUP].empty()) {
        request.SetGroupName(texts[TEXT_GROUP]);
    }
    if (numbers[NUMBER_BADGE] > 0) {
        request.SetBadgeNumber(static_cast<uint32_t>(numbers[NUMBER_BADGE]));
    }
    return NotificationHelper::PublishNotification(label, request);
}

jint NativeCancel(JNIEnv* env, jclass, jstring jlabel, jint id)
{
    if (jlabel == nullptr) {
        return RESULT_BAD_ARGUMENT;
    }
    return NotificationHelper::CancelNotification(Utf8(env, jlabel), id);
}

}  // namespace

// Called from JNI_OnLoad. A framework without adapter.core.OhNotifications keeps its in-process notifications.
int register_OhNotifications(JNIEnv* env)
{
    jclass cls = env->FindClass("adapter/core/OhNotifications");
    if (cls == nullptr) {
        env->ExceptionClear();
        return JNI_ERR;
    }
    const JNINativeMethod methods[] = {
        {"nativePublish", "(Ljava/lang/String;IIII[Ljava/lang/String;[Ljava/lang/String;[J)I",
            reinterpret_cast<void*>(NativePublish)},
        {"nativeCancel", "(Ljava/lang/String;I)I", reinterpret_cast<void*>(NativeCancel)},
    };
    const jint rc = env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0]));
    env->DeleteLocalRef(cls);
    if (rc != JNI_OK) {
        env->ExceptionClear();
        LOGE("RegisterNatives(OhNotifications) failed rc=%d", rc);
        return JNI_ERR;
    }
    return JNI_OK;
}
