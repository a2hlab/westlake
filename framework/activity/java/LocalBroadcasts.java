package adapter.activity;

import android.app.Application;
import android.app.ActivityThread;
import android.content.IIntentReceiver;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ResolveInfo;
import android.os.BatteryManager;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;

import java.util.Collections;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * Broadcasts within the app's own process, for direct launch.
 *
 * With no system_server, the direct-launch IActivityManager answered registerReceiver with null and
 * dropped every sendBroadcast: a receiver an app registered never heard the app's own broadcasts (a
 * service telling its activity what it is playing), and a sticky query such as
 * registerReceiver(null, new IntentFilter(ACTION_BATTERY_CHANGED)) -- how SDKs read the battery
 * level -- came back null. Here registrations are kept in process, the app's broadcasts go to the
 * ones whose filter matches, and BATTERY_CHANGED is answered sticky.
 *
 * Delivery goes through the receiver's IIntentReceiver (LoadedApk's dispatcher), which posts
 * onReceive to the handler the receiver was registered with, as for a broadcast from the system.
 * A broadcast to one of the app's manifest receivers, by name or within its package, runs that
 * receiver in process (ManifestReceivers).
 */
public final class LocalBroadcasts {
    private static final String TAG = "OH_LocalBroadcasts";

    private static final class Registration {
        final IIntentReceiver receiver;
        final IntentFilter filter;

        Registration(IIntentReceiver receiver, IntentFilter filter) {
            this.receiver = receiver;
            this.filter = filter;
        }
    }

    private static final CopyOnWriteArrayList<Registration> sRegistrations = new CopyOnWriteArrayList<>();

    private LocalBroadcasts() {}

    /** registerReceiver: keeps the registration; answers the sticky broadcast the filter matches. */
    public static Intent register(IIntentReceiver receiver, IntentFilter filter) {
        if (receiver != null && filter != null) sRegistrations.add(new Registration(receiver, filter));
        return filter != null ? sticky(filter) : null;
    }

    public static void unregister(IIntentReceiver receiver) {
        if (receiver == null) return;
        IBinder binder = receiver.asBinder();
        sRegistrations.removeIf(r -> r.receiver.asBinder() == binder);
    }

    /**
     * BATTERY_CHANGED as BatteryService sends it, for the same idle, fully charged phone the
     * battery properties service reports (LocalServiceBinders.batteryProperties).
     */
    private static Intent sticky(IntentFilter filter) {
        if (!filter.hasAction(Intent.ACTION_BATTERY_CHANGED)) return null;
        Intent battery = new Intent(Intent.ACTION_BATTERY_CHANGED);
        battery.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY | Intent.FLAG_RECEIVER_REPLACE_PENDING);
        battery.putExtra(BatteryManager.EXTRA_PRESENT, true);
        battery.putExtra(BatteryManager.EXTRA_LEVEL, 100);
        battery.putExtra(BatteryManager.EXTRA_SCALE, 100);
        battery.putExtra(BatteryManager.EXTRA_STATUS, BatteryManager.BATTERY_STATUS_FULL);
        battery.putExtra(BatteryManager.EXTRA_HEALTH, BatteryManager.BATTERY_HEALTH_GOOD);
        battery.putExtra(BatteryManager.EXTRA_PLUGGED, BatteryManager.BATTERY_PLUGGED_AC);
        battery.putExtra(BatteryManager.EXTRA_TECHNOLOGY, "Li-ion");
        battery.putExtra(BatteryManager.EXTRA_TEMPERATURE, 250);
        battery.putExtra(BatteryManager.EXTRA_VOLTAGE, 4200);
        return battery;
    }

    /** A broadcast for the receivers registered in this process, from another process of the app (DELIVER_REGISTERED). */
    static int deliverRegistered(Intent intent, int resultCode, String resultData, Bundle map) {
        return deliverRegistered(intent, resultCode, resultData, map, false, android.os.UserHandle.myUserId());
    }

    private static int deliverRegistered(Intent intent, int resultCode, String resultData, Bundle map,
            boolean ordered, int userId) {
        Application app = ActivityThread.currentApplication();
        int delivered = 0;
        for (Registration r : sRegistrations) {
            int match = r.filter.match(app != null ? app.getContentResolver() : null, intent, true, TAG);
            if (match < 0) continue;
            try {
                r.receiver.performReceive(new Intent(intent), resultCode, resultData, map, ordered, false, userId);
                delivered++;
            } catch (Throwable t) {
                Log.w(TAG, "delivery of " + intent.getAction() + " failed", t);
            }
        }
        return delivered;
    }

    /**
     * sendBroadcast / sendOrderedBroadcast from the app: each of its registered receivers whose
     * filter matches gets it, then the manifest receivers it reaches, then the ordered broadcast's
     * final receiver (resultTo) with the result. Returns ActivityManager.BROADCAST_SUCCESS.
     */
    public static int broadcast(Intent intent, String resolvedType, IIntentReceiver resultTo, int resultCode,
            String resultData, Bundle map, boolean ordered, int userId) {
        if (intent == null) return 0;
        if (PlayServicesChannel.deliverToClient(intent, resolvedType, resultTo, resultCode, resultData, map, ordered)) {
            return 0;
        }
        Application app = ActivityThread.currentApplication();
        String own = app != null ? app.getPackageName() : null;
        boolean forUs = intent.getPackage() == null || intent.getPackage().equals(own);
        if (intent.getComponent() != null) forUs = intent.getComponent().getPackageName().equals(own);
        int delivered = 0;
        if (forUs && intent.getComponent() == null) {
            delivered += deliverRegistered(intent, resultCode, resultData, map, ordered, userId);
            // The receivers registered in the app's other processes (ADR-0002) get it too, as the activity
            // manager delivers it to every process of the app: Play services' add-account screen, in its UI
            // process, waits for the check-in its main process completes (CHECKIN_COMPLETE). Not an ordered
            // broadcast: its receivers take it in turn and pass on a result, which this does not carry across.
            if (!ordered) PlayServicesChannel.deliverToRunningProcesses(intent, resultCode, resultData, map, null);
        }
        List<ResolveInfo> manifest = forUs ? ManifestReceivers.receiversFor(intent, resolvedType)
                : Collections.<ResolveInfo>emptyList();
        if (!manifest.isEmpty()) {
            ManifestReceivers.deliver(manifest, intent, resultTo, resultCode, resultData, map, ordered, userId);
            delivered += manifest.size();
        } else if (resultTo != null) {
            try {
                resultTo.performReceive(new Intent(intent), resultCode, resultData, map, false, false, userId);
            } catch (Throwable t) {
                Log.w(TAG, "result delivery of " + intent.getAction() + " failed", t);
            }
        }
        Log.i(TAG, "broadcast " + intent.getAction() + " -> " + delivered + " receiver(s)");
        return 0;
    }
}
