package adapter.core;

/**
 * Whether the user lets this app post notifications. Android keeps that as POST_NOTIFICATIONS (a
 * runtime permission from API 33) and NotificationManager.areNotificationsEnabled; OpenHarmony
 * keeps it as a per-application switch, the host application's here, which the user turns on in
 * Settings (Notifications and status bar). The native half lives in libwl_missing_natives
 * (notification_natives.c, OH_Notification_IsNotificationEnabled).
 */
public final class NotificationSwitch {
    private NotificationSwitch() {}

    private static native boolean nativeEnabled();

    public static boolean enabled() {
        try {
            return nativeEnabled();
        } catch (UnsatisfiedLinkError e) {
            return false;   // no binding: nothing says the user allowed them
        }
    }
}
