package adapter.activity;

import adapter.core.SocketBinder;
import adapter.packagemanager.SourcePackageRegistry;
import android.app.Activity;
import android.app.ActivityManager;
import android.app.ActivityThread;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.RemoteException;
import android.accounts.Account;
import android.content.pm.PackageInfo;
import android.os.Bundle;
import android.os.SystemClock;
import adapter.core.GoogleAuthDelegation;
import adapter.core.GoogleAuthDelegationPolicy;
import adapter.core.GoogleIdentityClaims;
import adapter.core.GoogleAuthProxy;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

/** Activity/result transport for verified, explicitly connected package hosts. */
public final class RemoteActivities {
    private static final String RESULT = "adapter.activity.RemoteActivityResult";
    private static final String CANCEL = "adapter.activity.RemoteActivityCancel";
    private static final Set<Outgoing> OUTGOING = new HashSet<>();
    private static final Set<Incoming> INCOMING = new HashSet<>();
    private static final int LIMIT = 32;

    private RemoteActivities() {}

    public static boolean enabled() {
        return "1".equals(System.getenv("WESTLAKE_REMOTE_ACTIVITIES"));
    }

    private static Handler main() { return Handler.createAsync(Looper.getMainLooper()); }

    /** Query through the same target namespace used for launch; never through the auth provider. */
    public static List<ResolveInfo> query(Intent intent, String type, long flags) {
        return enabled() ? PlayServicesChannel.queryRemoteActivities(intent, type, flags)
                : java.util.Collections.emptyList();
    }

    /** null means this intent is outside the explicitly configured host route. */
    static Integer start(Intent intent, String type, IBinder callerToken, ActivityResults.Request request) {
        if (!enabled() || !PlayServicesChannel.routesActivity(intent)) return null;
        if (callerToken == null) return ActivityManager.START_PERMISSION_DENIED;
        ActivityThread thread = ActivityThread.currentActivityThread();
        Activity caller = thread == null ? null : thread.getActivity(callerToken);
        if (caller == null || caller.isFinishing() || caller.isDestroyed()) return ActivityManager.START_CANCELED;
        List<ResolveInfo> found = query(intent, type, PackageManager.MATCH_DEFAULT_ONLY);
        if (found.isEmpty()) return ActivityManager.START_INTENT_NOT_RESOLVED;
        ActivityInfo target = found.get(0).activityInfo;
        IBinder host = PlayServicesChannel.activityProcess(target);
        if (host == null) return ActivityManager.START_CANCELED;
        Outgoing pending = new Outgoing(host, callerToken, request);
        synchronized (OUTGOING) {
            if (OUTGOING.size() >= LIMIT) return ActivityManager.START_CANCELED;
            OUTGOING.add(pending);
        }
        try {
            host.linkToDeath(pending, 0);
            Intent selected = new Intent(intent).setComponent(new ComponentName(target.packageName, target.name));
            if (!RemoteHostWindows.launch(target.packageName)) { pending.abandon(); return ActivityManager.START_CANCELED; }
            IBinder cancellation = PlayServicesChannel.startRemoteActivity(host, selected,
                    caller.getComponentName(), request != null, pending);
            if (cancellation == null) {
                pending.abandon();
                return ActivityManager.START_CANCELED;
            }
            pending.cancellation = cancellation;
            if (pending.done.get()) pending.cancelRemote();
            event("start_sent target=" + target.packageName + " process=" + target.processName);
            ActivityTaskManagerAdapter.raiseMissionOf(target.packageName, "an activity this app started there");
            return ActivityManager.START_SUCCESS;
        } catch (RemoteException | RuntimeException error) {
            pending.abandon();
            event("start_failed type=" + error.getClass().getSimpleName());
            return ActivityManager.START_CANCELED;
        }
    }

