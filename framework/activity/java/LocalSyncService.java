package adapter.activity;

import adapter.core.SyncDispatchState;
import adapter.core.PeriodicSyncSchedule;
import android.accounts.Account;
import android.accounts.AccountManager;
import android.app.ActivityThread;
import android.app.Application;
import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.PeriodicSync;
import android.content.ISyncAdapter;
import android.content.ISyncContext;
import android.content.ISyncStatusObserver;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.SyncAdapterType;
import android.content.SyncInfo;
import android.content.SyncRequest;
import android.content.SyncResult;
import android.content.SyncStatusInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.content.res.TypedArray;
import android.content.res.XmlResourceParser;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.SystemClock;
import android.os.UserHandle;
import android.util.Xml;
import android.util.Base64;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Opt-in app-owned sync execution. Periodic records survive restart; timers run while the app lives. */
public final class LocalSyncService {
    public static final Object UNHANDLED = new Object();
    private static final String ACTION = "android.content.SyncAdapter";
    private static final Object LOCK = new Object();
    private static LocalSyncService instance;
    private final Application app;
    private final SharedPreferences settings;
    private final Handler main = Handler.createAsync(Looper.getMainLooper());
    private final java.util.concurrent.ExecutorService worker = Executors.newCachedThreadPool();
    private final ScheduledExecutorService timers = Executors.newSingleThreadScheduledExecutor();
    private final List<Adapter> adapters = new ArrayList<>();
    private final List<Job> jobs = new ArrayList<>();
    private final Map<String, SyncStatusInfo> statuses = new HashMap<>();
    private final Map<IBinder, Observer> observers = new HashMap<>();
    private long sequence;
    private final List<Periodic> periodic = new ArrayList<>();
    private static final int MAX_PERIODIC = 64, MAX_PERIODIC_BYTES = 1_048_576;

    private LocalSyncService(Application app) {
        this.app = app;
        settings = app.getSharedPreferences("westlake-local-sync", Context.MODE_PRIVATE);
        discover();
        restorePeriodic();
        timers.scheduleWithFixedDelay(() -> worker.execute(this::periodicTick), 0, 30, TimeUnit.SECONDS);
    }

    public static boolean enabled() { return "1".equals(System.getenv("WESTLAKE_LOCAL_SYNC")); }

    /** Called by the process-local content service, not a globally accessible account service. */
    public static Object answer(String method, Object[] args) {
        if (!enabled() || !supported(method)) return UNHANDLED;
        Application app = ActivityThread.currentApplication();
        if (app == null) return UNHANDLED;
        synchronized (LOCK) {
            if (instance == null) instance = new LocalSyncService(app);
            if (!instance.app.getPackageName().equals(app.getPackageName())) throw new SecurityException("sync owner changed");
            trace("method=" + method);
            return instance.call(method, args == null ? new Object[0] : args);
        }
    }

    private static boolean supported(String m) {
        return Arrays.asList("requestSync", "sync", "syncAsUser", "cancelSync", "cancelSyncAsUser", "cancelRequest",
                "getSyncAutomatically", "getSyncAutomaticallyAsUser", "setSyncAutomatically", "setSyncAutomaticallyAsUser",
                "getIsSyncable", "getIsSyncableAsUser", "setIsSyncable", "setIsSyncableAsUser",
                "getMasterSyncAutomatically", "getMasterSyncAutomaticallyAsUser", "setMasterSyncAutomatically",
                "setMasterSyncAutomaticallyAsUser", "getSyncAdapterTypes", "getSyncAdapterTypesAsUser",
                "getSyncAdapterPackagesForAuthorityAsUser", "getSyncAdapterPackageAsUser", "isSyncActive", "isSyncPending",
                "isSyncPendingAsUser", "getCurrentSyncs", "getCurrentSyncsAsUser", "getSyncStatus", "getSyncStatusAsUser",
                "addStatusChangeListener", "removeStatusChangeListener", "getPeriodicSyncs", "addPeriodicSync",
                "removePeriodicSync").contains(m);
    }

