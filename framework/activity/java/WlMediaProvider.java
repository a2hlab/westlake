package adapter.activity;

import android.content.ContentProvider;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Intent;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.ParcelFileDescriptor;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * An in-process MediaStore ("content://media"), holding the app's own items.
 *
 * Every Android device has MediaStore, and since Android 10 an app saves to shared storage by
 * inserting into it and writing the returned URI; it needs no permission for its own items. Here no
 * provider had the authority, so ContentResolver.insert threw "Unknown URL": Prime Video's
 * Application.onCreate creates a file through MediaStore.Files and never started; LinkedIn died
 * saving a download.
 *
 * Items live in one table (as MediaProvider's files table) and their files under the app's
 * external storage, in Android's default directories (Pictures/, Movies/, Music/, Download/) or
 * the RELATIVE_PATH given, with a name made unique as MediaProvider makes it. The collections are
 * file, images/media, video/media, audio/media and downloads, on any volume name. What it answers is
 * what Android answers an app about its own items; OpenHarmony's media library does not see them.
 *
 * AppSchedulerBridge appends its ProviderInfo at bind, as for WlSettingsProvider.
 */
public class WlMediaProvider extends ContentProvider {
    private static final String TABLE = "files";
    private static final int IMAGE = 1, AUDIO = 2, VIDEO = 3;
    private static final String[] COLUMNS = {
            "_id", "_data", "_display_name", "title", "mime_type", "relative_path", "volume_name",
            "media_type", "is_download", "is_pending", "is_trashed", "is_favorite", "date_added",
            "date_modified", "datetaken", "_size", "owner_package_name",
            "bucket_display_name", "bucket_id", "width", "height", "duration", "orientation",
            "album", "artist", "description", "download_uri", "referer_uri", "generation_added",
            "generation_modified"};
    private static final Set<String> KNOWN = new HashSet<>(Arrays.asList(COLUMNS));

    private SQLiteOpenHelper mHelper;
    private Handler mCloses;

    /** A URI's collection: path below the volume, the media type it holds (0: any), and the item id. */
    private static final class Target {
        final String volume, collection;
        final int mediaType;
        final boolean downloads;
        final long id;

        Target(String volume, String collection, int mediaType, boolean downloads, long id) {
            this.volume = volume;
            this.collection = collection;
            this.mediaType = mediaType;
            this.downloads = downloads;
            this.id = id;
        }
    }

    @Override
    public boolean onCreate() {
        mHelper = new SQLiteOpenHelper(getContext(),
                new File(getContext().getDataDir(), "no_backup/westlake-media.db").getPath(), null, 1) {
            @Override
            public void onCreate(SQLiteDatabase db) {
                StringBuilder sql = new StringBuilder("CREATE TABLE " + TABLE + " (_id INTEGER PRIMARY KEY AUTOINCREMENT");
                for (String column : COLUMNS) {
                    if (!column.equals("_id")) sql.append(", ").append(column);
                }
                db.execSQL(sql.append(")").toString());
            }

            @Override
            public void onUpgrade(SQLiteDatabase db, int from, int to) {}
        };
        return true;
    }

    private static Target target(Uri uri) {
        List<String> segments = uri.getPathSegments();
        if (segments.size() < 2) return null;
        String volume = segments.get(0);
        List<String> rest = segments.subList(1, segments.size());
        String first = rest.get(0);
        int consumed;
        int type = 0;
        boolean downloads = false;
        String collection;
        if (first.equals("file") || first.equals("downloads")) {
            collection = first;
            downloads = first.equals("downloads");
            consumed = 1;
        } else if ((first.equals("images") || first.equals("video") || first.equals("audio"))
                && rest.size() >= 2 && rest.get(1).equals("media")) {
            collection = first + "/media";
            type = first.equals("images") ? IMAGE : first.equals("video") ? VIDEO : AUDIO;
            consumed = 2;
        } else {
            return null;
        }
        long id = -1;
        if (rest.size() == consumed + 1) {
            try {
                id = Long.parseLong(rest.get(consumed));
            } catch (NumberFormatException e) {
                return null;
            }
        } else if (rest.size() != consumed) {
            return null;
        }
        return new Target(volume, collection, type, downloads, id);
    }

    /** The rows a target and an app's selection name, as SQL with its arguments. */
    private static String where(Target target, String selection, List<String> args) {
        StringBuilder sql = new StringBuilder();
        if (target.mediaType != 0) sql.append("media_type=").append(target.mediaType);
        if (target.downloads) sql.append(sql.length() > 0 ? " AND " : "").append("is_download=1");
        if (target.id >= 0) {
            sql.append(sql.length() > 0 ? " AND " : "").append("_id=?");
            args.add(0, Long.toString(target.id));
        }
        if (selection != null && !selection.trim().isEmpty()) {
            sql.append(sql.length() > 0 ? " AND " : "").append('(').append(selection).append(')');
        }
        return sql.length() > 0 ? sql.toString() : null;
    }

    private static File root() {
        File root = Environment.getExternalStorageDirectory();
        return root != null ? root : new File("/sdcard");
    }

    private static String defaultDirectory(Target target, String mime) {
        if (target.downloads) return "Download/";
        int type = target.mediaType != 0 ? target.mediaType : typeOf(mime);
        return type == IMAGE ? "Pictures/" : type == VIDEO ? "Movies/" : type == AUDIO ? "Music/" : "Download/";
    }

    private static int typeOf(String mime) {
        if (mime == null) return 0;
        return mime.startsWith("image/") ? IMAGE : mime.startsWith("video/") ? VIDEO : mime.startsWith("audio/") ? AUDIO : 0;
    }

    /** name in dir, or "name (1).ext" and onwards when a file of that name exists, as MediaProvider names it. */
    private static File unique(File dir, String name) {
        File file = new File(dir, name);
        int dot = name.lastIndexOf('.');
        String stem = dot > 0 ? name.substring(0, dot) : name, ext = dot > 0 ? name.substring(dot) : "";
        for (int n = 1; file.exists() && n < 1000; n++) file = new File(dir, stem + " (" + n + ")" + ext);
        return file;
    }

    private static ContentValues known(ContentValues values) {
        ContentValues out = new ContentValues();
        if (values == null) return out;
        for (String key : values.keySet()) {
            if (KNOWN.contains(key) && !key.equals("_id")) out.putObject(key, values.get(key));
        }
        return out;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        Target target = target(uri);
        if (target == null || target.id >= 0) return null;
        ContentValues row = known(values);
        String name = row.getAsString("_display_name");
        if (name == null || name.isEmpty()) name = row.getAsString("title");
        if (name == null || name.isEmpty()) name = "file-" + System.currentTimeMillis();
        name = name.replace('/', '_');
        String mime = row.getAsString("mime_type");
        if (mime == null) {
            int dot = name.lastIndexOf('.');
            String guess = dot > 0 ? MimeTypeMap.getSingleton().getMimeTypeFromExtension(
                    name.substring(dot + 1).toLowerCase(Locale.ROOT)) : null;
            mime = guess != null ? guess : "application/octet-stream";
        }
        String relative = row.getAsString("relative_path");
        if (relative == null || relative.isEmpty()) relative = defaultDirectory(target, mime);
        if (!relative.endsWith("/")) relative += "/";
        File base = root();
        String data = row.getAsString("_data");
        File file = data != null && data.startsWith(base.getPath() + "/") ? new File(data)
                : unique(new File(base, relative), name);
        File dir = file.getParentFile();
        if (dir != null) dir.mkdirs();
        try {
            file.createNewFile();
        } catch (IOException e) {
            return null;
        }
        long now = System.currentTimeMillis();
        row.put("_data", file.getPath());
        row.put("_display_name", file.getName());
        if (!row.containsKey("title")) {
            int dot = file.getName().lastIndexOf('.');
            row.put("title", dot > 0 ? file.getName().substring(0, dot) : file.getName());
        }
        row.put("mime_type", mime);
        row.put("relative_path", relative);
        row.put("volume_name", "external_primary");
        row.put("media_type", target.mediaType != 0 ? target.mediaType : typeOf(mime));
        row.put("is_download", target.downloads ? 1 : 0);
        if (!row.containsKey("is_pending")) row.put("is_pending", 0);
        row.put("date_added", now / 1000);
        row.put("date_modified", now / 1000);
        row.put("_size", file.length());
        row.put("owner_package_name", getContext().getPackageName());
        row.put("bucket_display_name", dir != null ? dir.getName() : "");
        row.put("bucket_id", dir != null ? dir.getPath().toLowerCase(Locale.ROOT).hashCode() : 0);
        long id = mHelper.getWritableDatabase().insert(TABLE, null, row);
        if (id < 0) return null;
        mHelper.getWritableDatabase().execSQL("UPDATE " + TABLE + " SET generation_added=_id, generation_modified=_id WHERE _id=" + id);
        Uri item = ContentUris.withAppendedId(
                Uri.parse("content://media/" + target.volume + "/" + target.collection), id);
        getContext().getContentResolver().notifyChange(item, null);
        return item;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        Target target = target(uri);
        List<String> args = new ArrayList<>(selectionArgs != null ? Arrays.asList(selectionArgs) : java.util.Collections.<String>emptyList());
        String columns;
        if (projection == null || projection.length == 0) {
            columns = String.join(", ", COLUMNS);
        } else {
            List<String> out = new ArrayList<>();
            for (String column : projection) {
                if (column == null) continue;
                // A column MediaProvider has and this table does not answers null, as for a fresh file.
                out.add(KNOWN.contains(column) || !column.matches("[A-Za-z_][A-Za-z0-9_]*") ? column
                        : "NULL AS " + column);
            }
            columns = String.join(", ", out);
        }
        if (target == null) {
            // Thumbnails and other tables MediaProvider has: none here.
            return mHelper.getReadableDatabase().rawQuery("SELECT " + columns + " FROM " + TABLE + " WHERE 0", null);
        }
        String where = where(target, selection, args);
        String sql = "SELECT " + columns + " FROM " + TABLE + (where != null ? " WHERE " + where : "")
                + (sortOrder != null && !sortOrder.trim().isEmpty() ? " ORDER BY " + sortOrder : "");
        Cursor cursor = mHelper.getReadableDatabase().rawQuery(sql, args.toArray(new String[0]));
        cursor.setNotificationUri(getContext().getContentResolver(), uri);
        return cursor;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        Target target = target(uri);
        if (target == null) return 0;
        ContentValues row = known(values);
        row.remove("_data");
        List<String> args = new ArrayList<>(selectionArgs != null ? Arrays.asList(selectionArgs) : java.util.Collections.<String>emptyList());
        String where = where(target, selection, args);
        SQLiteDatabase db = mHelper.getWritableDatabase();
        int changed = 0;
        try (Cursor rows = db.rawQuery("SELECT _id, _data, _display_name, relative_path FROM " + TABLE
                + (where != null ? " WHERE " + where : ""), args.toArray(new String[0]))) {
            while (rows.moveToNext()) {
                ContentValues one = new ContentValues(row);
                String name = row.containsKey("_display_name") ? row.getAsString("_display_name") : rows.getString(2);
                String relative = row.containsKey("relative_path") ? row.getAsString("relative_path") : rows.getString(3);
                if (relative != null && !relative.endsWith("/")) relative += "/";
                if (name != null && relative != null
                        && (!name.equals(rows.getString(2)) || !relative.equals(rows.getString(3)))) {
                    File from = new File(rows.getString(1));
                    File to = unique(new File(root(), relative), name.replace('/', '_'));
                    if (to.getParentFile() != null) to.getParentFile().mkdirs();
                    if (from.renameTo(to)) {
                        one.put("_data", to.getPath());
                        one.put("_display_name", to.getName());
                        one.put("relative_path", relative);
                    }
                }
                one.put("date_modified", System.currentTimeMillis() / 1000);
                changed += db.update(TABLE, one, "_id=?", new String[] {rows.getString(0)});
            }
        }
        if (changed > 0) getContext().getContentResolver().notifyChange(uri, null);
        return changed;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        Target target = target(uri);
        if (target == null) return 0;
        List<String> args = new ArrayList<>(selectionArgs != null ? Arrays.asList(selectionArgs) : java.util.Collections.<String>emptyList());
        String where = where(target, selection, args);
        SQLiteDatabase db = mHelper.getWritableDatabase();
        int deleted = 0;
        try (Cursor rows = db.rawQuery("SELECT _id, _data FROM " + TABLE + (where != null ? " WHERE " + where : ""),
                args.toArray(new String[0]))) {
            while (rows.moveToNext()) {
                if (rows.getString(1) != null) new File(rows.getString(1)).delete();
                deleted += db.delete(TABLE, "_id=?", new String[] {rows.getString(0)});
            }
        }
        if (deleted > 0) getContext().getContentResolver().notifyChange(uri, null);
        return deleted;
    }

    @Override
    public String getType(Uri uri) {
        Target target = target(uri);
        if (target == null) return null;
        if (target.id < 0) return "vnd.android.cursor.dir/" + target.collection.replace('/', '_');
        try (Cursor row = mHelper.getReadableDatabase().rawQuery("SELECT mime_type FROM " + TABLE + " WHERE _id=?",
                new String[] {Long.toString(target.id)})) {
            return row.moveToFirst() ? row.getString(0) : null;
        }
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        Target target = target(uri);
        if (target == null || target.id < 0) throw new FileNotFoundException("No item at " + uri);
        final String id = Long.toString(target.id);
        String data;
        try (Cursor row = mHelper.getReadableDatabase().rawQuery("SELECT _data FROM " + TABLE + " WHERE _id=?",
                new String[] {id})) {
            data = row.moveToFirst() ? row.getString(0) : null;
        }
        if (data == null) throw new FileNotFoundException("No item at " + uri);
        final File file = new File(data);
        int flags = ParcelFileDescriptor.parseMode(mode);
        if ((flags & ParcelFileDescriptor.MODE_READ_ONLY) == flags) {
            return ParcelFileDescriptor.open(file, flags);
        }
        if (file.getParentFile() != null) file.getParentFile().mkdirs();
        // A write is followed by its size and time, as MediaProvider rescans a file it hands out for writing.
        try {
            return ParcelFileDescriptor.open(file, flags, closes(), e -> {
                ContentValues sized = new ContentValues();
                sized.put("_size", file.length());
                sized.put("date_modified", file.lastModified() / 1000);
                mHelper.getWritableDatabase().update(TABLE, sized, "_id=?", new String[] {id});
            });
        } catch (FileNotFoundException e) {
            throw e;
        } catch (IOException e) {
            throw new FileNotFoundException(file + ": " + e.getMessage());
        }
    }

    private synchronized Handler closes() {
        if (mCloses == null) {
            HandlerThread thread = new HandlerThread("westlake-media-close");
            thread.start();
            mCloses = new Handler(thread.getLooper());
        }
        return mCloses;
    }

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        // MediaStore.getVersion and getGeneration ask the provider by call().
        if ("get_version".equals(method)) {
            Bundle out = new Bundle();
            out.putString(Intent.EXTRA_TEXT, "westlake-local-1");
            return out;
        }
        if ("get_generation".equals(method)) {
            Bundle out = new Bundle();
            try (Cursor row = mHelper.getReadableDatabase().rawQuery("SELECT IFNULL(MAX(generation_modified), 0) FROM " + TABLE, null)) {
                out.putLong(Intent.EXTRA_INDEX, row.moveToFirst() ? row.getLong(0) : 0);
            }
            return out;
        }
        return super.call(method, arg, extras);
    }
}