    /** A foreign holder executes the capability at its creator; it never receives the stored Intent. */
    static Integer startPending(android.content.IIntentSender sender, Intent fillIn, int mask, int values,
            IBinder callerToken, ActivityResults.Request request) {
        LocalIntentSenders.Facts facts = LocalIntentSenders.ask(sender);
        if (facts == null || ActivityThread.currentPackageName().equals(facts.packageName)) return null;
        if (facts.type != ActivityManager.INTENT_SENDER_ACTIVITY) return ActivityManager.START_CANCELED;
        IBinder host = SocketBinder.peerRootOf(sender.asBinder());
        if (host == null) host = PlayServicesChannel.pendingActivityHost(facts.packageName);
        if (host == null || callerToken == null) return ActivityManager.START_CANCELED;
        ActivityThread thread = ActivityThread.currentActivityThread();
        Activity caller = thread == null ? null : thread.getActivity(callerToken);
        if (caller == null || caller.isFinishing() || caller.isDestroyed()) return ActivityManager.START_CANCELED;
        Outgoing pending = new Outgoing(host, callerToken, request);
        synchronized (OUTGOING) {
            if (OUTGOING.size() >= LIMIT) return ActivityManager.START_CANCELED;
            OUTGOING.add(pending);
        }
        try {
            host.linkToDeath(pending, 0);
            if (!RemoteHostWindows.launch(facts.packageName)) { pending.abandon(); return ActivityManager.START_CANCELED; }
            pending.cancellation = PlayServicesChannel.startPendingActivity(host, sender.asBinder(), fillIn,
                    mask, values, caller.getComponentName(), request != null, pending);
            if (pending.cancellation == null) { pending.abandon(); return ActivityManager.START_CANCELED; }
            if (pending.done.get()) pending.cancelRemote();
            event("pending_activity_sent creator=" + facts.packageName);
            ActivityTaskManagerAdapter.raiseMissionOf(facts.packageName, "a pending activity this app started there");
            return ActivityManager.START_SUCCESS;
        } catch (RemoteException | RuntimeException error) {
            pending.abandon();
            event("pending_activity_execute_rejected type=" + error.getClass().getSimpleName());
            return ActivityManager.START_CANCELED;
        }
    }

    /** Called on the target host after its channel has verified the client APK and signer. */
    static List<ResolveInfo> resolveHere(Intent intent, String type, long flags, boolean sameApp) {
        List<ResolveInfo> allowed = new ArrayList<>();
        if (!enabled() || intent == null) return allowed;
        for (ResolveInfo match : SourcePackageRegistry.queryActivities(intent, type, flags)) {
            ActivityInfo info = match.activityInfo;
            // Enabled as the package manager decides it: the app's run-time state over its manifest.
            if (info == null || info.applicationInfo == null
                    || !adapter.packagemanager.SourcePackageRegistry.isEnabled(info)) continue;
            // Permission-bearing foreign activities need a trusted grant service, not permissions
            // self-reported in HELLO or the shared native uid. Fail closed until that exists.
            if (!sameApp && (!info.exported || (info.permission != null && !info.permission.isEmpty()))) continue;
            allowed.add(match);
        }
        return allowed;
    }

    static IBinder accept(Intent intent, ComponentName statedCaller, boolean wantsResult, IBinder callback,
            PackageInfo verifiedInfo, int verifiedPid, String verifiedSpawner, int verifiedUid, boolean sameApp) throws RemoteException {
        return accept(intent, statedCaller, wantsResult, callback, verifiedInfo, verifiedPid,
                verifiedSpawner, verifiedUid, sameApp, null);
    }

    static IBinder accept(Intent intent, ComponentName statedCaller, boolean wantsResult, IBinder callback,
            PackageInfo verifiedInfo, int verifiedPid, String verifiedSpawner, int verifiedUid, boolean sameApp,
            Runnable cleanup) throws RemoteException {
        String verifiedPackage = verifiedInfo.packageName;
        if (!enabled() || callback == null || !SocketBinder.isCallingPeer(callback))
            throw new SecurityException("activity callback must belong to the verified caller channel");
        if (statedCaller == null || !verifiedPackage.equals(statedCaller.getPackageName()))
            throw new SecurityException("activity caller package mismatch");
        return launchVerified(intent, statedCaller, wantsResult, callback, verifiedInfo, verifiedPid,
                verifiedSpawner, verifiedUid, sameApp, cleanup);
    }

    /** Entry after a creator-side capability check, or accept's authenticated channel checks. */
    static IBinder launchVerified(Intent intent, ComponentName statedCaller, boolean wantsResult, IBinder callback,
            PackageInfo verifiedInfo, int verifiedPid, String verifiedSpawner, int verifiedUid, boolean sameApp)
            throws RemoteException {
        return launchVerified(intent, statedCaller, wantsResult, callback, verifiedInfo, verifiedPid,
                verifiedSpawner, verifiedUid, sameApp, null);
    }

