package adapter.activity;

import android.content.Intent;
import android.os.SystemClock;

import java.util.ArrayDeque;
import java.util.Iterator;

/**
 * The Intent an app passed to startActivity for one of its own activities, handed to that launch as
 * the app built it.
 *
 * The in-process launch goes out through OH's Want and comes back rebuilt from the Want's JSON,
 * which keeps strings and numbers but not Parcelable extras, and gives an action without a dot back
 * under OH's prefix. K-9's UpgradeDatabaseActivity lost the start intent it is handed (an Intent
 * extra) and its "upgrade_databases" action; once its database service finished it started a null
 * intent and the app died. AOSP hands the started activity a copy of the caller's Intent, taken
 * when startActivity is called; this keeps that copy for the launch to take.
 */
final class PendingLaunchIntents {
    private static final long MAX_AGE_MS = 10_000;

    /** A pending launch: the Intent, and the caller waiting for the activity's result, if any. */
    static final class Entry {
        final String className;
        final Intent intent;
        final ActivityResults.Request request;
        final long time;

        Entry(String className, Intent intent, ActivityResults.Request request) {
            this.className = className;
            this.intent = intent;
            this.request = request;
            this.time = SystemClock.uptimeMillis();
        }
    }

    private static final ArrayDeque<Entry> sPending = new ArrayDeque<>();

    private PendingLaunchIntents() {}

    /**
     * Called as startActivity sends the start on: a copy, since the caller may change its Intent, and
     * the caller's request when it asked for a result (startActivityForResult).
     */
    static synchronized void offer(String className, Intent intent, ActivityResults.Request request) {
        if (className == null || intent == null) return;
        sPending.addLast(new Entry(className, new Intent(intent), request));
    }

    /** The oldest pending launch for the activity class, or null; entries older than ten seconds expire. */
    static synchronized Entry take(String className) {
        long now = SystemClock.uptimeMillis();
        for (Iterator<Entry> it = sPending.iterator(); it.hasNext(); ) {
            Entry entry = it.next();
            if (now - entry.time > MAX_AGE_MS) {
                it.remove();
            } else if (entry.className.equals(className)) {
                it.remove();
                return entry;
            }
        }
        return null;
    }
}
