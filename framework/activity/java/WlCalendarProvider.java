package adapter.activity;

import android.content.ContentProvider;
import android.content.ContentUris;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import android.net.Uri;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * An in-process calendar provider ("content://com.android.calendar"), holding the app's own calendars
 * and events.
 *
 * Every Android device has CalendarProvider, and an app with the calendar permission adds, changes and
 * removes events through it. Here no provider had the authority, so ContentResolver.delete threw
 * "Unknown URL": Shortplay died when its reminder switch removed an event.
 *
 * The tables are CalendarContract's calendars, events, reminders, attendees and extendedproperties,
 * created with their usual columns; a column an app writes that is not one of them is added, and one
 * it asks for that no row has answers null. Instances answers instances/when/<begin>/<end> from the
 * events table (a repeating event is one instance). There are no calendars until the app adds one, as
 * on a device with no account. Removing an event removes its reminders and attendees. What is kept
 * here is the app's own; OpenHarmony's calendar does not see it.
 *
 * AppSchedulerBridge appends its ProviderInfo at bind, as for WlSettingsProvider.
 */
public class WlCalendarProvider extends ContentProvider {
    private static final Map<String, String[]> TABLES = new HashMap<>();
    static {
        TABLES.put("calendars", new String[] {"account_name", "account_type", "name", "calendar_displayName",
                "calendar_color", "calendar_color_index", "calendar_access_level", "ownerAccount", "visible",
                "sync_events", "calendar_timezone", "isPrimary", "allowedReminders", "allowedAvailability",
                "allowedAttendeeTypes", "maxReminders", "canOrganizerRespond", "canModifyTimeZone", "deleted",
                "dirty", "_sync_id", "mutators"});
        TABLES.put("events", new String[] {"calendar_id", "title", "description", "eventLocation", "eventColor",
                "eventColor_index", "displayColor", "dtstart", "dtend", "duration", "eventTimezone",
                "eventEndTimezone", "allDay", "rrule", "rdate", "exrule", "exdate", "lastDate", "hasAlarm",
                "hasAttendeeData", "hasExtendedProperties", "status", "selfAttendeeStatus", "availability",
                "accessLevel", "organizer", "isOrganizer", "guestsCanModify", "guestsCanInviteOthers",
                "guestsCanSeeGuests", "original_id", "original_sync_id", "originalInstanceTime", "originalAllDay",
                "customAppPackage", "customAppUri", "uid2445", "deleted", "dirty", "_sync_id", "mutators"});
        TABLES.put("reminders", new String[] {"event_id", "minutes", "method"});
        TABLES.put("attendees", new String[] {"event_id", "attendeeName", "attendeeEmail", "attendeeRelationship",
                "attendeeType", "attendeeStatus", "attendeeIdentity", "attendeeIdNamespace"});
        TABLES.put("extendedproperties", new String[] {"event_id", "name", "value"});
    }

    private SQLiteOpenHelper mHelper;

    @Override
    public boolean onCreate() {
        mHelper = new SQLiteOpenHelper(getContext(),
                new File(getContext().getDataDir(), "no_backup/westlake-calendar.db").getPath(), null, 1) {
            @Override
            public void onCreate(SQLiteDatabase db) {
                for (Map.Entry<String, String[]> table : TABLES.entrySet()) {
                    StringBuilder sql = new StringBuilder("CREATE TABLE " + table.getKey()
                            + " (_id INTEGER PRIMARY KEY AUTOINCREMENT");
                    for (String column : table.getValue()) sql.append(", ").append(column);
                    db.execSQL(sql.append(")").toString());
                }
            }

            @Override
            public void onUpgrade(SQLiteDatabase db, int from, int to) {}
        };
        return true;
    }

