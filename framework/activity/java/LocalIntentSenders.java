package adapter.activity;

import android.app.ActivityManager;
import android.app.ActivityThread;
import android.app.Application;
import android.content.IIntentReceiver;
import android.content.IIntentSender;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.UserHandle;
import android.util.Log;

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
 * package, request code, flags and filter-equal intent get the same sender (so a later cancel
 * matches), FLAG_NO_CREATE answers only an existing one, FLAG_CANCEL_CURRENT replaces it and
 * FLAG_UPDATE_CURRENT refreshes its extras.
 *
 * Sending delivers in process, as PendingIntentRecord sends: the intent the sender holds, with what
 * the caller fills in unless it is immutable, goes to the app's own receivers (broadcast), services
 * (service, foreground service) or activities (activity, each in turn for a stack); a one-shot
 * sender is used up. The alarms the app sets are delivered this way (LocalAlarms). Nothing here
 * reaches OH's WantAgent, so a sender handed to OH (a notification's content intent) still is not
 * sent from there, and an activity result sender (createPendingResult) is not delivered.
 */
public final class LocalIntentSenders {
    private static final String TAG = "[WESTLAKE-PENDING-INTENT]";
    private static final int FLAG_ONE_SHOT = 1 << 30;
    private static final int FLAG_NO_CREATE = 1 << 29;
    private static final int FLAG_CANCEL_CURRENT = 1 << 28;
    private static final int FLAG_UPDATE_CURRENT = 1 << 27;
    private static final int FLAG_IMMUTABLE = 1 << 26;
    private static final String SENDER_DESCRIPTOR = "android.content.IIntentSender";

    private static final Map<Key, Record> sByKey = new HashMap<>();

    private LocalIntentSenders() {}

    /**
     * PendingIntentRecord.Key: the flags it was asked with, less the three that only steer the
     * request, are part of what identifies a sender, and the intent is the last one asked for.
     */
    private static final class Key {
        final int type;
        final String packageName;
        final int requestCode;
        final Intent intent;
        final int flags;

        Key(int type, String packageName, int requestCode, Intent intent, int flags) {
            this.type = type;
            this.packageName = packageName;
            this.requestCode = requestCode;
            this.intent = intent;
            this.flags = flags;
        }

        @Override
        public boolean equals(Object o) {
            if (!(o instanceof Key)) return false;
            Key k = (Key) o;
            return type == k.type && requestCode == k.requestCode && flags == k.flags
                    && java.util.Objects.equals(packageName, k.packageName)
                    && (intent == null ? k.intent == null : intent.filterEquals(k.intent));
        }

        @Override
        public int hashCode() {
            return Arrays.hashCode(new Object[] {type, packageName, requestCode, flags,
                    intent == null ? 0 : intent.filterHashCode()});
        }
    }

    /**
     * A Westlake call on a sender's binder, answered in the process that made the sender: the package that made
     * it, whether it is immutable, and its type. On Android the activity manager knows that of every sender
     * (getInfoForIntentSender); here the process that made one does. A sender can reach a process through
     * another (an app's sender passed on by Play services' main process to its persistent one), and the binder
     * call goes back to its maker through each of them. Inside the range of user codes, apart from
     * IIntentSender's own.
     */
    static final int INFO_TRANSACTION = 0x574c49;

    /** A sender's binder: it leads back to its record for as long as a PendingIntent holds it. */
    private static final class SenderBinder extends Binder {
        final Record record;

