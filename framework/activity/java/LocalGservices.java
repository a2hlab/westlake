package adapter.activity;

import android.app.ActivityThread;
import android.app.ContentProviderHolder;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.ProviderInfo;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.util.Log;
import adapter.packagemanager.SourcePackageRegistry;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Map;
import java.util.Properties;
import java.util.TreeSet;

/**
 * Services Framework's settings table, com.google.android.gsf.gservices, in the shared Play services
 * process (ADR-0001). Services Framework's own code can't start inside Play services' start-up, and
 * Play services keeps there what check-in returns, the Android ID among them, so sign-in stops without
 * it. This stores what it is given and answers with that: it makes up no values.
 *
 * Writes keep the meanings of Play services' own provider (GservicesChimeraProvider, which serves this table
 * from Android 15): .../main replaces the whole table with check-in's settings, .../main_diff applies a
 * difference (a null value deletes the key), and .../override sets values kept apart that win over the
 * table's (empty values clear them all). A merge for every write kept keys check-in no longer sends, for
 * example a restriction Google's servers had lifted.
 */
final class LocalGservices extends ContentProvider {
    static final String AUTHORITY = "com.google.android.gsf.gservices";
    private static final String TAG = "WL_Gservices";
    private static final String[] COLUMNS = {"key", "value"};
    private static ContentProviderHolder sHolder;

    private final Properties mValues = new Properties();
    private final Properties mOverrides = new Properties();

    /** The holder getContentProvider answers with. Null if Services Framework isn't known here. */
    static synchronized ContentProviderHolder holder() {
        if (sHolder != null) return sHolder;
        ProviderInfo info = adapter.packagemanager.GuestPackages.resolveProvider(AUTHORITY, 0);
        if (info == null) return null;
        LocalGservices provider = new LocalGservices();
        provider.attachInfo(ActivityThread.currentActivityThread().getSystemContext(), info);
        ContentProviderHolder holder = new ContentProviderHolder(info);
        holder.provider = provider.getIContentProvider();
        holder.noReleaseNeeded = true;
        sHolder = holder;
        return holder;
    }

    private static File file(String name) {
        String data = SourcePackageRegistry.dataDir();
        File dir = new File(data != null ? data : "/data/data/com.google.android.gms", "westlake-gservices");
        dir.mkdirs();
        return new File(dir, name);
    }

    private static void load(Properties into, File file) {
        if (!file.isFile()) return;
        try (FileInputStream in = new FileInputStream(file)) {
            into.load(in);
        } catch (IOException e) {
            Log.e(TAG, "cannot read " + file.getName() + ": " + e.getClass().getName());
        }
    }

    private static void save(Properties from, File file) {
        try (FileOutputStream out = new FileOutputStream(file)) {
            from.store(out, null);
        } catch (IOException e) {
            Log.e(TAG, "cannot write " + file.getName() + ": " + e.getClass().getName());
        }
    }

    @Override
    public boolean onCreate() {
        load(mValues, file("values.properties"));
        load(mOverrides, file("overrides.properties"));
        return true;
    }

    /** A key's value as readers see it: an override wins over the table. */
    private String value(String key) {
        String override = mOverrides.getProperty(key);
        return override != null ? override : mValues.getProperty(key);
    }

    private TreeSet<String> keys() {
        TreeSet<String> keys = new TreeSet<>(mValues.stringPropertyNames());
        keys.addAll(mOverrides.stringPropertyNames());
        return keys;
    }

    /** By key from selection arguments; under /prefix, each argument is a prefix; no arguments, all. */
    @Override
    public synchronized Cursor query(Uri uri, String[] projection, String selection, String[] arguments, String order) {
        MatrixCursor cursor = new MatrixCursor(COLUMNS);
        boolean prefix = "prefix".equals(uri.getLastPathSegment());
        if (arguments == null || arguments.length == 0) {
            for (String key : keys()) {
                cursor.addRow(new Object[] {key, value(key)});
            }
            return cursor;
        }
        for (String argument : arguments) {
            if (!prefix) {
                cursor.addRow(new Object[] {argument, value(argument)});
                continue;
            }
            for (String key : keys()) {
                if (key.startsWith(argument)) cursor.addRow(new Object[] {key, value(key)});
            }
        }
        return cursor;
    }

    @Override
    public synchronized int update(Uri uri, ContentValues values, String selection, String[] arguments) {
        String table = uri != null ? uri.getLastPathSegment() : null;
        int count = values != null ? values.size() : 0;
        if ("override".equals(table)) {
            if (count == 0) {
                mOverrides.clear();
            } else {
                apply(mOverrides, values);
            }
            save(mOverrides, file("overrides.properties"));
        } else if ("main".equals(table)) {
            // check-in's whole settings table: what it no longer sends is gone, as in Play services' provider
            int before = mValues.size();
            mValues.clear();
            if (values != null) {
                for (Map.Entry<String, Object> entry : values.valueSet()) {
                    if (entry.getValue() != null) mValues.setProperty(entry.getKey(), String.valueOf(entry.getValue()));
                }
            }
            save(mValues, file("values.properties"));
            Log.i(TAG, "main replaced: " + before + " values before, " + mValues.size() + " now");
        } else {
            // main_diff, and the bare authority as earlier callers used it: a difference
            if (values == null) return 0;
            apply(mValues, values);
            save(mValues, file("values.properties"));
        }
        Log.i(TAG, "stored " + count + " values (" + (table != null ? table : "authority") + ")");
        if (adapter.core.GoogleBootstrapExperiment.enabled()) {
            String status = value("uncertified_status");
            // Only a bounded numeric policy field; never dump a server settings table or android_id.
            if (status != null && !status.matches("[0-9]{1,3}")) status = "unrecognized";
            adapter.core.GoogleBootstrapExperiment.event("gservices_update="
                    + ("main".equals(table) ? "full" : "difference")
                    + " uncertified_status=" + (status == null ? "absent" : status)
                    + " policy_override=" + mOverrides.containsKey("uncertified_status")
                    + " device_id_hash=" + (value("android_id") == null ? "absent"
                        : adapter.core.GoogleBootstrapExperiment.deviceIdHash(value("android_id")))
                    + " freshness=provider_write_only");
        }
        // Readers cache values until told otherwise, so without this they keep what they read first:
        // an Android ID of 0, read before check-in finished.
        android.app.Application app = ActivityThread.currentApplication();
        if (app != null) {
            app.getContentResolver().notifyChange(Uri.parse("content://" + AUTHORITY), null);
            app.sendBroadcast(new android.content.Intent("com.google.gservices.intent.action.GSERVICES_CHANGED")
                    .setPackage(app.getPackageName()));
        }
        return count;
    }

    /** A difference: a null value deletes the key. */
    private static void apply(Properties into, ContentValues values) {
        for (Map.Entry<String, Object> entry : values.valueSet()) {
            if (entry.getValue() == null) into.remove(entry.getKey());
            else into.setProperty(entry.getKey(), String.valueOf(entry.getValue()));
        }
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        update(uri, values, null, null);
        return uri;
    }

    @Override
    public int delete(Uri uri, String selection, String[] arguments) {
        return 0;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }
}
