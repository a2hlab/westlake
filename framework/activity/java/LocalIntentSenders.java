package adapter.activity;

import android.app.ActivityManager;
import android.content.IIntentSender;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;

import java.lang.reflect.Proxy;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

/**
 * PendingIntents for a directly launched app, answered in process.
 *
 * Every PendingIntent.getActivity/getBroadcast/getService asks the activity manager for an
 * IIntentSender, and direct launch's activity manager answered null, so the factory returned a null
 * PendingIntent. Apps pass it straight on: Element's MainActivity.onResume hands it to
 * AlarmManager.cancel, which throws on null.
 *
 * What the activity manager guarantees at creation is kept: two requests with the same type,
 * package, request code and filter-equal intent get the same sender (so a later cancel matches),
 * FLAG_NO_CREATE answers only an existing one, FLAG_CANCEL_CURRENT replaces it and
 * FLAG_UPDATE_CURRENT refreshes its extras. Sending is accepted and not delivered, the same
 * policy as the in-process alarm service: nothing here reaches OH's WantAgent.
 */
public final class LocalIntentSenders {
    private static final int FLAG_NO_CREATE = 1 << 29;
    private static final int FLAG_CANCEL_CURRENT = 1 << 28;
    private static final int FLAG_UPDATE_CURRENT = 1 << 27;
    private static final int FLAG_IMMUTABLE = 1 << 26;
    private static final String SENDER_DESCRIPTOR = "android.content.IIntentSender";

    private static final Map<Key, Record> sByKey = new HashMap<>();
    private static final Map<IBinder, Record> sByBinder = new HashMap<>();

    private LocalIntentSenders() {}

    private static final class Key {
        final int type;
        final String packageName;
        final int requestCode;
        final Intent intent;

        Key(int type, String packageName, int requestCode, Intent intent) {
            this.type = type;
            this.packageName = packageName;
            this.requestCode = requestCode;
            this.intent = intent;
        }

        @Override
        public boolean equals(Object o) {
            if (!(o instanceof Key)) return false;
            Key k = (Key) o;
            return type == k.type && requestCode == k.requestCode
                    && java.util.Objects.equals(packageName, k.packageName)
                    && (intent == null ? k.intent == null : intent.filterEquals(k.intent));
        }

        @Override
        public int hashCode() {
            return Arrays.hashCode(new Object[] {type, packageName, requestCode,
                    intent == null ? 0 : intent.filterHashCode()});
        }
    }

    private static final class Record {
        final Key key;
        final Binder binder = new Binder();
        final boolean immutable;
        Intent[] intents;

        Record(Key key, Intent[] intents, boolean immutable) {
            this.key = key;
            this.intents = intents;
            this.immutable = immutable;
        }
    }

    /** IActivityManager.getIntentSender / getIntentSenderWithFeature. */
    public static synchronized IIntentSender get(int type, String packageName, int requestCode,
            Intent[] intents, int flags) {
        Intent first = intents != null && intents.length > 0 ? intents[0] : null;
        Key key = new Key(type, packageName, requestCode, first);
        Record record = sByKey.get(key);
        if (record != null && (flags & FLAG_CANCEL_CURRENT) != 0) {
            remove(record);
            record = null;
        }
        if (record == null) {
            if ((flags & FLAG_NO_CREATE) != 0) return null;
            record = new Record(key, intents, (flags & FLAG_IMMUTABLE) != 0);
            attach(record);
            sByKey.put(key, record);
            sByBinder.put(record.binder, record);
        } else if ((flags & FLAG_UPDATE_CURRENT) != 0 && first != null) {
            record.intents = intents;
        }
        return (IIntentSender) record.binder.queryLocalInterface(SENDER_DESCRIPTOR);
    }

    /** IActivityManager.cancelIntentSender. */
    public static synchronized void cancel(IIntentSender sender) {
        Record record = find(sender);
        if (record != null) remove(record);
    }

    /** IActivityManager.getInfoForIntentSender. */
    public static synchronized ActivityManager.PendingIntentInfo info(IIntentSender sender) {
        Record record = find(sender);
        if (record == null) return null;
        return new ActivityManager.PendingIntentInfo(record.key.packageName,
                android.os.Process.myUid(), record.immutable, record.key.type);
    }

    /** IActivityManager.getIntentForIntentSender. */
    public static synchronized Intent intent(IIntentSender sender) {
        Record record = find(sender);
        return record == null || record.intents == null || record.intents.length == 0
                ? null : new Intent(record.intents[0]);
    }

    /** IActivityManager.sendIntentSender: accepted, not delivered; 0 is success to PendingIntent. */
    public static int send(IIntentSender sender) {
        Record record;
        synchronized (LocalIntentSenders.class) {
            record = find(sender);
        }
        System.err.println("[WESTLAKE-PENDING-INTENT] send accepted, not delivered: "
                + (record != null && record.intents != null && record.intents.length > 0
                        ? record.intents[0] : "unknown sender"));
        return record != null ? 0 : -96; // ActivityManager.START_CANCELED for a cancelled sender
    }

    private static Record find(IIntentSender sender) {
        return sender == null ? null : sByBinder.get(sender.asBinder());
    }

    private static void remove(Record record) {
        sByKey.remove(record.key);
        sByBinder.remove(record.binder);
    }

    private static void attach(Record record) {
        Object local = Proxy.newProxyInstance(LocalIntentSenders.class.getClassLoader(),
                new Class<?>[] {IIntentSender.class}, (self, method, args) -> {
                    switch (method.getName()) {
                        case "asBinder": return record.binder;
                        case "hashCode": return System.identityHashCode(self);
                        case "equals": return args != null && args.length == 1 && args[0] == self;
                        case "toString": return "LocalIntentSender{" + record.key.packageName + "}";
                        default:
                            // send(): the oneway entry point, reached when something other than
                            // PendingIntent.send delivers through the binder itself.
                            send((IIntentSender) self);
                            return null;
                    }
                });
        record.binder.attachInterface((IInterface) local, SENDER_DESCRIPTOR);
    }
}
