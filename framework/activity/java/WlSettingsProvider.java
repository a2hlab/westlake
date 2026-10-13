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
 * Semantics: every read reports "unset" — GET_* methods return a Bundle without a
 * value entry (getString -> null), LIST_* methods return an empty serializable map.
 * Callers then take their documented defaults, exactly like a fresh device image.
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

    /** The value a GET_* call answers, or null for "unset". */
    public static String answer(String method, String name) {
        return "GET_secure".equals(method) && "android_id".equals(name) ? androidId() : null;
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
