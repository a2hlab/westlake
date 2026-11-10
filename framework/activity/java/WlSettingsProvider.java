package adapter.activity;

/**
 * WESTLAKE §611e — minimal in-process "settings" ContentProvider.
 *
 * This port has no settings service, so Settings$NameValueCache's provider lookup
 * returns null and any code path that reads settings through the provider API NPEs.
 * First caller found: androidx SearchView inflate → TextView Editor →
 * TextClassificationConstants → DeviceConfig.getProperty →
 * Settings$Config.getStrings → getStringsForPrefix (Material Catalog, cat_toc_header).
 *
 * AppSchedulerBridge appends a synthetic ProviderInfo (authority "settings") pointing
 * here, so ActivityThread.installContentProviders publishes it into the process-local
 * provider map at bind time — acquireProvider finds it BEFORE any AMS call, so the
 * stub AMS is never involved.
 *
 * Semantics: a read reports "unset" -- GET_* methods return a Bundle without a value entry
 * (getString -> null), LIST_* methods return an empty serializable map -- and callers take their
 * documented defaults, except for the settings every device in use holds (SYSTEM, GLOBAL, SECURE
 * below), which are answered with Android's first-boot values.
 *
 * One exception: Settings.Secure.ANDROID_ID. Android never leaves it unset; every app gets a
 * 64-bit value, stable for that app on that device. Jellyfin's API client builds its device
 * identity from it and dies on the null. It is generated once and kept in the app's no_backup
 * directory, so it survives restarts and differs between devices, as Android's does.
 *
 * Files: the default sounds' URIs open DefaultSounds' files (openFile), as SettingsProvider opens
 * the device's ringtones.
 */
public class WlSettingsProvider extends android.content.ContentProvider {
    @Override public boolean onCreate() { return true; }

    private static String sAndroidId;

    /** Settings.Secure.ANDROID_ID for this app: 16 hex digits, created on first read. */
    public static synchronized String androidId() {
        if (sAndroidId != null) return sAndroidId;
        java.io.File file = null;
        try {
            android.app.Application app = android.app.ActivityThread.currentApplication();
            if (app != null) file = new java.io.File(app.getDataDir(), "no_backup/westlake-android-id");
            if (file != null && file.isFile()) {
                String saved = new String(java.nio.file.Files.readAllBytes(file.toPath())).trim();
                if (saved.matches("[0-9a-f]{16}")) return sAndroidId = saved;
            }
        } catch (Throwable ignored) {
        }
        String id = String.format("%016x", new java.security.SecureRandom().nextLong());
        if (file != null) {
            try {
                file.getParentFile().mkdirs();
                java.nio.file.Files.write(file.toPath(), id.getBytes());
            } catch (Throwable ignored) {
                // Not persisted: stable for this process only.
            }
        }
        return sAndroidId = id;
    }

    /**
     * Settings a device in use always holds, as Android's SettingsProvider writes them on first boot
     * (DatabaseHelper, with AOSP's defaults.xml), and as a device past its setup wizard holds them.
     * Apps read some of these with getInt(resolver, name), which throws when the setting is unset:
     * ByteDance's video engine reads screen_brightness that way as every video starts, and Prime Video
     * the global boot_count as it starts a session. Settings for hardware the runtime does not report
     * (Bluetooth, a notification LED) stay unset, and so do the settings apps read to judge the device
     * itself (adb_enabled, development_settings_enabled).
     */
    private static final java.util.Map<String, String> SYSTEM = new java.util.HashMap<>();
    private static final java.util.Map<String, String> GLOBAL = new java.util.HashMap<>();
    private static final java.util.Map<String, String> SECURE = new java.util.HashMap<>();
    static {
        SYSTEM.put("screen_brightness", "102");
        SYSTEM.put("screen_brightness_mode", "0");          // manual
        SYSTEM.put("screen_off_timeout", "60000");
        SYSTEM.put("accelerometer_rotation", "0");
        SYSTEM.put("user_rotation", "0");
        SYSTEM.put("dim_screen", "1");
        SYSTEM.put("haptic_feedback_enabled", "1");
        SYSTEM.put("sound_effects_enabled", "1");
        SYSTEM.put("dtmf_tone", "1");
        SYSTEM.put("pointer_speed", "0");
        SYSTEM.put("font_scale", "1.0");
        GLOBAL.put("airplane_mode_on", "0");
        GLOBAL.put("auto_time", "1");
        GLOBAL.put("auto_time_zone", "1");
        GLOBAL.put("stay_on_while_plugged_in", "0");
        GLOBAL.put("mode_ringer", "2");                     // AudioManager.RINGER_MODE_NORMAL
        GLOBAL.put("device_provisioned", "1");              // set by the setup wizard
        GLOBAL.put("boot_count", "1");                      // counted by the system at each boot
        SECURE.put("user_setup_complete", "1");             // set by the setup wizard
    }

    /** The value a GET_* call answers, or null for "unset". */
    public static String answer(String method, String name) {
        if (method == null || name == null) return null;
        switch (method) {
            case "GET_secure":
                return "android_id".equals(name) ? androidId() : SECURE.get(name);
            case "GET_system":
                return SYSTEM.get(name);
            case "GET_global":
                return GLOBAL.get(name);
            default:
                return null;
        }
    }

    @Override
    public android.os.Bundle call(String method, String arg, android.os.Bundle extras) {
        android.os.Bundle b = new android.os.Bundle();
        String value = answer(method, arg);
        if (value != null) b.putString("value", value);
        if (method != null && method.startsWith("LIST")) {
            b.putSerializable("value", new java.util.HashMap<String, String>());
        }
        return b;
    }

    @Override
    public android.database.Cursor query(android.net.Uri uri, String[] projection,
            String selection, String[] selectionArgs, String sortOrder) {
        return new android.database.MatrixCursor(new String[] { "name", "value" });
    }

    @Override
    public String getType(android.net.Uri uri) {
        return adapter.core.DefaultSounds.typeOf(uri) != 0 ? "audio/wav" : null;
    }

    /**
     * The default ringtone, notification sound and alarm (content://settings/system/ringtone,
     * notification_sound and alarm_alert, and their "_cache" forms), as SettingsProvider opens
     * them: MediaPlayer.setDataSource and Ringtone open these URIs to play the device's defaults.
     * The board has no ringtone files, so these are DefaultSounds' own.
     */
    @Override
    public android.os.ParcelFileDescriptor openFile(android.net.Uri uri, String mode)
            throws java.io.FileNotFoundException {
        int type = adapter.core.DefaultSounds.typeOf(uri);
        java.io.File file = type == 0 ? null : adapter.core.DefaultSounds.file(type);
        if (file == null) throw new java.io.FileNotFoundException("no settings file for " + uri);
        return android.os.ParcelFileDescriptor.open(file, android.os.ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public android.net.Uri insert(android.net.Uri uri, android.content.ContentValues values) {
        return null;
    }

    @Override public int delete(android.net.Uri uri, String selection, String[] args) { return 0; }

    @Override
    public int update(android.net.Uri uri, android.content.ContentValues values,
            String selection, String[] args) {
        return 0;
    }
}
