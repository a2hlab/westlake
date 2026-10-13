package adapter.core;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * The device's default sounds: the ringtone, the notification sound and the alarm that
 * content://settings/system/ringtone, notification_sound and alarm_alert stand for. Android
 * ships them as files under /system/media/audio and SettingsProvider opens them for those URIs
 * (MediaPlayer and Ringtone try the "_cache" form first). This board ships no ringtone files and
 * its ringtone settings are empty, so each is written once, as 48 kHz mono 16-bit WAV, into the
 * app's no_backup directory: a ringtone and an alarm pattern that loop, and a short chime.
 */
public final class DefaultSounds {
    private DefaultSounds() {}

    // RingtoneManager.TYPE_RINGTONE, TYPE_NOTIFICATION, TYPE_ALARM.
    public static final int RINGTONE = 1;
    public static final int NOTIFICATION = 2;
    public static final int ALARM = 4;

    private static final int RATE = 48000;

    /** The default-sound type a settings URI names (content://settings/system/<name>[_cache]), or 0. */
    public static int typeOf(android.net.Uri uri) {
        if (uri == null || !"settings".equals(uri.getAuthority())) return 0;
        java.util.List<String> path = uri.getPathSegments();
        if (path.size() != 2 || !"system".equals(path.get(0))) return 0;
        String name = path.get(1);
        if (name.endsWith("_cache")) name = name.substring(0, name.length() - "_cache".length());
        switch (name) {
            case "ringtone": return RINGTONE;
            case "notification_sound": return NOTIFICATION;
            case "alarm_alert": return ALARM;
            default: return 0;
        }
    }

    /** The sound for a usage (AudioAttributes.USAGE_*): the alarm for alarms, else the notification sound. */
    public static int typeForUsage(int usage) {
        if (usage == android.media.AudioAttributes.USAGE_ALARM) return ALARM;
        if (usage == android.media.AudioAttributes.USAGE_NOTIFICATION_RINGTONE) return RINGTONE;
        return NOTIFICATION;
    }

    /** The default sound's file, written on first use; null when it cannot be written. */
    public static synchronized File file(int type) {
        String name = type == ALARM ? "alarm" : type == RINGTONE ? "ringtone" : "notification";
        try {
            android.app.Application app = android.app.ActivityThread.currentApplication();
            if (app == null) return null;
            File file = new File(app.getNoBackupFilesDir(), "westlake-sounds/" + name + ".wav");
            if (file.isFile() && file.length() > 44) return file;
            file.getParentFile().mkdirs();
            writeWav(file, synthesize(type));
            return file;
        } catch (Throwable t) {
            System.err.println("[WL-SOUNDS] " + name + " not written: " + t);
            return null;
        }
    }

    private static short[] synthesize(int type) {
        Tone tone = new Tone();
        if (type == ALARM) {
            // Four short beeps and a pause; played looping, a steady alarm.
            for (int i = 0; i < 4; i++) {
                tone.note(880, 0.12, 0.5);
                tone.rest(0.08);
            }
            tone.rest(0.64);
        } else if (type == RINGTONE) {
            for (int i = 0; i < 2; i++) {
                tone.note(659, 0.2, 0.45);
                tone.note(523, 0.2, 0.45);
            }
            tone.rest(1.0);
        } else {
            tone.note(988, 0.1, 0.4);
            tone.rest(0.03);
            tone.decaying(1319, 0.3, 0.4);
        }
        return tone.samples();
    }

    /** Sine notes with 5 ms ramps, so no note starts or stops with a click. */
    private static final class Tone {
        private final java.util.ArrayList<Short> out = new java.util.ArrayList<>();

        void note(double hz, double seconds, double level) {
            int n = (int) (seconds * RATE), ramp = RATE / 200;
            for (int i = 0; i < n; i++) {
                double edge = Math.min(1.0, Math.min(i, n - 1 - i) / (double) ramp);
                out.add((short) (Math.sin(2 * Math.PI * hz * i / RATE) * level * edge * Short.MAX_VALUE));
            }
        }

        void decaying(double hz, double seconds, double level) {
            int n = (int) (seconds * RATE), ramp = RATE / 200;
            for (int i = 0; i < n; i++) {
                double edge = Math.min(1.0, i / (double) ramp) * Math.exp(-4.0 * i / n);
                out.add((short) (Math.sin(2 * Math.PI * hz * i / RATE) * level * edge * Short.MAX_VALUE));
            }
        }

        void rest(double seconds) {
            for (int i = 0, n = (int) (seconds * RATE); i < n; i++) out.add((short) 0);
        }

        short[] samples() {
            short[] result = new short[out.size()];
            for (int i = 0; i < result.length; i++) result[i] = out.get(i);
            return result;
        }
    }

    private static void writeWav(File file, short[] samples) throws IOException {
        int data = samples.length * 2;
        ByteBuffer buffer = ByteBuffer.allocate(44 + data).order(ByteOrder.LITTLE_ENDIAN);
        buffer.put("RIFF".getBytes()).putInt(36 + data).put("WAVE".getBytes());
        buffer.put("fmt ".getBytes()).putInt(16).putShort((short) 1).putShort((short) 1)
                .putInt(RATE).putInt(RATE * 2).putShort((short) 2).putShort((short) 16);
        buffer.put("data".getBytes()).putInt(data);
        for (short sample : samples) buffer.putShort(sample);
        File partial = new File(file.getPath() + ".part");
        try (FileOutputStream stream = new FileOutputStream(partial)) {
            stream.write(buffer.array());
        }
        if (!partial.renameTo(file)) throw new IOException("rename " + partial);
    }
}