    private static IBinder launchVerified(Intent intent, ComponentName statedCaller, boolean wantsResult, IBinder callback,
            PackageInfo verifiedInfo, int verifiedPid, String verifiedSpawner, int verifiedUid, boolean sameApp,
            Runnable cleanup) throws RemoteException {
        String verifiedPackage = verifiedInfo.packageName;
        int grants = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION;
        if (intent == null || (intent.getFlags() & grants) != 0)
            throw new SecurityException("remote activity URI grants are not implemented");
        List<ResolveInfo> matches = resolveHere(intent, intent.getType(), PackageManager.MATCH_DEFAULT_ONLY, sameApp);
        if (matches.isEmpty()) throw new SecurityException("activity is not an enabled exported target");
        ActivityInfo info = matches.get(0).activityInfo;
        if (!AppProcesses.runsHere(AppProcesses.activityProcessOf(info)))
            throw new SecurityException("activity sent to the wrong process");
        Intent launch = new Intent(intent);
        launch.setExtrasClassLoader(ActivityThread.currentApplication().getClassLoader());
        String actualClass = info.targetActivity == null ? info.name : info.targetActivity;
        launch.setComponent(new ComponentName(info.packageName, actualClass));
        Incoming pending = new Incoming(callback, statedCaller, verifiedUid, wantsResult, verifiedInfo, verifiedPid, verifiedSpawner, cleanup);
        pending.targetPackage = info.packageName;
        pending.targetActivity = actualClass;
        synchronized (INCOMING) {
            if (INCOMING.size() >= LIMIT) throw new IllegalStateException("too many remote activities");
            INCOMING.add(pending);
        }
        try { callback.linkToDeath(pending, 0); }
        catch (RemoteException | RuntimeException error) { pending.complete(0, null, false, false); throw error; }
        main().post(() -> {
            if (pending.done.get()) return;
            event("launch_received caller=" + verifiedPackage + " process=" + AppProcesses.self()
                    + " class=" + actualClass);
            AppSchedulerBridge.launchRemoteActivity(launch, new ActivityResults.Request(pending));
        });
        return pending;
    }

    /** Explicit interactive requests allow their host's nested UI until the request finishes. */
    static boolean interactive() { synchronized (INCOMING) { return !INCOMING.isEmpty(); } }

    /** Account-picker metadata may be changed only for this live, verified UI request's caller. */
    public static boolean isInteractiveCaller(String packageName) {
        synchronized (INCOMING) {
            for (Incoming request : INCOMING)
                if (!request.done.get() && request.caller.getPackageName().equals(packageName)) return true;
        }
        return false;
    }

    /** Only same-app channel callers reach this entry, or the official UI's own AccountManager. */
    static String readDelegatedAuth(String method, Account account, String input) {
        if (!GoogleAuthDelegation.enabled()) return null;
        boolean tokenRead = "peekAuthToken".equals(method);
        boolean invalidate = "invalidateAuthToken".equals(method);
        boolean expiry = "getUserData".equals(method) && input != null && input.startsWith("EXP:");
        boolean scopes = "getUserData".equals(method) && input != null && input.startsWith("SCOPES:");
        if (!tokenRead && !invalidate && !expiry && !scopes) return null;
        try {
            if (invalidate) {
                List<Incoming> live;
                synchronized (INCOMING) { live = new ArrayList<>(INCOMING); }
                for (Incoming pending : live) pending.invalidate(input);
                return null;
            }
            String key = tokenRead ? input : input.substring(expiry ? 4 : 7);
            GoogleAuthDelegationPolicy request = GoogleAuthDelegationPolicy.parse(key);
            Incoming selected = null;
            synchronized (INCOMING) {
                for (Incoming pending : INCOMING) {
                    if (pending.selected(account) && pending.caller.getPackageName().equals(request.owner)) {
                        if (selected != null) throw new SecurityException("ambiguous interactive request");
                        selected = pending;
                    }
                }
            }
            if (selected == null) {
                if ("1".equals(System.getenv("WESTLAKE_GOOGLE_AUTH_BOUNDARY_TRACE"))) {
                    int live;
                    synchronized (INCOMING) { live = INCOMING.size(); }
                    GoogleAuthDelegation.event("no_selection=true live_requests=" + live + " method=" + method);
                }
                return null;
            }
            if (!request.signer.equals(GoogleAuthProxy.digest(selected.origin.signatures[0].toByteArray(), "SHA-1")))
                throw new SecurityException("cache signer differs from origin");
            if (!tokenRead) {
                synchronized (selected.tokens) {
                    Issued found = selected.tokens.get(key);
                    return found != null && found.account.equals(account) && selected.selected(account)
                            ? found.result.getString(expiry ? "expiry" : "scopes") : null;
                }
            }
            String providerKey = GoogleIdentityClaims.request(key,
                    "1".equals(System.getenv("WESTLAKE_GOOGLE_IDENTITY_CLAIMS")), selected.targetPackage, selected.targetActivity);
            if (GoogleAuthOperations.enabled()) synchronized (selected.tokens) {
                if (selected.authEpoch == null) selected.authEpoch = GoogleAuthOperations.generation(request.owner);
            }
            Bundle result = PlayServicesChannel.delegatedAuth(selected.origin, selected.originPid, selected.spawner,
                    account, providerKey, selected.witness(account), null);
            if (result == null) return null;
            String token = result.getString("token");
            synchronized (selected.tokens) {
                if (!selected.selected(account)) return null;
                if (token != null) {
                    if (selected.tokens.size() >= 16 && !selected.tokens.containsKey(key)) throw new SecurityException("token limit");
                    selected.tokens.put(key, new Issued(account, result, providerKey));
                }
            }
            GoogleAuthDelegation.event("ui_read=true owner=" + request.owner + " token_present=" + (token != null)
                    + " identity_claims_adapted=" + !key.equals(providerKey));
            return token;
        } catch (Exception error) {
            GoogleAuthDelegation.event("ui_denied=true " + GoogleAuthDelegation.failure(error));
            return null;
        }
    }

