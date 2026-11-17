package org.westlake.syncprobe;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;

/** Local provider required by AbstractThreadedSyncAdapter; stores no user data. */
public final class ProbeProvider extends ContentProvider {
    @Override public boolean onCreate() { return true; }
    @Override public String getType(Uri uri) { return "vnd.android.cursor.dir/vnd.westlake.syncprobe"; }
    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        return new MatrixCursor(projection == null ? new String[] {"_id"} : projection);
    }
    @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
    @Override public int delete(Uri uri, String selection, String[] args) { throw new UnsupportedOperationException(); }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) { throw new UnsupportedOperationException(); }
}