    private Object call(String method, Object[] a) {
        if (method.endsWith("AsUser")) {
            int index = method.equals("syncAsUser") ? 1 : a.length - 1;
            int user = (Integer) a[index];
            if (user != UserHandle.myUserId() && user != UserHandle.USER_CURRENT)
                throw new SecurityException("sync belongs to the current user");
        }
        switch (method) {
            case "getSyncAdapterTypes": case "getSyncAdapterTypesAsUser":
                return adapters.stream().map(x -> x.type).toArray(SyncAdapterType[]::new);
            case "getSyncAdapterPackagesForAuthorityAsUser":
                return adapters.stream().anyMatch(x -> x.type.authority.equals(a[0]))
                        ? new String[] {app.getPackageName()} : new String[0];
            case "getSyncAdapterPackageAsUser":
                return adapters.stream().anyMatch(x -> x.type.accountType.equals(a[0]) && x.type.authority.equals(a[1]))
                        ? app.getPackageName() : null;
            case "getMasterSyncAutomatically": case "getMasterSyncAutomaticallyAsUser":
                return settings.getBoolean("master", true);
            case "setMasterSyncAutomatically": case "setMasterSyncAutomaticallyAsUser":
                save(settings.edit().putBoolean("master", (Boolean) a[0]));
                notifyStatus(ContentResolver.SYNC_OBSERVER_TYPE_SETTINGS);
                if ((Boolean) a[0]) request(null, null, new Bundle(), app.getPackageName());
                return null;
            case "getSyncAutomatically": case "getSyncAutomaticallyAsUser":
                return settings.getBoolean(key((Account) a[0], (String) a[1]) + ".auto", false);
            case "setSyncAutomatically": case "setSyncAutomaticallyAsUser": {
                Account account = (Account) a[0]; String authority = (String) a[1];
                requireAdapter(account, authority);
                save(settings.edit().putBoolean(key(account, authority) + ".auto", (Boolean) a[2]));
                notifyStatus(ContentResolver.SYNC_OBSERVER_TYPE_SETTINGS);
                if ((Boolean) a[2]) request(account, authority, new Bundle(), app.getPackageName());
                return null;
            }
            case "getIsSyncable": case "getIsSyncableAsUser":
                return syncable((Account) a[0], (String) a[1]);
            case "setIsSyncable": case "setIsSyncableAsUser": {
                Account account = (Account) a[0]; String authority = (String) a[1];
                requireAdapter(account, authority); int value = (Integer) a[2];
                if (value < -1 || value > 2) throw new IllegalArgumentException("invalid syncable state");
                save(settings.edit().putInt(key(account, authority) + ".syncable", value));
                notifyStatus(ContentResolver.SYNC_OBSERVER_TYPE_SETTINGS); return null;
            }
            case "requestSync":
                request((Account) a[0], (String) a[1], (Bundle) a[2], (String) a[3]); return null;
            case "sync": case "syncAsUser": {
                SyncRequest r = (SyncRequest) a[0];
                if (r.isPeriodic()) {
                    String caller = (String) a[a.length - 1];
                    if (caller != null && !app.getPackageName().equals(caller)) throw new SecurityException("foreign sync caller");
                    addPeriodic(r.getAccount(), r.getProvider(), r.getBundle(), r.getSyncRunTime(), r.getSyncFlexTime());
                    return null;
                }
                if (r.getSyncRunTime() != 0 || r.getSyncFlexTime() != 0)
                    throw new UnsupportedOperationException("delayed sync scheduling is not implemented");
                request(r.getAccount(), r.getProvider(), r.getBundle(), (String) a[a.length - 1]); return null;
            }
            case "cancelSync": case "cancelSyncAsUser":
                if (a[2] != null) throw new UnsupportedOperationException("component sync request unsupported");
                cancel((Account) a[0], (String) a[1]); return null;
            case "cancelRequest": {
                SyncRequest r = (SyncRequest) a[0];
                if (r.isPeriodic()) removePeriodic(r.getAccount(), r.getProvider(), r.getBundle());
                for (Job j : new ArrayList<>(jobs)) if (j.matches(r.getAccount(), r.getProvider())
                        && sameExtras(j.extras, r.getBundle())) j.cancel("cancelled");
                return null;
            }
            case "isSyncActive": case "isSyncPending": case "isSyncPendingAsUser": {
                if (a[2] != null) return false;
                boolean active = method.equals("isSyncActive");
                return jobs.stream().anyMatch(j -> j.matches((Account) a[0], (String) a[1]) && j.running == active);
            }
            case "getCurrentSyncs": case "getCurrentSyncsAsUser": {
                List<SyncInfo> list = new ArrayList<>();
                for (Job j : jobs) if (j.running) list.add(new SyncInfo(j.key.hashCode(), j.account, j.adapter.type.authority, j.started));
                return list;
            }
            case "getSyncStatus": case "getSyncStatusAsUser": {
                if (a[2] != null || a[0] == null || a[1] == null) return null;
                String k = key((Account) a[0], (String) a[1]);
                SyncStatusInfo value = statuses.get(k);
                return value == null ? null : new SyncStatusInfo(value);
            }
            case "addStatusChangeListener": {
                ISyncStatusObserver callback = (ISyncStatusObserver) a[1];
                if (callback != null && observers.size() < 64) observers.put(callback.asBinder(), new Observer((Integer) a[0], callback));
                return null;
            }
            case "removeStatusChangeListener":
                if (a[0] != null) observers.remove(((ISyncStatusObserver) a[0]).asBinder()); return null;
            case "getPeriodicSyncs": {
                requireAdapter((Account) a[0], (String) a[1]);
                List<PeriodicSync> result = new ArrayList<>();
                for (Periodic p : periodic) if (p.sync.account.equals(a[0]) && p.sync.authority.equals(a[1]))
                    result.add(new PeriodicSync(p.sync));
                return result;
            }
            case "addPeriodicSync": {
                long period = (Long) a[3];
                long flex = Math.min(Math.max(0, period), 86_400) * 4 / 100;
                addPeriodic((Account) a[0], (String) a[1], (Bundle) a[2], period, flex);
                return null;
            }
            case "removePeriodicSync":
                removePeriodic((Account) a[0], (String) a[1], (Bundle) a[2]); return null;
            default: return UNHANDLED;
        }
    }

