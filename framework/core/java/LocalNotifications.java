package adapter.core;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.pm.ParceledListSlice;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.net.Uri;
import android.service.notification.StatusBarNotification;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * The in-process notification service: the app's channels and its posted notifications, and the
 * sound a post makes. NotificationManagerService plays a notification's sound when it is posted
 * (buzzBeepBlink): the channel's sound and audio attributes from Android 8 (the notification's own
 * before), at the channel's importance DEFAULT or above, an update only when it changes what the user
 * sees and never with ONLY_ALERT_ONCE, at most one alert a second,
 * looping with FLAG_INSISTENT until the notification is cancelled. A timer or an alarm rings that
 * way: Fossify Clock posts its expired timer on a channel whose sound is the alarm.
 *
 * Posts are kept only when the user lets the app post notifications (NotificationSwitch, the OH
 * per-application switch): Android drops the rest, sound included. Nothing is shown in OH's
 * notification centre; that is the OH notification service's, not bridged here. Toasts, which go
 * through the same service, are shown in process (LocalToasts).
 */
final class LocalNotifications {
    private LocalNotifications() {}

    private static final String TAG = "[WL-NOTIFY]";
    private static final Map<String, NotificationChannel> sChannels = new LinkedHashMap<>();
    private static final Map<String, StatusBarNotification> sActive = new LinkedHashMap<>();
    private static MediaPlayer sPlayer;
    private static String sAlerting;

    static Object answer(String method, Object[] args) {
        switch (method) {
            case "createNotificationChannels":
                createChannels((ParceledListSlice<?>) args[1]);
                return LocalServiceBinders.DEFAULT;
            case "deleteNotificationChannel":
                synchronized (sChannels) {
                    sChannels.remove((String) args[1]);
                }
                return LocalServiceBinders.DEFAULT;
            case "getNotificationChannel":
            case "getConversationNotificationChannel":
                synchronized (sChannels) {
                    return sChannels.get((String) args[3]);
                }
            case "getNotificationChannels":
            case "getNotificationChannelsForPackage":
                synchronized (sChannels) {
                    return new ParceledListSlice<>(new ArrayList<>(sChannels.values()));
                }
            case "areNotificationsEnabled":
            case "areNotificationsEnabledForPackage":
                return NotificationSwitch.enabled();
            case "enqueueNotificationWithTag":
                post((String) args[0], (String) args[2], (Integer) args[3], (Notification) args[4]);
                return LocalServiceBinders.DEFAULT;
            case "cancelNotificationWithTag":
                cancel(key((String) args[2], (Integer) args[3]));
                return LocalServiceBinders.DEFAULT;
            case "cancelAllNotifications":
                cancel(null);
                return LocalServiceBinders.DEFAULT;
            case "getAppActiveNotifications":
                synchronized (sActive) {
                    return new ParceledListSlice<>(new ArrayList<>(sActive.values()));
                }
            case "enqueueTextToast":
            case "enqueueToast":
            case "cancelToast":
            case "finishToken":
                return LocalToasts.answer(method, args);
            case "getActiveNotifications":
            case "getActiveNotificationsWithAttribution":
                synchronized (sActive) {
                    return sActive.values().toArray(new StatusBarNotification[0]);
                }
            default:
                return LocalServiceBinders.DEFAULT;
        }
    }

    private static String key(String tag, int id) {
        return tag + "|" + id;
    }

    /** As NotificationManagerService: a new channel is kept as given; an existing one keeps its settings. */
    private static void createChannels(ParceledListSlice<?> list) {
        if (list == null) return;
        synchronized (sChannels) {
            for (Object item : list.getList()) {
                NotificationChannel channel = (NotificationChannel) item;
                NotificationChannel existing = sChannels.get(channel.getId());
                if (existing == null) {
                    sChannels.put(channel.getId(), channel);
                } else {
                    existing.setName(channel.getName());
                    existing.setDescription(channel.getDescription());
                }
            }
        }
    }

    private static void post(String pkg, String tag, int id, Notification notification) {
        if (notification == null) return;
        if (!NotificationSwitch.enabled()) {
            System.err.println(TAG + " not posted: the user has not allowed this app's notifications");
            return;
        }
        String key = key(tag, id);
        StatusBarNotification record = new StatusBarNotification(pkg, pkg, id, tag, android.os.Process.myUid(),
                android.os.Process.myPid(), notification, android.os.Process.myUserHandle(), null,
                System.currentTimeMillis());
        StatusBarNotification previous;
        synchronized (sActive) {
            previous = sActive.put(key, record);
        }
        boolean update = previous != null;
        if (update && (notification.flags & Notification.FLAG_ONLY_ALERT_ONCE) != 0) return;
        // As NotificationManagerService.shouldMuteNotificationLocked: an update that changes nothing the user
        // sees is silent, and an app alerts at most once a second (AlertRateLimiter). Play services re-posted
        // one unchanged notification about every 5 s while apps came and went, and each post chimed.
        if (update && !visuallyInterruptive(previous.getNotification(), notification)) return;
        long now = android.os.SystemClock.elapsedRealtime();
        synchronized (LocalNotifications.class) {
            if (now - sLastAlert < ALERT_INTERVAL_MS) {
                System.err.println(TAG + " muting recently noisy " + key);
                return;
            }
        }
        Uri sound;
        AudioAttributes attributes;
        String channelId = notification.getChannelId();
        if (channelId != null) {
            NotificationChannel channel;
            synchronized (sChannels) {
                channel = sChannels.get(channelId);
            }
            if (channel == null || channel.getImportance() < NotificationManager.IMPORTANCE_DEFAULT) return;
            sound = channel.getSound();
            attributes = channel.getAudioAttributes();
        } else {
            sound = (notification.defaults & Notification.DEFAULT_SOUND) != 0
                    ? android.provider.Settings.System.DEFAULT_NOTIFICATION_URI : notification.sound;
            attributes = notification.audioAttributes;
        }
        if (sound == null || Uri.EMPTY.equals(sound)) return;
        synchronized (LocalNotifications.class) {
            sLastAlert = now;
        }
        play(key, sound, attributes, (notification.flags & Notification.FLAG_INSISTENT) != 0);
    }