    private static final class Issued {
        final Account account;
        final Bundle result;
        final String providerKey;
        Issued(Account account, Bundle result, String providerKey) {
            this.account = account; this.result = result; this.providerKey = providerKey;
        }
    }

    static void callerDestroyed(IBinder token) {
        List<Outgoing> cancel = new ArrayList<>();
        synchronized (OUTGOING) { for (Outgoing p : OUTGOING) if (p.callerToken == token) cancel.add(p); }
        for (Outgoing pending : cancel) { pending.abandon(); pending.cancelRemote(); }
    }

    static final class Incoming extends Binder implements IBinder.DeathRecipient {
        final IBinder callback;
        final ComponentName caller;
        final int callerUid;
        final boolean wantsResult;
        final AtomicBoolean done = new AtomicBoolean();
        volatile IBinder activityToken;
        final PackageInfo origin;
        final int originPid;
        final String spawner;
        final Runnable cleanup;
        String targetPackage, targetActivity;
        String authEpoch;
        final long authDeadline = SystemClock.elapsedRealtime() + 10 * 60_000L;
        final java.util.Map<String, Issued> tokens = new java.util.HashMap<>();

        Incoming(IBinder callback, ComponentName caller, int uid, boolean wantsResult,
                PackageInfo origin, int originPid, String spawner, Runnable cleanup) {
            this.callback = callback; this.caller = caller; this.callerUid = uid; this.wantsResult = wantsResult;
            this.origin = origin; this.originPid = originPid; this.spawner = spawner;
            this.cleanup = cleanup;
        }

        boolean selected(Account account) {
            return !done.get() && SystemClock.elapsedRealtime() < authDeadline && callback.isBinderAlive()
                    && GoogleAuthProxy.selectedInteractiveAccount(account, caller.getPackageName());
        }

        IBinder witness(Account account) {
            return new Binder() {
                @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                    if (code != 1 || !PlayServicesChannel.authProviderCalling()) throw new SecurityException("wrong witness peer");
                    data.enforceInterface(GoogleAuthDelegation.WITNESS);
                    reply.writeNoException(); reply.writeBoolean(selected(account)); return true;
                }
            };
        }