    private static final class Periodic {
        final String id;
        final PeriodicSync sync;
        final PeriodicSyncSchedule schedule;
        Periodic(String id, PeriodicSync sync, PeriodicSyncSchedule schedule) {
            if (id == null || !id.matches("[0-9a-f-]{36}")) throw new IllegalArgumentException("invalid periodic id");
            this.id = id; this.sync = sync; this.schedule = schedule;
        }
        boolean matches(Account account, String authority, Bundle extras) {
            return sync.account.equals(account) && sync.authority.equals(authority) && sameExtras(sync.extras, extras);
        }
    }

    private Bundle periodicExtras(Account account, String authority, Bundle extras) {
        requireAdapter(account, authority);
        if (account.name.isEmpty() || account.name.length() > 320 || account.type.length() > 256)
            throw new IllegalArgumentException("invalid periodic account");
        Bundle copy = extras == null ? new Bundle() : new Bundle(extras);
        ContentResolver.validateSyncExtrasBundle(copy);
        if (ContentResolver.invalidPeriodicExtras(copy)) throw new IllegalArgumentException("illegal periodic extras");
        Parcel parcel = Parcel.obtain();
        try {
            parcel.writeBundle(copy);
            if (parcel.dataSize() > 65_536) throw new IllegalArgumentException("sync extras too large");
            parcel.setDataPosition(0);
            return parcel.readBundle(Account.class.getClassLoader());
        } finally { parcel.recycle(); }
    }

