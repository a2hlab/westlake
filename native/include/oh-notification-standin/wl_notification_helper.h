/*
 * The NotificationHelper members Westlake calls, declared exactly as interfaces/inner_api/notification_helper.h
 * declares them at OpenHarmony-v6.1-Release. That header also includes headers OpenHarmony generates from IDL at
 * build time (ians_operation_callback.h, the dialog and subscriber stubs), which no checkout carries. The definitions
 * are libans_innerkits.z.so's; a declaration that differed from them would not link.
 */
#ifndef WESTLAKE_STANDIN_NOTIFICATION_HELPER_H
#define WESTLAKE_STANDIN_NOTIFICATION_HELPER_H

#include <string>

#include "errors.h"
#include "notification_request.h"

namespace OHOS {
namespace Notification {
class NotificationHelper {
public:
    static ErrCode PublishNotification(const std::string &label, const NotificationRequest &request,
        const std::string &instanceKey = "");
    static ErrCode CancelNotification(const std::string &label, int32_t notificationId,
        const std::string &instanceKey = "");
};
}  // namespace Notification
}  // namespace OHOS

#endif