        void invalidate(String token) throws RemoteException {
            java.util.Map<String, Issued> copy;
            synchronized (tokens) { copy = new java.util.HashMap<>(tokens); }
            for (java.util.Map.Entry<String, Issued> entry : copy.entrySet()) {
                Issued issued = entry.getValue();
                if (token != null && token.equals(issued.result.getString("token")) && selected(issued.account)) {
                    synchronized (tokens) { tokens.remove(entry.getKey()); }
                    PlayServicesChannel.delegatedAuth(origin, originPid, spawner, issued.account, issued.providerKey, witness(issued.account), token);
                }
            }
        }

        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            if (code != 1 || !SocketBinder.isCallingPeer(callback)) throw new SecurityException("wrong cancellation peer");
            data.enforceInterface(CANCEL);
            complete(Activity.RESULT_CANCELED, null, false, true);
            if (reply != null) reply.writeNoException();
            return true;
        }

        @Override public void binderDied() { complete(Activity.RESULT_CANCELED, null, false, true); }

        void complete(int result, Intent intent, boolean notify, boolean close) {
            if (!done.compareAndSet(false, true)) return;
            try {
                synchronized (INCOMING) { INCOMING.remove(this); }
                synchronized (tokens) {
                    if (result == Activity.RESULT_OK && notify && GoogleAuthOperations.enabled()
                            && "com.google.android.gms".equals(targetPackage)
                            && "com.google.android.gms.auth.api.credentials.assistedsignin.ui.GoogleSignInActivity".equals(targetActivity)
                            && SystemClock.elapsedRealtime() < authDeadline && callback.isBinderAlive()) {
                        for (Issued issued : tokens.values()) try {
                            GoogleAuthDelegationPolicy request = GoogleAuthDelegationPolicy.parse(issued.providerKey);
                            if (request.clientId != null && !request.serverCode && issued.result.getString("token") != null
                                    && GoogleAuthProxy.selectedInteractiveAccount(issued.account, caller.getPackageName()))
                                GoogleAuthOperations.remember(origin, issued.account, issued.providerKey, authEpoch);
                        } catch (Exception error) {
                            GoogleAuthDelegation.event("session_grant_denied=true " + GoogleAuthDelegation.failure(error));
                        }
                    }
                    tokens.clear();
                }
                adapter.core.GoogleAuthProxy.clearInteractiveVisibility(caller.getPackageName());
                callback.unlinkToDeath(this, 0);
                if (notify) {
                    if (wantsResult) RemoteHostWindows.returnTo(caller.getPackageName());
                    Parcel data = Parcel.obtain();
                    try {
                        data.writeInterfaceToken(RESULT); data.writeInt(result); data.writeTypedObject(intent, 0);
                        callback.transact(1, data, null, IBinder.FLAG_ONEWAY);
                        event("result_sent caller=" + caller.getPackageName() + " result_code=" + result);
                        // The caller raises its own mission when the result arrives, from the background, which OH
                        // 7.0 + SceneBoard does not bring forward; this host, still in front, raises it too.
                        ActivityTaskManagerAdapter.raiseMissionOf(caller.getPackageName(), "the result went back");
                    } catch (RemoteException | RuntimeException error) { event("result_peer_unavailable=true"); }
                    finally { data.recycle(); }
                }
                if (close) main().post(() -> {
                    ActivityThread thread = ActivityThread.currentActivityThread();
                    Activity activity = thread == null || activityToken == null ? null : thread.getActivity(activityToken);
                    if (activity != null && !activity.isFinishing()) activity.finish();
                });
            } finally { if (cleanup != null) cleanup.run(); }
        }
    }

    private static final class Outgoing extends Binder implements IBinder.DeathRecipient {
        final IBinder host;
        final IBinder callerToken;
        final ActivityResults.Request request;
        final AtomicBoolean done = new AtomicBoolean();
        volatile IBinder cancellation;

        Outgoing(IBinder host, IBinder token, ActivityResults.Request request) {
            this.host = host; this.callerToken = token; this.request = request;
        }

        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            if (code != 1 || !SocketBinder.isCallingPeer(host)) throw new SecurityException("wrong result peer");
            data.enforceInterface(RESULT);
            int result = data.readInt(); Intent intent = data.readTypedObject(Intent.CREATOR);
            finish(result, intent);
            if (reply != null) reply.writeNoException();
            return true;
        }

        @Override public void binderDied() { finish(Activity.RESULT_CANCELED, null); }

        void finish(int result, Intent data) {
            if (!done.compareAndSet(false, true)) return;
            release();
            main().post(() -> {
                if (data != null) data.setExtrasClassLoader(ActivityThread.currentApplication().getClassLoader());
                event("result_received result_code=" + result);
                ActivityTaskManagerAdapter.raiseOwnMission("the activity it started in another app returned");
                if (request != null) ActivityResults.sendNow(request.resultTo, request.resultWho,
                        request.requestCode, result, data);
            });
        }

        void abandon() { if (done.compareAndSet(false, true)) release(); }
        private void release() {
            synchronized (OUTGOING) { OUTGOING.remove(this); }
            host.unlinkToDeath(this, 0);
        }
        void cancelRemote() {
            IBinder target = cancellation;
            if (target == null) return;
            Parcel data = Parcel.obtain();
            try { data.writeInterfaceToken(CANCEL); target.transact(1, data, null, IBinder.FLAG_ONEWAY); }
            catch (RemoteException | RuntimeException ignored) {}
            finally { data.recycle(); }
        }
    }

    static void event(String value) { System.err.println("[WL-REMOTE-ACTIVITY] time=" + System.currentTimeMillis() + " " + value); }
}