    /** AlertRateLimiter's interval: one alert a second per app. */
    private static final long ALERT_INTERVAL_MS = 1000;
    private static long sLastAlert = -ALERT_INTERVAL_MS;   // guarded by the class

    /**
     * NotificationManagerService.isVisuallyInterruptive, for an update: whether the title, the text, the
     * completion of its progress or its actions changed. A foreground service's notification is one session
     * and its updates never are.
     */
    static boolean visuallyInterruptive(Notification old, Notification now) {
        if (old == null) return true;
        if ((now.flags & Notification.FLAG_FOREGROUND_SERVICE) != 0) return false;
        if (old.extras == null || now.extras == null) return false;
        if (!java.util.Objects.equals(String.valueOf(old.extras.get(Notification.EXTRA_TITLE)),
                String.valueOf(now.extras.get(Notification.EXTRA_TITLE)))) {
            return true;
        }
        if (!java.util.Objects.equals(String.valueOf(old.extras.get(Notification.EXTRA_TEXT)),
                String.valueOf(now.extras.get(Notification.EXTRA_TEXT)))) {
            return true;
        }
        if (completed(old) != completed(now)) return true;
        Notification.Action[] a = old.actions == null ? new Notification.Action[0] : old.actions;
        Notification.Action[] b = now.actions == null ? new Notification.Action[0] : now.actions;
        if (a.length != b.length) return true;
        for (int i = 0; i < a.length; i++) {
            if (!java.util.Objects.equals(String.valueOf(a[i].title), String.valueOf(b[i].title))) return true;
        }
        return false;
    }

    private static boolean completed(Notification n) {
        int max = n.extras.getInt(Notification.EXTRA_PROGRESS_MAX, 0);
        int progress = n.extras.getInt(Notification.EXTRA_PROGRESS, 0);
        return max > 0 && progress == max;
    }

    private static void cancel(String key) {
        synchronized (sActive) {
            if (key == null) sActive.clear();
            else sActive.remove(key);
        }
        synchronized (LocalNotifications.class) {
            if (sPlayer != null && (key == null || key.equals(sAlerting))) stop();
        }
    }

    /**
     * Play one notification's sound; a newer alert replaces it, as on Android. A sound that cannot
     * be opened falls back to the default sound for its usage, as Ringtone falls back.
     */
    private static synchronized void play(String key, Uri sound, AudioAttributes attributes, boolean insistent) {
        stop();
        int usage = attributes != null ? attributes.getUsage() : AudioAttributes.USAGE_NOTIFICATION;
        MediaPlayer player = new MediaPlayer();
        try {
            if (attributes != null) player.setAudioAttributes(attributes);
            try {
                player.setDataSource(android.app.ActivityThread.currentApplication(), sound);
            } catch (Exception unopened) {
                java.io.File fallback = DefaultSounds.file(DefaultSounds.typeForUsage(usage));
                if (fallback == null) throw unopened;
                player.reset();
                if (attributes != null) player.setAudioAttributes(attributes);
                player.setDataSource(fallback.getPath());
                System.err.println(TAG + " " + sound + " unopened (" + unopened + "): the default sound");
            }
            player.setLooping(insistent);
            player.setOnCompletionListener(done -> {
                synchronized (LocalNotifications.class) {
                    if (sPlayer == done) stop();
                }
            });
            player.setOnPreparedListener(MediaPlayer::start);
            player.prepareAsync();
            sPlayer = player;
            sAlerting = key;
            System.err.println(TAG + " sound for " + key + ": " + sound + " usage=" + usage
                    + (insistent ? " insistent" : ""));
        } catch (Throwable t) {
            player.release();
            System.err.println(TAG + " no sound for " + key + ": " + t);
        }
    }

    private static void stop() {
        if (sPlayer == null) return;
        try {
            sPlayer.stop();
        } catch (Throwable ignored) {
            // Not started yet.
        }
        sPlayer.release();
        sPlayer = null;
        sAlerting = null;
    }
}