    private void addPeriodic(Account account, String authority, Bundle extras, long period, long flex) {
        Bundle copy = periodicExtras(account, authority, extras);
        if (!Arrays.asList(AccountManager.get(app).getAccounts()).contains(account))
            throw new SecurityException("sync account not visible");
        PeriodicSyncSchedule timing = PeriodicSyncSchedule.create(period, flex, System.currentTimeMillis());
        List<Periodic> changed = new ArrayList<>(periodic);
        Periodic previous = null;
        for (Periodic p : periodic) if (p.matches(account, authority, copy)) { previous = p; break; }
        if (previous != null && previous.schedule.periodSeconds == timing.periodSeconds
                && previous.schedule.flexSeconds == timing.flexSeconds) return; // Preserve an existing window.
        if (previous == null && changed.size() >= MAX_PERIODIC) throw new IllegalStateException("periodic sync limit reached");
        if (previous != null) changed.remove(previous);
        String id = previous == null ? java.util.UUID.randomUUID().toString() : previous.id;
        changed.add(new Periodic(id, new PeriodicSync(account, authority, copy,
                timing.periodSeconds, timing.flexSeconds), timing));
        storePeriodic(changed);
        trace("periodic_registered=true period_seconds=" + timing.periodSeconds + " flex_seconds=" + timing.flexSeconds);
        notifyStatus(ContentResolver.SYNC_OBSERVER_TYPE_SETTINGS);
        worker.execute(this::periodicTick);
    }

    private void removePeriodic(Account account, String authority, Bundle extras) {
        Bundle copy = periodicExtras(account, authority, extras);
        List<Periodic> changed = new ArrayList<>(periodic), removed = new ArrayList<>();
        for (Periodic p : periodic) if (p.matches(account, authority, copy)) { changed.remove(p); removed.add(p); }
        if (removed.isEmpty()) return;
        storePeriodic(changed);
        for (Job j : new ArrayList<>(jobs)) if (removed.stream().anyMatch(p -> p.id.equals(j.periodicId))) j.cancel("periodic_removed");
        trace("periodic_removed=true count=" + removed.size());
        notifyStatus(ContentResolver.SYNC_OBSERVER_TYPE_SETTINGS);
    }

    private void storePeriodic(List<Periodic> values) {
        Parcel parcel = Parcel.obtain();
        try {
            parcel.writeInt(1); parcel.writeInt(values.size());
            for (Periodic p : values) {
                parcel.writeString(p.id); parcel.writeTypedObject(p.sync, 0); parcel.writeLong(p.schedule.nextAtMillis);
            }
            if (parcel.dataSize() > MAX_PERIODIC_BYTES) throw new IllegalStateException("periodic store too large");
            save(settings.edit().putString("periodic.v1", Base64.encodeToString(parcel.marshall(), Base64.NO_WRAP)));
            periodic.clear(); periodic.addAll(values);
        } finally { parcel.recycle(); }
    }

    private void restorePeriodic() {
        String encoded = settings.getString("periodic.v1", null);
        if (encoded == null) return;
        Parcel parcel = Parcel.obtain();
        try {
            if (encoded.length() > MAX_PERIODIC_BYTES * 4 / 3 + 4) throw new IllegalStateException("periodic store too large");
            byte[] bytes = Base64.decode(encoded, Base64.NO_WRAP);
            if (bytes.length > MAX_PERIODIC_BYTES) throw new IllegalStateException("periodic store too large");
            parcel.unmarshall(bytes, 0, bytes.length); parcel.setDataPosition(0);
            if (parcel.readInt() != 1) throw new IllegalStateException("unknown periodic store");
            int count = parcel.readInt(); if (count < 0 || count > MAX_PERIODIC) throw new IllegalStateException("invalid periodic count");
            List<Periodic> restored = new ArrayList<>();
            for (int i = 0; i < count; i++) {
                String id = parcel.readString(); PeriodicSync value = parcel.readTypedObject(PeriodicSync.CREATOR);
                if (value == null) throw new IllegalStateException("missing periodic record");
                long next = parcel.readLong();
                Bundle copy = periodicExtras(value.account, value.authority, value.extras);
                if (restored.stream().anyMatch(p -> p.id.equals(id) || p.matches(value.account, value.authority, copy)))
                    throw new IllegalStateException("duplicate periodic record");
                PeriodicSyncSchedule timing = PeriodicSyncSchedule.restore(value.period, value.flexTime, next);
                restored.add(new Periodic(id, new PeriodicSync(value.account, value.authority, copy,
                        timing.periodSeconds, timing.flexSeconds), timing));
            }
            if (parcel.dataAvail() != 0) throw new IllegalStateException("trailing periodic fields");
            periodic.addAll(restored); trace("periodic_restored=true count=" + restored.size());
        } catch (RuntimeException invalid) {
            periodic.clear(); trace("periodic_restore_refused=true type=" + invalid.getClass().getSimpleName());
        } finally { parcel.recycle(); }
    }

