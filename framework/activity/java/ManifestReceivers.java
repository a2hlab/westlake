package adapter.activity;

import android.app.ActivityThread;
import android.app.Application;
import android.app.LoadedApk;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.IIntentReceiver;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ResolveInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.lang.reflect.Method;
import java.util.Collections;
import java.util.List;

/**
 * The receivers an app declares in its manifest, run in process for the broadcasts the app sends
 * them: one sent to a receiver by name, or within the app's package -- the PendingIntent of an alarm
 * the app set, its own sendBroadcast. Nothing ran them before, so an alarm that rang a receiver rang
 * nothing.
 *
 * As ActivityThread.handleReceiver runs one, on the main thread: the receiver is made through the
 * app's component factory and gets onReceive with a context that cannot register receivers or bind
 * services, and goAsync works. An ordered broadcast reaches the receivers one at a time, the highest
 * priority first, each with the result the one before it left, until one aborts it; the final
 * receiver gets the result after them.
 *
 * As on Android 8 and later, an implicit broadcast that names no package reaches no manifest
 * receiver of an app that targets those versions. A receiver declared in another process runs in
 * this one, and its result is read when onReceive returns, a pending goAsync not waited for.
 */
final class ManifestReceivers {
    private static final String TAG = "OH_ManifestReceivers";
    // Intent.FLAG_RECEIVER_INCLUDE_BACKGROUND and FLAG_RECEIVER_EXCLUDE_BACKGROUND (hidden).
    private static final int FLAG_RECEIVER_INCLUDE_BACKGROUND = 0x01000000;
    private static final int FLAG_RECEIVER_EXCLUDE_BACKGROUND = 0x00800000;

    private static Method sRestrictedContext;

    private ManifestReceivers() {}

    /** The app's manifest receivers a broadcast from the app reaches, as the broadcast queue skips them. */
    static List<ResolveInfo> receiversFor(Intent intent, String resolvedType) {
        Application app = ActivityThread.currentApplication();
        int flags = intent.getFlags();
        if (app == null || (flags & Intent.FLAG_RECEIVER_REGISTERED_ONLY) != 0) return Collections.emptyList();
        boolean named = intent.getComponent() != null || intent.getPackage() != null;
        boolean limited = app.getApplicationInfo().targetSdkVersion >= Build.VERSION_CODES.O;
        if (limited && ((flags & FLAG_RECEIVER_EXCLUDE_BACKGROUND) != 0
                || (!named && (flags & FLAG_RECEIVER_INCLUDE_BACKGROUND) == 0))) {
            return Collections.emptyList();
        }
        return adapter.packagemanager.SourcePackageRegistry.queryReceivers(intent, resolvedType, 0);
    }

    /** Runs the receivers on the main thread, after what is already queued there, then resultTo. */
    static void deliver(final List<ResolveInfo> receivers, final Intent intent, final IIntentReceiver resultTo,
            final int resultCode, final String resultData, final Bundle extras, final boolean ordered,
            final int userId) {
        new Handler(Looper.getMainLooper()).post(
                () -> run(receivers, intent, resultTo, resultCode, resultData, extras, ordered, userId));
    }

    private static void run(List<ResolveInfo> receivers, Intent intent, IIntentReceiver resultTo,
            int code, String data, Bundle extras, boolean ordered, int userId) {
        Application app = ActivityThread.currentApplication();
        for (ResolveInfo resolved : receivers) {
            ActivityInfo info = resolved.activityInfo;
            Intent delivered = new Intent(intent);
            delivered.setComponent(new ComponentName(info.packageName, info.name));
            // A receiver declared for another of the app's processes runs there (ADR-0002). An ordered
            // broadcast's result does not come back from that process yet.
            String process = AppProcesses.processOf(info);
            if (!AppProcesses.runsHere(process)) {
                PlayServicesChannel.deliverInOwnProcess(process, delivered);
                continue;
            }
            BroadcastReceiver.PendingResult result = new BroadcastReceiver.PendingResult(code, data, extras,
                    BroadcastReceiver.PendingResult.TYPE_UNREGISTERED, ordered, false, null, userId,
                    intent.getFlags());
            try {
                ClassLoader loader = app.getClassLoader();
                delivered.setExtrasClassLoader(loader);
                BroadcastReceiver receiver = instantiate(app, loader, info.name, delivered);
                receiver.setPendingResult(result);
                receiver.onReceive(restrictedContext(app), delivered);
                Log.i(TAG, "delivered " + intent.getAction() + " to " + info.name);
            } catch (Throwable t) {
                Log.e(TAG, "receiver " + info.name + " failed on " + intent, t);
            }
            if (ordered) {
                code = result.getResultCode();
                data = result.getResultData();
                extras = result.getResultExtras(false);
                if (result.getAbortBroadcast()) break;
            }
        }
        if (resultTo != null) {
            try {
                resultTo.performReceive(new Intent(intent), code, data, extras, false, false, userId);
            } catch (Throwable t) {
                Log.w(TAG, "result delivery of " + intent.getAction() + " failed", t);
            }
        }
    }

    /** A broadcast another process of this app routed to one of its manifest receivers declared for this one. */
    static void deliverHere(Intent intent) {
        List<ResolveInfo> receivers = new java.util.ArrayList<>(
                adapter.packagemanager.SourcePackageRegistry.queryReceivers(intent, null, 0));
        receivers.removeIf(r -> r.activityInfo == null || !AppProcesses.runsHere(AppProcesses.processOf(r.activityInfo)));
        if (receivers.isEmpty()) {
            Log.w(TAG, "no receiver here for " + intent);
            return;
        }
        deliver(receivers, intent, null, 0, null, null, false, android.os.UserHandle.myUserId());
    }

    private static BroadcastReceiver instantiate(Application app, ClassLoader loader, String name, Intent intent)
            throws Exception {
        LoadedApk apk = app.mLoadedApk;
        if (apk != null && apk.getAppFactory() != null) {
            return apk.getAppFactory().instantiateReceiver(loader, name, intent);
        }
        return (BroadcastReceiver) loader.loadClass(name).newInstance();
    }

    /** ContextImpl.getReceiverRestrictedContext of the app's base context, or the app itself. */
    private static Context restrictedContext(Application app) {
        Context base = app.getBaseContext();
        try {
            if (sRestrictedContext == null) {
                Method method = base.getClass().getDeclaredMethod("getReceiverRestrictedContext");
                method.setAccessible(true);
                sRestrictedContext = method;
            }
            return (Context) sRestrictedContext.invoke(base);
        } catch (Throwable t) {
            return app;
        }
    }
}
