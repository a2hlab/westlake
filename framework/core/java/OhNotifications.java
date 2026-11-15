package adapter.core;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.os.Bundle;
import android.os.Parcelable;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * What an app posts, shown in OH's notification centre (westlake ADR-0009 decision 1). The native half is in
 * liboh_adapter_bridge (oh_notification_client.cpp), which publishes through OH's notification client, the call
 * a HAP's notificationManager.publish reaches.
 *
 * OH sees every Westlake process as the host application, so all apps publish as one bundle. An Android
 * notification is OH's (label, id), and the label is the package and the tag: two apps' notifications with the
 * same id stay apart, and one app's cancelAll cancels only its own.
 *
 * Calls run in order on a thread of their own: Android's enqueueNotificationWithTag is one-way, and OH's publish
 * is a synchronous call to its notification service.
 *
 * Carried: title, text and sub text; the big text and inbox styles, and a messaging style as lines; the post time
 * and whether it is shown; auto-cancel; alert once; a progress bar; the group; the number. The channel's
 * importance picks OH's slot: HIGH and MAX show a banner (SOCIAL_COMMUNICATION for messages and calls,
 * SERVICE_REMINDER otherwise), DEFAULT and LOW are SERVICE_REMINDER without a banner, MIN is CONTENT_INFORMATION
 * (no status bar icon). OH plays no sound or vibration for them: LocalNotifications plays the sound by Android's
 * channel rules. A group summary is not published, since OH groups the notifications of a group itself.
 *
 * Not carried yet: the content intent and actions (OH needs a want agent that reaches the app), icons and
 * pictures, and the user dismissing a notification in OH (Android would remove it from the app's active ones).
 */
final class OhNotifications {
    private OhNotifications() {}

    private static final String TAG = "[WL-NOTIFY]";

    // Must match oh_notification_client.cpp.
    static final int CONTENT_NORMAL = 0;
    static final int CONTENT_LONG_TEXT = 1;
    static final int CONTENT_MULTI_LINE = 2;
    static final int FLAG_SHOW_TIME = 1;
    static final int FLAG_TAP_DISMISSED = 2;
    static final int FLAG_ALERT_ONCE = 4;
    static final int FLAG_BANNER = 8;
    static final int FLAG_PROGRESS = 16;
    static final int FLAG_INDETERMINATE = 32;
    static final int TEXT_TITLE = 0;
    static final int TEXT_TEXT = 1;
    static final int TEXT_ADDITIONAL = 2;
    static final int TEXT_LONG = 3;
    static final int TEXT_BRIEF = 4;
    static final int TEXT_EXPANDED_TITLE = 5;
    static final int TEXT_GROUP = 6;
    static final int TEXT_COUNT = 7;
    static final int NUMBER_DELIVERY_TIME = 0;
    static final int NUMBER_PROGRESS = 1;
    static final int NUMBER_PROGRESS_MAX = 2;
    static final int NUMBER_BADGE = 3;
    static final int NUMBER_COUNT = 4;

    // OH's NotificationConstant::SlotType.
    static final int SLOT_SOCIAL_COMMUNICATION = 0;
    static final int SLOT_SERVICE_REMINDER = 1;
    static final int SLOT_CONTENT_INFORMATION = 2;

    /** OH's multi-line content shows this many lines. */
    private static final int MAX_LINES = 7;
    /**
     * Longer text is cut here, at a character: OH cuts a long text at 3072 bytes of UTF-8, which can split a
     * character, and 1000 characters are at most 3000 bytes.
     */
    private static final int MAX_TEXT = 1000;

    private static native int nativePublish(String label, int id, int slot, int content, int flags, String[] texts,
            String[] lines, long[] numbers);

    private static native int nativeCancel(String label, int id);

    private static final ExecutorService QUEUE = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "wl-oh-notify");
        thread.setDaemon(true);
        return thread;
    });
    private static volatile boolean sUnbound;

    static String label(String pkg, String tag) {
        return pkg + "|" + (tag != null ? tag : "");
    }

    /** One publish, its arguments read from the notification when it is posted. */
    private static final class Request {
        String label;
        int id;
        int slot;
        int content = CONTENT_NORMAL;
        int flags;
        final String[] texts = new String[TEXT_COUNT];
        String[] lines;
        final long[] numbers = new long[NUMBER_COUNT];
    }

    /** Publish one notification, or update the one with its label and id. Nothing when OH cannot show it. */
    static void publish(String pkg, String tag, int id, Notification notification, NotificationChannel channel,
            CharSequence appLabel) {
        if (sUnbound || notification == null) return;
        if ((notification.flags & Notification.FLAG_GROUP_SUMMARY) != 0) return;
        int importance = channel != null ? channel.getImportance() : importanceOf(notification.priority);
        if (importance == NotificationManager.IMPORTANCE_NONE) return;
        final Request request;
        try {
            request = request(pkg, tag, id, notification, importance, appLabel);
        } catch (RuntimeException e) {
            System.err.println(TAG + " not shown in OH (" + pkg + " id " + id + "): " + e);
            return;
        }
        QUEUE.execute(() -> {
            int rc = call(() -> nativePublish(request.label, request.id, request.slot, request.content, request.flags,
                    request.texts, request.lines, request.numbers));
            if (rc != Integer.MIN_VALUE) {
                System.err.println(TAG + " OH " + (rc == 0 ? "shows" : "did not show (" + rc + ")") + " " + pkg
                        + " id " + id + (shortTag(tag)) + " in slot " + request.slot);
            }
        });
    }

    /** OH's ERR_ANS_NOTIFICATION_NOT_EXISTS: Android's cancel of a notification that is not there does nothing. */
    private static final int NOT_EXISTS = 0x4000010;

    static void cancel(String pkg, String tag, int id) {
        if (sUnbound) return;
        final String label = label(pkg, tag);
        QUEUE.execute(() -> {
            int rc = call(() -> nativeCancel(label, id));
            if (rc != Integer.MIN_VALUE && rc != 0 && rc != NOT_EXISTS) {
                System.err.println(TAG + " OH cancel of " + pkg + " id " + id + shortTag(tag) + ": " + rc);
            }
        });
    }

    private interface NativeCall {
        int run();
    }

    private static int call(NativeCall call) {
        if (sUnbound) return Integer.MIN_VALUE;
        try {
            return call.run();
        } catch (UnsatisfiedLinkError e) {
            sUnbound = true;
            System.err.println(TAG + " OH's notification client is not bound here: notifications stay in process");
            return Integer.MIN_VALUE;
        }
    }

    private static String shortTag(String tag) {
        return tag == null ? "" : tag.length() <= 32 ? " tag " + tag : " tag of " + tag.length() + " chars";
    }

    /** Android's importance for a notification posted without a channel (before Android 8), from its priority. */
    private static int importanceOf(int priority) {
        if (priority >= Notification.PRIORITY_HIGH) return NotificationManager.IMPORTANCE_HIGH;
        if (priority == Notification.PRIORITY_LOW) return NotificationManager.IMPORTANCE_LOW;
        if (priority <= Notification.PRIORITY_MIN) return NotificationManager.IMPORTANCE_MIN;
        return NotificationManager.IMPORTANCE_DEFAULT;
    }

    private static Request request(String pkg, String tag, int id, Notification n, int importance,
            CharSequence appLabel) {
        Request r = new Request();
        r.label = label(pkg, tag);
        r.id = id;
        Bundle extras = n.extras != null ? n.extras : new Bundle();
        boolean messaging = extras.containsKey(Notification.EXTRA_MESSAGES);
        boolean conversation = messaging || Notification.CATEGORY_MESSAGE.equals(n.category)
                || Notification.CATEGORY_CALL.equals(n.category);
        if (importance >= NotificationManager.IMPORTANCE_HIGH) {
            r.slot = conversation ? SLOT_SOCIAL_COMMUNICATION : SLOT_SERVICE_REMINDER;
            r.flags |= FLAG_BANNER;
        } else if (importance >= NotificationManager.IMPORTANCE_LOW) {
            r.slot = SLOT_SERVICE_REMINDER;
        } else {
            r.slot = SLOT_CONTENT_INFORMATION;
        }

        String title = text(extras.getCharSequence(Notification.EXTRA_TITLE));
        r.texts[TEXT_TITLE] = !title.isEmpty() ? title : text(appLabel);
        r.texts[TEXT_TEXT] = text(extras.getCharSequence(Notification.EXTRA_TEXT));
        String sub = text(extras.getCharSequence(Notification.EXTRA_SUB_TEXT));
        r.texts[TEXT_ADDITIONAL] = !sub.isEmpty() ? sub : text(extras.getCharSequence(Notification.EXTRA_INFO_TEXT));
        r.texts[TEXT_BRIEF] = text(extras.getCharSequence(Notification.EXTRA_SUMMARY_TEXT));
        r.texts[TEXT_EXPANDED_TITLE] = text(extras.getCharSequence(Notification.EXTRA_TITLE_BIG));
        r.texts[TEXT_GROUP] = n.getGroup() != null ? n.getGroup() : "";
        String bigText = text(extras.getCharSequence(Notification.EXTRA_BIG_TEXT));
        List<String> lines = lines(extras);
        if (!lines.isEmpty()) {
            r.content = CONTENT_MULTI_LINE;
            r.lines = lines.toArray(new String[0]);
            if (r.texts[TEXT_TEXT].isEmpty()) r.texts[TEXT_TEXT] = lines.get(lines.size() - 1);
        } else if (!bigText.isEmpty()) {
            r.content = CONTENT_LONG_TEXT;
            r.texts[TEXT_LONG] = bigText;
            if (r.texts[TEXT_TEXT].isEmpty()) r.texts[TEXT_TEXT] = bigText;
        }
        if (r.texts[TEXT_EXPANDED_TITLE].isEmpty()) r.texts[TEXT_EXPANDED_TITLE] = r.texts[TEXT_TITLE];

        if (n.when > 0) r.numbers[NUMBER_DELIVERY_TIME] = n.when;
        if (n.showsTime()) r.flags |= FLAG_SHOW_TIME;   // Android's rule: a time, and EXTRA_SHOW_WHEN
        if ((n.flags & Notification.FLAG_AUTO_CANCEL) != 0) r.flags |= FLAG_TAP_DISMISSED;
        if ((n.flags & Notification.FLAG_ONLY_ALERT_ONCE) != 0) r.flags |= FLAG_ALERT_ONCE;
        int max = extras.getInt(Notification.EXTRA_PROGRESS_MAX, 0);
        boolean indeterminate = extras.getBoolean(Notification.EXTRA_PROGRESS_INDETERMINATE, false);
        if (max > 0 || indeterminate) {
            r.flags |= FLAG_PROGRESS | (indeterminate ? FLAG_INDETERMINATE : 0);
            r.numbers[NUMBER_PROGRESS] = Math.max(0, Math.min(max, extras.getInt(Notification.EXTRA_PROGRESS, 0)));
            r.numbers[NUMBER_PROGRESS_MAX] = max;
        }
        if (n.number > 0) r.numbers[NUMBER_BADGE] = n.number;
        return r;
    }

    /** The inbox style's lines, or a messaging style's last messages as "sender: text". */
    private static List<String> lines(Bundle extras) {
        List<String> out = new ArrayList<>();
        CharSequence[] inbox = extras.getCharSequenceArray(Notification.EXTRA_TEXT_LINES);
        if (inbox != null) {
            for (CharSequence line : inbox) {
                if (out.size() == MAX_LINES) break;
                String s = text(line);
                if (!s.isEmpty()) out.add(s);
            }
            return out;
        }
        Parcelable[] messages = extras.getParcelableArray(Notification.EXTRA_MESSAGES);
        if (messages == null) return out;
        for (int i = Math.max(0, messages.length - MAX_LINES); i < messages.length; i++) {
            if (!(messages[i] instanceof Bundle)) continue;
            Bundle message = (Bundle) messages[i];
            String body = text(message.getCharSequence("text"));
            if (body.isEmpty()) continue;
            String sender = text(message.getCharSequence("sender"));
            if (sender.isEmpty() && message.getParcelable("sender_person") instanceof android.app.Person) {
                sender = text(((android.app.Person) message.getParcelable("sender_person")).getName());
            }
            out.add(sender.isEmpty() ? body : sender + ": " + body);
        }
        return out;
    }

    private static String text(CharSequence value) {
        if (value == null) return "";
        String s = value.toString().trim();
        return s.length() <= MAX_TEXT ? s : s.substring(0, MAX_TEXT);
    }
}
