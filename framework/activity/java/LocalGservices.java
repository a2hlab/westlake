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
 */
final class LocalGservices extends ContentProvider {
    static final String AUTHORITY = "com.google.android.gsf.gservices";
    private static final String TAG = "WL_Gservices";
    private static final String[] COLUMNS = {"key", "value"};
    private static ContentProviderHolder sHolder;

    private final Properties mValues = new Properties();

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

    private static File file() {
        String data = SourcePackageRegistry.dataDir();
        File dir = new File(data != null ? data : "/data/data/com.google.android.gms", "westlake-gservices");
        dir.mkdirs();
        return new File(dir, "values.properties");
    }

    @Override
    public boolean onCreate() {
        File file = file();
        if (file.isFile()) {
            try (FileInputStream in = new FileInputStream(file)) {
                mValues.load(in);
            } catch (IOException e) {
                Log.e(TAG, "cannot read values: " + e.getClass().getName());
            }
        }
        return true;
    }

    /** By key from selection arguments; under /prefix, each argument is a prefix; no arguments, all. */
    @Override
    public synchronized Cursor query(Uri uri, String[] projection, String selection, String[] arguments, String order) {
        MatrixCursor cursor = new MatrixCursor(COLUMNS);
        boolean prefix = "prefix".equals(uri.getLastPathSegment());
        if (arguments == null || arguments.length == 0) {
            for (String key : new TreeSet<>(mValues.stringPropertyNames())) {
                cursor.addRow(new Object[] {key, mValues.getProperty(key)});
            }
            return cursor;
        }
        for (String argument : arguments) {
            if (!prefix) {
                cursor.addRow(new Object[] {argument, mValues.getProperty(argument)});
                continue;
            }
            for (String key : new TreeSet<>(mValues.stringPropertyNames())) {
                if (key.startsWith(argument)) cursor.addRow(new Object[] {key, mValues.getProperty(key)});
            }
        }
        return cursor;
    }

    @Override
    public synchronized int update(Uri uri, ContentValues values, String selection, String[] arguments) {
        if (values == null) return 0;
        for (Map.Entry<String, Object> entry : values.valueSet()) {
            if (entry.getValue() == null) mValues.remove(entry.getKey());
            else mValues.setProperty(entry.getKey(), String.valueOf(entry.getValue()));
        }
        try (FileOutputStream out = new FileOutputStream(file())) {
            mValues.store(out, null);
        } catch (IOException e) {
            Log.e(TAG, "cannot write values: " + e.getClass().getName());
        }
        Log.i(TAG, "stored " + values.size() + " values");
        // Readers cache values until told otherwise, so without this they keep what they read first:
        // an Android ID of 0, read before check-in finished.
        android.app.Application app = ActivityThread.currentApplication();
        if (app != null) {
            app.getContentResolver().notifyChange(Uri.parse("content://" + AUTHORITY), null);
            app.sendBroadcast(new android.content.Intent("com.google.gservices.intent.action.GSERVICES_CHANGED")
                    .setPackage(app.getPackageName()));
        }
        return values.size();
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