    /** The table a URI names and the row id it names (-1 for none), or null for a path not kept here. */
    private static Object[] target(Uri uri) {
        List<String> segments = uri.getPathSegments();
        if (segments.isEmpty() || !TABLES.containsKey(segments.get(0))) return null;
        if (segments.size() == 1) return new Object[] {segments.get(0), -1L};
        if (segments.size() == 2) {
            try {
                return new Object[] {segments.get(0), Long.parseLong(segments.get(1))};
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return null;
    }

    private Set<String> columns(SQLiteDatabase db, String table) {
        Set<String> out = new HashSet<>();
        try (Cursor info = db.rawQuery("PRAGMA table_info(" + table + ")", null)) {
            while (info.moveToNext()) out.add(info.getString(1));
        }
        return out;
    }

    /** values with a column added for each key the table lacks, as the provider keeps what an app writes. */
    private ContentValues fit(SQLiteDatabase db, String table, ContentValues values) {
        ContentValues out = new ContentValues();
        if (values == null) return out;
        Set<String> have = columns(db, table);
        for (String key : values.keySet()) {
            if (key.equals("_id") || !key.matches("[A-Za-z_][A-Za-z0-9_]*")) continue;
            if (!have.contains(key)) {
                db.execSQL("ALTER TABLE " + table + " ADD COLUMN " + key);
                have.add(key);
            }
            out.putObject(key, values.get(key));
        }
        return out;
    }

    private static String where(long id, String selection, List<String> args) {
        StringBuilder sql = new StringBuilder();
        if (id >= 0) {
            sql.append("_id=?");
            args.add(0, Long.toString(id));
        }
        if (selection != null && !selection.trim().isEmpty()) {
            sql.append(sql.length() > 0 ? " AND " : "").append('(').append(selection).append(')');
        }
        return sql.length() > 0 ? sql.toString() : null;
    }

    private String projection(SQLiteDatabase db, String table, String[] projection) {
        if (projection == null || projection.length == 0) return "*";
        Set<String> have = columns(db, table);
        List<String> out = new ArrayList<>();
        for (String column : projection) {
            if (column == null) continue;
            out.add(have.contains(column) || !column.matches("[A-Za-z_][A-Za-z0-9_]*") ? column : "NULL AS " + column);
        }
        return String.join(", ", out);
    }

    private static List<String> args(String[] selectionArgs) {
        return new ArrayList<>(selectionArgs != null ? Arrays.asList(selectionArgs)
                : java.util.Collections.<String>emptyList());
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        Object[] target = target(uri);
        if (target == null || (long) target[1] >= 0) return null;
        String table = (String) target[0];
        SQLiteDatabase db = mHelper.getWritableDatabase();
        ContentValues row = fit(db, table, values);
        if (table.equals("calendars")) {
            // A sync adapter names the account in the URI, not the values.
            String account = uri.getQueryParameter("account_name"), type = uri.getQueryParameter("account_type");
            if (account != null && !row.containsKey("account_name")) row.put("account_name", account);
            if (type != null && !row.containsKey("account_type")) row.put("account_type", type);
            if (!row.containsKey("visible")) row.put("visible", 1);
            if (!row.containsKey("sync_events")) row.put("sync_events", 1);
        }
        if (!row.containsKey("deleted") && columns(db, table).contains("deleted")) row.put("deleted", 0);
        long id = db.insert(table, null, row);
        if (id < 0) return null;
        Uri item = ContentUris.withAppendedId(Uri.parse("content://com.android.calendar/" + table), id);
        getContext().getContentResolver().notifyChange(item, null);
        return item;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        SQLiteDatabase db = mHelper.getReadableDatabase();
        List<String> segments = uri.getPathSegments();
        String order = sortOrder != null && !sortOrder.trim().isEmpty() ? " ORDER BY " + sortOrder : "";
        if (segments.size() == 4 && segments.get(0).equals("instances") && segments.get(1).equals("when")) {
            // Each event an instance, its begin and end the event's; event_id names it.
            List<String> args = args(selectionArgs);
            args.add(0, segments.get(3));
            args.add(1, segments.get(2));
            String instances = "(SELECT *, _id AS event_id, dtstart AS \"begin\", IFNULL(dtend, dtstart) AS \"end\" FROM events"
                    + " WHERE IFNULL(deleted, 0)=0 AND dtstart < ? AND IFNULL(dtend, dtstart) >= ?)";
            // begin and end are SQL keywords: the columns asked for by name are quoted.
            List<String> asked = new ArrayList<>();
            if (projection != null) {
                for (String column : projection) {
                    if (column != null) asked.add(column.matches("[A-Za-z_][A-Za-z0-9_]*") ? "\"" + column + "\"" : column);
                }
            }
            String sql = "SELECT " + (asked.isEmpty() ? "*" : String.join(", ", asked))
                    + " FROM " + instances + (selection != null && !selection.trim().isEmpty() ? " WHERE (" + selection + ")" : "")
                    + order;
            return db.rawQuery(sql, args.toArray(new String[0]));
        }
        Object[] target = target(uri);
        if (target == null) {
            return db.rawQuery("SELECT NULL AS _id WHERE 0", null);
        }
        String table = (String) target[0];
        List<String> args = args(selectionArgs);
        String where = where((long) target[1], selection, args);
        Cursor cursor = db.rawQuery("SELECT " + projection(db, table, projection) + " FROM " + table
                + (where != null ? " WHERE " + where : "") + order, args.toArray(new String[0]));
        cursor.setNotificationUri(getContext().getContentResolver(), uri);
        return cursor;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        Object[] target = target(uri);
        if (target == null) return 0;
        String table = (String) target[0];
        SQLiteDatabase db = mHelper.getWritableDatabase();
        ContentValues row = fit(db, table, values);
        if (row.size() == 0) return 0;
        List<String> args = args(selectionArgs);
        int changed = db.update(table, row, where((long) target[1], selection, args), args.toArray(new String[0]));
        if (changed > 0) getContext().getContentResolver().notifyChange(uri, null);
        return changed;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        Object[] target = target(uri);
        if (target == null) return 0;
        String table = (String) target[0];
        SQLiteDatabase db = mHelper.getWritableDatabase();
        List<String> args = args(selectionArgs);
        String where = where((long) target[1], selection, args);
        List<String> ids = new ArrayList<>();
        try (Cursor rows = db.rawQuery("SELECT _id FROM " + table + (where != null ? " WHERE " + where : ""),
                args.toArray(new String[0]))) {
            while (rows.moveToNext()) ids.add(rows.getString(0));
        }
        for (String id : ids) {
            db.delete(table, "_id=?", new String[] {id});
            if (table.equals("events")) {
                for (String child : new String[] {"reminders", "attendees", "extendedproperties"}) {
                    db.delete(child, "event_id=?", new String[] {id});
                }
            } else if (table.equals("calendars")) {
                try (Cursor events = db.rawQuery("SELECT _id FROM events WHERE calendar_id=?", new String[] {id})) {
                    while (events.moveToNext()) {
                        for (String child : new String[] {"reminders", "attendees", "extendedproperties"}) {
                            db.delete(child, "event_id=?", new String[] {events.getString(0)});
                        }
                    }
                }
                db.delete("events", "calendar_id=?", new String[] {id});
            }
        }
        if (!ids.isEmpty()) getContext().getContentResolver().notifyChange(uri, null);
        return ids.size();
    }

    @Override
    public String getType(Uri uri) {
        Object[] target = target(uri);
        if (target == null) return null;
        String table = (String) target[0];
        return ((long) target[1] >= 0 ? "vnd.android.cursor.item/" : "vnd.android.cursor.dir/") + table;
    }
}