    private void periodicTick() {
        synchronized (LOCK) {
            try {
                if (periodic.isEmpty() || !settings.getBoolean("master", true)) return;
                Account[] visible = AccountManager.get(app).getAccounts();
                for (Periodic p : new ArrayList<>(periodic)) {
                    long now = System.currentTimeMillis();
                    if (!p.schedule.due(now) || !Arrays.asList(visible).contains(p.sync.account)
                            || !settings.getBoolean(key(p.sync.account, p.sync.authority) + ".auto", false)
                            || syncable(p.sync.account, p.sync.authority) == 0) continue;
                    requireAdapter(p.sync.account, p.sync.authority);
                    if (jobs.stream().anyMatch(j -> j.matches(p.sync.account, p.sync.authority)
                            && sameExtras(j.extras, p.sync.extras))) continue;
                    request(p.sync.account, p.sync.authority, p.sync.extras, app.getPackageName(), p.id);
                    List<Periodic> changed = new ArrayList<>(periodic); changed.remove(p);
                    changed.add(new Periodic(p.id, p.sync, p.schedule.advance(now))); storePeriodic(changed);
                    trace("periodic_due=true authority=" + p.sync.authority);
                }
            } catch (RuntimeException failure) {
                trace("periodic_dispatch_failed=true type=" + failure.getClass().getSimpleName());
            }
        }
    }

    private static void save(SharedPreferences.Editor edit) {
        if (!edit.commit()) throw new IllegalStateException("sync settings write failed");
    }

    private int syncable(Account account, String authority) {
        Adapter adapter = find(account, authority);
        return adapter == null ? 0 : settings.getInt(key(account, authority) + ".syncable", adapter.type.isAlwaysSyncable() ? 1 : -1);
    }

    private void request(Account account, String authority, Bundle extras, String caller) {
        request(account, authority, extras, caller, null);
    }

    private void request(Account account, String authority, Bundle extras, String caller, String periodicId) {
        if (caller != null && !app.getPackageName().equals(caller)) throw new SecurityException("foreign sync caller");
        Bundle copy = extras == null ? new Bundle() : new Bundle(extras);
        ContentResolver.validateSyncExtrasBundle(copy);
        Parcel parcel = Parcel.obtain();
        try { parcel.writeBundle(copy); if (parcel.dataSize() > 65_536) throw new IllegalArgumentException("sync extras too large"); }
        finally { parcel.recycle(); }
        Account[] visible = AccountManager.get(app).getAccounts();
        if (account != null && !Arrays.asList(visible).contains(account)) throw new SecurityException("sync account not visible");
        int count = 0;
        for (Account chosen : visible) {
            if (account != null && !account.equals(chosen)) continue;
            for (Adapter adapter : adapters) {
                if (!adapter.type.accountType.equals(chosen.type) || authority != null && !authority.equals(adapter.type.authority)) continue;
                requireAdapter(chosen, adapter.type.authority); // Refuse ambiguous declarations.
                if (jobs.size() >= 32) throw new IllegalStateException("sync queue full");
                Job job = new Job(++sequence, chosen, adapter, new Bundle(copy));
                job.periodicId = periodicId;
                jobs.add(job); status(job).pending = true; count++;
            }
        }
        trace("request=true matches=" + count);
        notifyStatus(ContentResolver.SYNC_OBSERVER_TYPE_PENDING);
        worker.execute(this::pump);
    }