        SenderBinder(Record record) {
            this.record = record;
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code != INFO_TRANSACTION) return super.onTransact(code, data, reply, flags);
            data.enforceInterface(SENDER_DESCRIPTOR);
            reply.writeNoException();
            reply.writeString(record.key.packageName);
            reply.writeBoolean(record.immutable);
            reply.writeInt(record.key.type);
            return true;
        }
    }

    /** What a sender made in another process says of itself (INFO_TRANSACTION). */
    static final class Facts {
        final String packageName;
        final boolean immutable;
        final int type;

        Facts(String packageName, boolean immutable, int type) {
            this.packageName = packageName;
            this.immutable = immutable;
            this.type = type;
        }
    }

    /** Asks a sender from another process who made it; null for a sender of this process or one that can't say. */
    static Facts ask(IIntentSender sender) {
        IBinder binder = sender == null ? null : sender.asBinder();
        if (binder == null || binder instanceof SenderBinder) return null;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(SENDER_DESCRIPTOR);
            if (!binder.transact(INFO_TRANSACTION, data, reply, 0)) return null;
            reply.readException();
            String packageName = reply.readString();
            boolean immutable = reply.readBoolean();
            int type = reply.readInt();
            return packageName == null ? null : new Facts(packageName, immutable, type);
        } catch (RemoteException | RuntimeException e) {
            return null;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    private static final class Record {
        final Key key;
        final Binder binder = new SenderBinder(this);
        final boolean immutable;
        Intent[] intents;
        // Cancelled, replaced or used up: like the activity manager's record, it still answers what
        // it was (PendingIntent asks before it sends) and sends nothing.
        boolean cancelled;

        Record(Key key, Intent[] intents) {
            this.key = key;
            this.intents = intents;
            this.immutable = (key.flags & FLAG_IMMUTABLE) != 0;
        }

        /** The intent the sender delivers: the last one asked for, as the activity manager keeps it. */
        Intent last() {
            return intents != null && intents.length > 0 ? intents[intents.length - 1] : null;
        }
    }

    /** IActivityManager.getIntentSender / getIntentSenderWithFeature. */
    public static synchronized IIntentSender get(int type, String packageName, int requestCode,
            Intent[] intents, int flags) {
        Intent last = intents != null && intents.length > 0 ? intents[intents.length - 1] : null;
        Key key = new Key(type, packageName, requestCode, last,
                flags & ~(FLAG_NO_CREATE | FLAG_CANCEL_CURRENT | FLAG_UPDATE_CURRENT));
        Record record = sByKey.get(key);
        if (record != null && (flags & FLAG_CANCEL_CURRENT) != 0) {
            remove(record);
            record = null;
        }
        if (record == null) {
            if ((flags & FLAG_NO_CREATE) != 0) return null;
            record = new Record(key, intents);
            attach(record);
            sByKey.put(key, record);
        } else if ((flags & FLAG_UPDATE_CURRENT) != 0 && last != null) {
            record.intents = intents;
        }
        return (IIntentSender) record.binder.queryLocalInterface(SENDER_DESCRIPTOR);
    }

    /** IActivityManager.cancelIntentSender. */
    public static synchronized void cancel(IIntentSender sender) {
        Record record = find(sender);
        if (record != null) remove(record);
    }

    /**
     * IActivityManager.getInfoForIntentSender. Another process's sender is asked outside this class's lock: the
     * question is a binder call to its maker.
     */
    public static ActivityManager.PendingIntentInfo info(IIntentSender sender) {
        synchronized (LocalIntentSenders.class) {
            Record record = find(sender);
            if (record != null) {
                return new ActivityManager.PendingIntentInfo(record.key.packageName,
                        android.os.Process.myUid(), record.immutable, record.key.type);
            }
        }
        return PlayServicesChannel.remoteSenderInfo(sender);
    }

    /** IActivityManager.getIntentForIntentSender. */
    public static synchronized Intent intent(IIntentSender sender) {
        Record record = find(sender);
        return record == null || record.last() == null ? null : new Intent(record.last());
    }

    /**
     * IActivityManager.sendIntentSender, as PendingIntentRecord.sendInner sends: returns 0, or
     * ActivityManager.START_CANCELED for a sender that was cancelled or used up, which PendingIntent
     * throws as CanceledException. The finished receiver (PendingIntent.OnFinished) hears back once
     * the intent is on its way, for a broadcast with the result its receivers leave.
     */
    public static int send(IIntentSender sender, int code, Intent fillIn, String resolvedType,
            IIntentReceiver finishedReceiver) {
        Record record;
        Intent[] intents;
        synchronized (LocalIntentSenders.class) {
            record = find(sender);
            if (record == null && PlayServicesChannel.sendRemoteSender(sender, code, fillIn, resolvedType)) {
                return 0;
            }
            if (record == null || record.cancelled) {
                System.err.println(TAG + " send refused: the sender was cancelled");
                return ActivityManager.START_CANCELED;
            }
            if ((record.key.flags & FLAG_ONE_SHOT) != 0) remove(record);
            intents = record.intents;
        }
        Intent intent = record.last() != null ? new Intent(record.last()) : new Intent();
        if (!record.immutable && fillIn != null) {
            int changes = intent.fillIn(fillIn, record.key.flags);
            if ((changes & Intent.FILL_IN_DATA) == 0) resolvedType = null;
        } else {
            resolvedType = null;
        }
        int userId = UserHandle.myUserId();
        System.err.println(TAG + " send " + typeName(record.key.type) + " " + intent);
        try {
            switch (record.key.type) {
                case ActivityManager.INTENT_SENDER_BROADCAST:
                    // Ordered when a result is asked for; LocalBroadcasts hands the final receiver
                    // its result.
                    LocalBroadcasts.broadcast(intent, resolvedType, finishedReceiver, code, null, null,
                            finishedReceiver != null, userId);
                    return 0;
                case ActivityManager.INTENT_SENDER_SERVICE:
                case ActivityManager.INTENT_SENDER_FOREGROUND_SERVICE:
                    if (InProcessServiceBinder.start(intent) == null) {
                        System.err.println(TAG + " no service of the app's for " + intent);
                    }
                    break;
                case ActivityManager.INTENT_SENDER_ACTIVITY:
                    startActivities(intents, intent);
                    break;
                default:
                    System.err.println(TAG + " not delivered: " + typeName(record.key.type) + " senders are not sent");
                    break;
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "sending " + intent + " failed", e);
        }
        if (finishedReceiver != null) {
            try {
                finishedReceiver.performReceive(new Intent(intent), 0, null, null, false, false, userId);
            } catch (Throwable t) {
                Log.w(TAG, "the finished receiver of " + intent + " failed", t);
            }
        }
        return 0;
    }

    /**
     * An activity sender's activities, started from the application as the activity manager starts
     * them: in turn, so that a stack (PendingIntent.getActivities) ends with its last one on top.
     */
    private static void startActivities(Intent[] intents, Intent last) {
        Application app = ActivityThread.currentApplication();
        if (app == null) return;
        int count = intents != null && intents.length > 1 ? intents.length : 1;
        for (int i = 0; i < count; i++) {
            Intent start = i == count - 1 ? last : new Intent(intents[i]);
            start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);   // from outside an activity
            app.startActivity(start);
        }
    }

    private static String typeName(int type) {
        switch (type) {
            case ActivityManager.INTENT_SENDER_BROADCAST: return "broadcast";
            case ActivityManager.INTENT_SENDER_ACTIVITY: return "activity";
            case ActivityManager.INTENT_SENDER_ACTIVITY_RESULT: return "activity result";
            case ActivityManager.INTENT_SENDER_SERVICE: return "service";
            case ActivityManager.INTENT_SENDER_FOREGROUND_SERVICE: return "foreground service";
            default: return "type " + type;
        }
    }

    private static Record find(IIntentSender sender) {
        IBinder binder = sender == null ? null : sender.asBinder();
        return binder instanceof SenderBinder ? ((SenderBinder) binder).record : null;
    }

    private static void remove(Record record) {
        record.cancelled = true;
        if (sByKey.get(record.key) == record) sByKey.remove(record.key);
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
                            // send(code, intent, resolvedType, whitelistToken, finishedReceiver,
                            // requiredPermission, options): the oneway entry point, reached when
                            // something other than PendingIntent.send delivers through the binder.
                            if ("send".equals(method.getName()) && args != null && args.length > 4) {
                                send((IIntentSender) self, (Integer) args[0], (Intent) args[1],
                                        (String) args[2], (IIntentReceiver) args[4]);
                            }
                            return null;
                    }
                });
        record.binder.attachInterface((IInterface) local, SENDER_DESCRIPTOR);
    }
}
