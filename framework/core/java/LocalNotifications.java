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
 * before), at the channel's importance DEFAULT or above, once per update with ONLY_ALERT_ONCE,
 * looping with FLAG_INSISTENT until the notification is cancelled. A timer or an alarm rings that
 * way: Fossify Clock posts its expired timer on a channel whose sound is the alarm.
 *
 * Posts are kept only when the user lets the app post notifications (NotificationSwitch, the OH
 * per-application switch): Android drops the rest, sound included. Nothing is shown in OH's
 * notification centre; that is the OH notification service's, not bridged here.
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
        boolean update;
        synchronized (sActive) {
            update = sActive.put(key, record) != null;
        }
        if (update && (notification.flags & Notification.FLAG_ONLY_ALERT_ONCE) != 0) return;
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
        play(key, sound, attributes, (notification.flags & Notification.FLAG_INSISTENT) != 0);
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