    private void pump() {
        List<Job> start = new ArrayList<>();
        synchronized (LOCK) {
            for (Job j : new ArrayList<>(jobs)) {
                if (j.binding || j.done) continue;
                boolean occupied = jobs.stream().anyMatch(x -> x != j && x.binding && x.adapter.component.equals(j.adapter.component));
                if (occupied) continue;
                boolean manual = j.extras.getBoolean(ContentResolver.SYNC_EXTRAS_MANUAL)
                        || j.extras.getBoolean(ContentResolver.SYNC_EXTRAS_IGNORE_SETTINGS);
                int state = syncable(j.account, j.adapter.type.authority);
                if (state == 0 || state > 0 && !manual && (!settings.getBoolean("master", true)
                        || !settings.getBoolean(j.key + ".auto", false))) {
                    j.finish(null, "settings_disabled"); continue;
                }
                j.initializing = state < 0;
                j.binding = true; start.add(j);
            }
        }
        for (Job j : start) {
            j.timeout = timers.schedule(() -> j.cancel("timeout"), 120, TimeUnit.SECONDS);
            main.post(j::bind);
        }
    }

    private void cancel(Account account, String authority) {
        for (Job j : new ArrayList<>(jobs)) if (j.matches(account, authority)) j.cancel("cancelled");
    }

    private static boolean sameExtras(Bundle a, Bundle b) {
        if (b == null) return a.isEmpty();
        if (!a.keySet().equals(b.keySet())) return false;
        for (String key : a.keySet()) if (!java.util.Objects.deepEquals(a.get(key), b.get(key))) return false;
        return true;
    }

    private SyncStatusInfo status(Job j) { return statuses.computeIfAbsent(j.key, k -> new SyncStatusInfo(k.hashCode())); }

    private void notifyStatus(int mask) {
        for (Observer o : new ArrayList<>(observers.values())) if ((o.mask & mask) != 0) worker.execute(() -> {
            try { o.callback.onStatusChanged(mask); }
            catch (Exception dead) { synchronized (LOCK) { observers.remove(o.callback.asBinder()); } }
        });
    }

    private static String key(Account account, String authority) {
        if (account == null || authority == null || authority.isEmpty()) throw new IllegalArgumentException("sync account and authority required");
        try {
            byte[] bytes = MessageDigest.getInstance("SHA-256").digest((account.type + "\0" + account.name + "\0" + authority).getBytes(StandardCharsets.UTF_8));
            StringBuilder result = new StringBuilder(); for (byte b : bytes) result.append(String.format("%02x", b & 255)); return result.toString();
        } catch (java.security.NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
    }

    private Adapter find(Account account, String authority) {
        if (account == null || authority == null) return null;
        Adapter found = null;
        for (Adapter a : adapters) if (a.type.accountType.equals(account.type) && a.type.authority.equals(authority)) {
            if (found != null) throw new SecurityException("ambiguous sync adapter"); found = a;
        }
        return found;
    }
    private Adapter requireAdapter(Account account, String authority) {
        Adapter result = find(account, authority);
        if (result == null) throw new SecurityException("sync adapter not owned by this app");
        return result;
    }

    private void discover() {
        PackageManager pm = app.getPackageManager();
        List<ResolveInfo> found = pm.queryIntentServices(new Intent(ACTION).setPackage(app.getPackageName()), PackageManager.GET_META_DATA);
        if (found.size() > 64) throw new IllegalStateException("too many sync adapters");
        for (ResolveInfo r : found) {
            ServiceInfo s = r.serviceInfo;
            if (s == null || !s.enabled || !app.getPackageName().equals(s.packageName)) continue;
            try (XmlResourceParser xml = s.loadXmlMetaData(pm, ACTION)) {
                if (xml == null) continue;
                int event; do { event = xml.next(); } while (event != 2 && event != 1);
                if (event != 2 || !"sync-adapter".equals(xml.getName())) continue;
                TypedArray a = pm.getResourcesForApplication(s.applicationInfo).obtainAttributes(Xml.asAttributeSet(xml), com.android.internal.R.styleable.SyncAdapter);
                try {
                    String authority = a.getString(com.android.internal.R.styleable.SyncAdapter_contentAuthority);
                    String accountType = a.getString(com.android.internal.R.styleable.SyncAdapter_accountType);
                    if (authority == null || !authority.matches("[A-Za-z0-9_.-]{1,256}") || accountType == null || accountType.isEmpty() || accountType.length() > 256)
                        throw new IllegalArgumentException("invalid sync metadata");
                    SyncAdapterType type = new SyncAdapterType(authority, accountType,
                            a.getBoolean(com.android.internal.R.styleable.SyncAdapter_userVisible, true),
                            a.getBoolean(com.android.internal.R.styleable.SyncAdapter_supportsUploading, true),
                            a.getBoolean(com.android.internal.R.styleable.SyncAdapter_isAlwaysSyncable, false),
                            a.getBoolean(com.android.internal.R.styleable.SyncAdapter_allowParallelSyncs, false),
                            a.getString(com.android.internal.R.styleable.SyncAdapter_settingsActivity), s.packageName);
                    adapters.add(new Adapter(new ComponentName(s.packageName, s.name), type));
                    trace("adapter_discovered=true authority=" + authority + " service=" + s.name);
                } finally { a.recycle(); }
            } catch (Exception failure) { trace("discovery_failed=" + failure.getClass().getSimpleName()); }
        }
        trace("discovery_complete=true count=" + adapters.size());
    }

    private static void trace(String message) { System.err.println("[WL-SYNC] time=" + System.currentTimeMillis() + " " + message); }
    private static final class Adapter {
        final ComponentName component; final SyncAdapterType type;
        Adapter(ComponentName component, SyncAdapterType type) { this.component = component; this.type = type; }
    }
    private static final class Observer {
        final int mask; final ISyncStatusObserver callback;
        Observer(int mask, ISyncStatusObserver callback) { this.mask = mask; this.callback = callback; }
    }

    private final class Job implements ServiceConnection, IBinder.DeathRecipient {
        final long id;
        final Account account;
        final Adapter adapter;
        final Bundle extras;
        final String key;
        boolean binding, running, done, bound, initializing;
        long started;
        String cancellation, periodicId;
        ISyncAdapter remote;
        java.util.concurrent.ScheduledFuture<?> timeout;
        final SyncDispatchState dispatch = new SyncDispatchState();
        Attempt activeAttempt;
        final class Attempt extends ISyncContext.Stub {
            final SyncDispatchState.Ticket ticket;
            final boolean initialization;
            Attempt(SyncDispatchState.Ticket ticket, boolean initialization) {
                this.ticket = ticket; this.initialization = initialization;
            }
            @Override public void sendHeartbeat() { trace("heartbeat=true job=" + id); }
            @Override public void onFinished(SyncResult result) {
                worker.execute(() -> {
                    synchronized (LOCK) {
                        if (done || !dispatch.accept(ticket)) return;
                        activeAttempt = null;
                        if (cancellation == null && initialization && result != null && !result.hasError()
                                && syncable(account, adapter.type.authority) > 0) {
                            boolean manual = extras.getBoolean(ContentResolver.SYNC_EXTRAS_MANUAL)
                                    || extras.getBoolean(ContentResolver.SYNC_EXTRAS_IGNORE_SETTINGS);
                            if (!manual && (!settings.getBoolean("master", true)
                                    || !settings.getBoolean(key + ".auto", false))) {
                                finish(null, "settings_disabled"); return;
                            }
                            initializing = false; worker.execute(Job.this::start); return;
                        }
                        finish(result, cancellation != null ? cancellation : initializing ? "initialization_unresolved" : "adapter_finished");
                    }
                });
            }
        }
        Job(long id, Account account, Adapter adapter, Bundle extras) {
            this.id = id; this.account = account; this.adapter = adapter; this.extras = extras;
            key = key(account, adapter.type.authority);
        }
        boolean matches(Account a, String authority) {
            return (a == null || a.equals(account)) && (authority == null || authority.equals(adapter.type.authority));
        }
        void bind() {
            synchronized (LOCK) { if (done) return; }
            try {
                boolean accepted = app.bindService(new Intent(ACTION).setComponent(adapter.component), this, Context.BIND_AUTO_CREATE);
                synchronized (LOCK) {
                    bound = accepted;
                    if (accepted && done) main.post(() -> { try { app.unbindService(this); } catch (Exception ignored) {} });
                    else if (!accepted) finish(null, "bind_refused");
                }
            } catch (Exception error) { finish(null, "bind_failed"); }
        }
        @Override public void onServiceConnected(ComponentName component, IBinder binder) {
            worker.execute(() -> {
                try {
                    synchronized (LOCK) {
                        if (done) return;
                        if (!component.equals(adapter.component) || binder == null || !"android.content.ISyncAdapter".equals(binder.getInterfaceDescriptor()))
                            throw new SecurityException("wrong sync service");
                        remote = ISyncAdapter.Stub.asInterface(binder); binder.linkToDeath(this, 0);
                    }
                    start();
                } catch (Exception error) { finish(null, "connection_failed"); }
            });
        }
        void start() {
            try {
                if (!Arrays.asList(AccountManager.get(app).getAccounts()).contains(account)) { finish(null, "account_removed"); return; }
                Bundle request; Attempt attempt;
                synchronized (LOCK) {
                    if (done || cancellation != null) return;
                    SyncDispatchState.Ticket ticket = dispatch.begin();
                    if (ticket == null) return;
                    attempt = new Attempt(ticket, initializing); activeAttempt = attempt;
                    request = initializing ? new Bundle() : new Bundle(extras);
                    if (initializing) request.putBoolean(ContentResolver.SYNC_EXTRAS_INITIALIZE, true);
                    running = true; started = SystemClock.elapsedRealtime(); status(this).pending = false;
                    notifyStatus(ContentResolver.SYNC_OBSERVER_TYPE_ACTIVE | ContentResolver.SYNC_OBSERVER_TYPE_PENDING);
                    trace("start=true job=" + id + " authority=" + adapter.type.authority + " initialize=" + initializing);
                }
                remote.startSync(attempt, adapter.type.authority, account, request);
                synchronized (LOCK) {
                    // Cancellation can race the Binder call; repeat it after start returns.
                    if (cancellation != null) worker.execute(() -> {
                        try { remote.cancelSync(attempt); } catch (Exception ignored) {}
                    });
                }
            } catch (Exception failure) { finish(null, "start_failed"); }
        }
        void cancel(String reason) {
            synchronized (LOCK) {
                if (done || cancellation != null) return;
                cancellation = reason; dispatch.cancel();
                if (!running || remote == null) { finish(null, reason); return; }
            }
            // The adapter may block in cancelSync; its call cannot own the deadline.
            timers.schedule(() -> finish(null, reason), 10, TimeUnit.SECONDS);
            worker.execute(() -> {
                Attempt attempt;
                synchronized (LOCK) { attempt = activeAttempt; }
                try { if (attempt != null) remote.cancelSync(attempt); }
                catch (Exception dead) { finish(null, reason); }
            });
        }
        void finish(SyncResult result, String reason) {
            synchronized (LOCK) {
                if (done) return;
                done = true; dispatch.close(); jobs.remove(this); if (timeout != null) timeout.cancel(false);
                SyncStatusInfo value = status(this); value.pending = jobs.stream().anyMatch(j -> j.key.equals(key) && !j.running);
                boolean success = "adapter_finished".equals(reason) && result != null && !result.hasError();
                if (success) value.lastSuccessTime = System.currentTimeMillis();
                else { value.lastFailureTime = System.currentTimeMillis(); value.lastFailureMesg = reason; }
                if (remote != null) remote.asBinder().unlinkToDeath(this, 0);
                if (bound) main.post(() -> { try { app.unbindService(this); } catch (Exception ignored) {} });
                trace("finished=true job=" + id + " reason=" + reason + " success=" + success
                        + " auth_error=" + (result != null && result.stats.numAuthExceptions > 0)
                        + " io_error=" + (result != null && result.stats.numIoExceptions > 0));
                notifyStatus(ContentResolver.SYNC_OBSERVER_TYPE_ACTIVE | ContentResolver.SYNC_OBSERVER_TYPE_PENDING | ContentResolver.SYNC_OBSERVER_TYPE_STATUS);
            }
            worker.execute(LocalSyncService.this::pump);
        }
        @Override public void onServiceDisconnected(ComponentName name) { finish(null, "service_disconnected"); }
        @Override public void onBindingDied(ComponentName name) { finish(null, "binding_died"); }
        @Override public void onNullBinding(ComponentName name) { finish(null, "null_binding"); }
        @Override public void binderDied() { finish(null, "binder_died"); }
    }
}
