package adapter.activity;

import android.app.ActivityThread;
import android.app.Application;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.job.IJobCallback;
import android.app.job.IJobScheduler;
import android.app.job.IJobService;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.app.job.JobWorkItem;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ParceledListSlice;
import android.content.pm.ServiceInfo;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.PersistableBundle;
import android.os.Process;
import android.os.StatFs;
import android.os.SystemClock;

import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.HashSet;
import java.util.Set;
import java.util.Objects;

/**
 * The job scheduler, in process: a job the app schedules runs while the app runs, as
 * JobSchedulerService runs it. The scheduler before this accepted every job and ran none, so the
 * probe's job never ran and an app's background work (syncs, uploads, JobIntentService work)
 * waited forever.
 *
 * A job is ready once its minimum latency has passed and its constraints are met, or once its
 * deadline has passed. Its service is bound in process and the job started through the service's
 * JobServiceEngine, on the main thread as from the system. It finishes when onStartJob returns
 * false, when the app calls jobFinished, or when its last work item is dequeued. A job asked to be
 * rescheduled backs off as its policy says; a periodic one comes back in its next window; one that
 * runs past ten minutes is stopped, as a job its app started on top is. Scheduling a job with the id
 * of another replaces it and stops the old one if it runs; enqueue adds work to a job that is the
 * same, running or not. The app's view keeps Binder's value semantics: what it hands over and gets
 * back are copies.
 *
 * Constraints: the network one as ConnectivityController matches the active network to the job's
 * request; storage not low as the storage monitor counts it; charging and battery not low always
 * met, as the device reports itself charging and full. Device idle and content triggers are never
 * met: nothing here sees the device idle or watches content, so such a job runs only at its
 * deadline. Jobs live as long as the app's process, like its alarms: a persisted job is not kept
 * across a restart, and nothing starts the app for one.
 */
public final class LocalJobs {
    private LocalJobs() {}

    private static final String TAG = "[WL-JOBS]";
    private static final long NO_LATEST = Long.MAX_VALUE;
    // The longest the scheduler waits before it looks at the constraints again: the network comes
    // and goes without telling it, and its handler counts uptime, which stops while the device sleeps.
    private static final long RECHECK_MS = 60_000;
    // What JobSchedulerService guarantees a job its app started while on top, before it stops it.
    private static final long RUNTIME_LIMIT_MS = 10 * 60_000;
    // How long a job asked to stop has to answer before it is taken as stopped.
    private static final long STOP_TIMEOUT_MS = 8_000;
    private static final int MAX_JOBS = 150;   // JobSchedulerService.MAX_JOBS_PER_APP

    private static final Object sLock = new Object();
    private static final List<Job> sJobs = new ArrayList<>();
    private static final Map<ComponentName, IJobService> sServices = new HashMap<>();   // main thread
    /** Job services declared for another process that could not start there: bound here, as before. */
    private static final Set<ComponentName> sHere = new HashSet<>();   // main thread
    private static final Runnable EVALUATE = LocalJobs::evaluate;
    private static Handler sHandler;
    private static Handler sMain;
    private static Object sScheduler;

    private static final class Job {
        final String namespace;
        final JobInfo info;
        long earliest;          // elapsed realtime it may start at; 0 for any time
        long latest;            // its deadline, or a periodic job's window end; NO_LATEST for none
        int failures;
        final List<JobWorkItem> pendingWork = new ArrayList<>();
        final List<JobWorkItem> executingWork = new ArrayList<>();
        int nextWorkId = 1;
        Run run;

        Job(String namespace, JobInfo info, long now) {
            this.namespace = namespace;
            this.info = info;
            if (info.isPeriodic()) {
                long period = Math.max(JobInfo.getMinPeriodMillis(), info.getIntervalMillis());
                long flex = Math.max(JobInfo.getMinFlexMillis(), Math.min(period, info.getFlexMillis()));
                latest = now + period;
                earliest = latest - flex;
            } else {
                earliest = info.hasEarlyConstraint() ? now + info.getMinLatencyMillis() : 0;
                latest = info.hasLateConstraint() ? now + info.getMaxExecutionDelayMillis() : NO_LATEST;
            }
        }

        boolean deadlinePassed(long now) {
            return !info.isPeriodic() && latest != NO_LATEST && now >= latest;
        }

        @Override
        public String toString() {
            return "job " + info.getId() + (namespace != null ? " in " + namespace : "") + " ("
                    + info.getService().getShortClassName() + ")";
        }
    }

    private static final class Run {
        final Job job;
        final Callback callback = new Callback(this);
        JobParameters params;
        IJobService service;
        boolean stopping;
        boolean done;
        int notificationId;
        int notificationPolicy = -1;
        final Runnable timeout = () -> stop(this, JobParameters.STOP_REASON_TIMEOUT,
                JobParameters.INTERNAL_STOP_REASON_TIMEOUT, "ran past its time");
        final Runnable stopTimeout = () -> {
            synchronized (sLock) {
                finishLocked(this, true, "did not answer onStopJob in time");
            }
        };

        Run(Job job) {
            this.job = job;
        }
    }

    /** The answers a running job's JobParameters reach, as JobServiceContext's callback gives them. */
    private static final class Callback extends IJobCallback.Stub {
        private final Run run;

        Callback(Run run) {
            this.run = run;
        }

        @Override
        public void acknowledgeStartMessage(int jobId, boolean ongoing) {
            if (!ongoing) finish(run, false, "onStartJob returned false");
        }

        @Override
        public void acknowledgeStopMessage(int jobId, boolean reschedule) {
            finish(run, reschedule, "onStopJob returned " + reschedule);
        }

        @Override
        public JobWorkItem dequeueWork(int jobId) {
            return LocalJobs.dequeueWork(run);
        }

        @Override
        public boolean completeWork(int jobId, int workId) {
            return LocalJobs.completeWork(run, workId);
        }

        @Override
        public void jobFinished(int jobId, boolean reschedule) {
            finish(run, reschedule, "jobFinished");
        }

        @Override
        public void updateEstimatedNetworkBytes(int jobId, JobWorkItem item, long downloadBytes,
                long uploadBytes) {
        }

        @Override
        public void updateTransferredNetworkBytes(int jobId, JobWorkItem item,
                long transferredDownloadBytes, long transferredUploadBytes) {
        }

        @Override
        public void setNotification(int jobId, int notificationId, Notification notification,
                int jobEndNotificationPolicy) {
            LocalJobs.setNotification(run, notificationId, notification, jobEndNotificationPolicy);
        }

        @Override
        public void acknowledgeGetTransferredDownloadBytesMessage(int jobId, int workId,
                long transferredBytes) {
        }

        @Override
        public void acknowledgeGetTransferredUploadBytesMessage(int jobId, int workId,
                long transferredBytes) {
        }
    }

    /** The IJobScheduler that JobSchedulerImpl forwards the app's calls to. */
    public static synchronized Object scheduler() {
        if (sScheduler == null) {
            sScheduler = Proxy.newProxyInstance(LocalJobs.class.getClassLoader(),
                    new Class<?>[] {IJobScheduler.class}, (proxy, method, args) -> answer(proxy, method, args));
        }
        return sScheduler;
    }

    private static Object answer(Object proxy, Method method, Object[] args) {
        switch (method.getName()) {
            case "schedule":
                return schedule((String) args[0], (JobInfo) args[1], null);
            case "enqueue":
                if (args[2] == null) throw new NullPointerException("work is null");
                return schedule((String) args[0], (JobInfo) args[1], (JobWorkItem) args[2]);
            case "scheduleAsPackage":
                return schedule((String) args[0], (JobInfo) args[1], null);
            case "cancel":
                cancel((String) args[0], (Integer) args[1]);
                return null;
            case "cancelAll":
                cancelAll(null, false);
                return null;
            case "cancelAllInNamespace":
                cancelAll((String) args[0], true);
                return null;
            case "getAllPendingJobs":
                return pendingByNamespace();
            case "getAllPendingJobsInNamespace":
                return new ParceledListSlice<>(pending((String) args[0]));
            case "getPendingJob":
                return pendingJob((String) args[0], (Integer) args[1]);
            case "getPendingJobReason":
                return pendingReason((String) args[0], (Integer) args[1]);
            case "getStartedJobs":
                return started();
            case "getAllJobSnapshots":
                return ParceledListSlice.emptyList();
            case "canRunUserInitiatedJobs":
            case "hasRunUserInitiatedJobsPermission":
                return userInitiatedAllowed();
            case "asBinder":
                return null;
            case "hashCode":
                return System.identityHashCode(proxy);
            case "equals":
                return args != null && args.length == 1 && args[0] == proxy;
            case "toString":
                return "LocalJobs";
            default:
                return null;   // the user-visible job observers, which only the system registers
        }
    }

    private static Handler handler() {
        synchronized (LocalJobs.class) {
            if (sHandler == null) {
                HandlerThread thread = new HandlerThread("WL-Jobs");
                thread.start();
                sHandler = new Handler(thread.getLooper());
                sMain = new Handler(Looper.getMainLooper());
            }
            return sHandler;
        }
    }

    private static <T extends Parcelable> T copy(T value, Parcelable.Creator<T> creator) {
        Parcel parcel = Parcel.obtain();
        try {
            value.writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            return creator.createFromParcel(parcel);
        } finally {
            parcel.recycle();
        }
    }

    /**
     * JobSchedulerService's checks on a job's service: it is one of the app's own, declared, and
     * requires BIND_JOB_SERVICE. Disabled services are found too: they still run in process.
     */
    private static void validate(JobInfo info) {
        Application app = ActivityThread.currentApplication();
        ComponentName service = info.getService();
        if (app == null || service == null) return;
        if (!app.getPackageName().equals(service.getPackageName())) {
            throw new IllegalArgumentException("uid " + Process.myUid() + " cannot schedule job in "
                    + service.getPackageName());
        }
        ServiceInfo si;
        try {
            si = app.getPackageManager().getServiceInfo(service,
                    PackageManager.MATCH_DIRECT_BOOT_AWARE | PackageManager.MATCH_DIRECT_BOOT_UNAWARE
                            | PackageManager.MATCH_DISABLED_COMPONENTS);
        } catch (PackageManager.NameNotFoundException e) {
            si = null;
        }
        if (si == null) {
            throw new IllegalArgumentException("Tried to schedule job for non-existent component: " + service);
        }
        if (!JobService.PERMISSION_BIND.equals(si.permission)) {
            throw new IllegalArgumentException("Scheduled service " + service
                    + " does not require android.permission.BIND_JOB_SERVICE permission");
        }
    }

    private static int schedule(String namespace, JobInfo info, JobWorkItem work) {
        validate(info);
        info = copy(info, JobInfo.CREATOR);
        if (work != null) work = copy(work, JobWorkItem.CREATOR);
        Job job;
        synchronized (sLock) {
            Job old = find(namespace, info.getId());
            if (work != null && old != null && old.info.equals(info)) {
                // The job as it is takes the work: a running one dequeues it.
                addWork(old, work);
                System.err.println(TAG + " work added to " + old);
                kick();
                return JobScheduler.RESULT_SUCCESS;
            }
            if (old == null && sJobs.size() >= MAX_JOBS) {
                throw new IllegalStateException("Apps may not schedule more than " + MAX_JOBS + " distinct jobs");
            }
            job = new Job(namespace, info, SystemClock.elapsedRealtime());
            if (old != null) {
                // The new job replaces the old one and takes over its work; a running one is stopped.
                job.pendingWork.addAll(old.executingWork);
                job.pendingWork.addAll(old.pendingWork);
                job.nextWorkId = old.nextWorkId;
                old.executingWork.clear();
                old.pendingWork.clear();
                removeLocked(old, JobParameters.STOP_REASON_CANCELLED_BY_APP, "job rescheduled by app");
            }
            if (work != null) addWork(job, work);
            sJobs.add(job);
            kick();
        }
        System.err.println(TAG + " scheduled " + job + describe(job));
        return JobScheduler.RESULT_SUCCESS;
    }

    private static String describe(Job job) {
        StringBuilder out = new StringBuilder();
        long now = SystemClock.elapsedRealtime();
        if (job.earliest > now) out.append(", in ").append(job.earliest - now).append(" ms");
        if (job.latest != NO_LATEST) out.append(job.info.isPeriodic() ? ", every " : ", deadline in ")
                .append(job.info.isPeriodic() ? job.info.getIntervalMillis() : job.latest - now).append(" ms");
        if (job.info.getRequiredNetwork() != null) out.append(", needs a network");
        if (job.info.isRequireDeviceIdle()) out.append(", needs the device idle");
        if (job.info.getTriggerContentUris() != null) out.append(", on content changes");
        if (!job.pendingWork.isEmpty()) out.append(", ").append(job.pendingWork.size()).append(" work item(s)");
        return out.toString();
    }

    /** With sLock held. */
    private static void addWork(Job job, JobWorkItem work) {
        work.setWorkId(job.nextWorkId++);
        job.pendingWork.add(work);
    }

    /** With sLock held. */
    private static Job find(String namespace, int id) {
        for (Job job : sJobs) {
            if (job.info.getId() == id && Objects.equals(job.namespace, namespace)) return job;
        }
        return null;
    }

    private static void cancel(String namespace, int id) {
        synchronized (sLock) {
            Job job = find(namespace, id);
            if (job != null) removeLocked(job, JobParameters.STOP_REASON_CANCELLED_BY_APP, "cancel() called by app");
        }
    }

    private static void cancelAll(String namespace, boolean onlyThatNamespace) {
        synchronized (sLock) {
            for (Job job : new ArrayList<>(sJobs)) {
                if (!onlyThatNamespace || Objects.equals(job.namespace, namespace)) {
                    removeLocked(job, JobParameters.STOP_REASON_CANCELLED_BY_APP, "cancel() called by app");
                }
            }
        }
    }

    /** With sLock held: the job is no longer scheduled; if it runs, it is stopped. */
    private static void removeLocked(Job job, int reason, String why) {
        sJobs.remove(job);
        if (job.run != null) stop(job.run, reason, JobParameters.INTERNAL_STOP_REASON_CANCELED, why);
        kick();
    }

    private static List<JobInfo> pending(String namespace) {
        List<JobInfo> out = new ArrayList<>();
        synchronized (sLock) {
            for (Job job : sJobs) {
                if (Objects.equals(job.namespace, namespace)) out.add(copy(job.info, JobInfo.CREATOR));
            }
        }
        return out;
    }

    private static Map<String, ParceledListSlice<JobInfo>> pendingByNamespace() {
        Map<String, List<JobInfo>> grouped = new HashMap<>();
        synchronized (sLock) {
            for (Job job : sJobs) {
                grouped.computeIfAbsent(job.namespace, n -> new ArrayList<>()).add(copy(job.info, JobInfo.CREATOR));
            }
        }
        Map<String, ParceledListSlice<JobInfo>> out = new HashMap<>();
        for (Map.Entry<String, List<JobInfo>> entry : grouped.entrySet()) {
            out.put(entry.getKey(), new ParceledListSlice<>(entry.getValue()));
        }
        return out;
    }

    private static JobInfo pendingJob(String namespace, int id) {
        synchronized (sLock) {
            Job job = find(namespace, id);
            return job != null ? copy(job.info, JobInfo.CREATOR) : null;
        }
    }

    private static int pendingReason(String namespace, int id) {
        synchronized (sLock) {
            Job job = find(namespace, id);
            if (job == null) return JobScheduler.PENDING_JOB_REASON_INVALID_JOB_ID;
            return waitingFor(job, SystemClock.elapsedRealtime(), new Conditions());
        }
    }

    private static List<JobInfo> started() {
        List<JobInfo> out = new ArrayList<>();
        synchronized (sLock) {
            for (Job job : sJobs) {
                if (job.run != null) out.add(copy(job.info, JobInfo.CREATOR));
            }
        }
        return out;
    }

    private static boolean userInitiatedAllowed() {
        Application app = ActivityThread.currentApplication();
        return app != null && app.checkSelfPermission("android.permission.RUN_USER_INITIATED_JOBS")
                == PackageManager.PERMISSION_GRANTED;
    }

    /** The device's state as the constraints read it, looked at once per pass. */
    private static final class Conditions {
        private boolean networkRead;
        private Network network;
        private NetworkCapabilities capabilities;
        private Boolean storageLow;

        /**
         * The network a job asking for this one runs with, as ConnectivityController matches the
         * request: the active network, when it has every capability and one of the transports asked
         * for. Whether a network is managed by a VCN is not something OH's networks say.
         */
        Network networkFor(NetworkRequest request) {
            if (!networkRead) {
                networkRead = true;
                try {
                    ConnectivityManager connectivity = ActivityThread.currentApplication()
                            .getSystemService(ConnectivityManager.class);
                    network = connectivity.getActiveNetwork();
                    capabilities = network != null ? connectivity.getNetworkCapabilities(network) : null;
                } catch (Throwable t) {
                    System.err.println(TAG + " the active network could not be read: " + t);
                }
            }
            if (capabilities == null) return null;
            for (int capability : request.getCapabilities()) {
                if (capability == NetworkCapabilities.NET_CAPABILITY_NOT_VCN_MANAGED) continue;
                if (!capabilities.hasCapability(capability)) return null;
            }
            int[] transports = request.getTransportTypes();
            if (transports.length == 0) return network;
            for (int transport : transports) {
                if (capabilities.hasTransport(transport)) return network;
            }
            return null;
        }

        /** As StorageManager.getStorageLowBytes counts it: under 5% free, at most 500 MB. */
        boolean storageLow() {
            if (storageLow == null) {
                storageLow = Boolean.FALSE;
                try {
                    StatFs fs = new StatFs(ActivityThread.currentApplication().getDataDir().getPath());
                    long low = Math.min(fs.getTotalBytes() * 5 / 100, 500L << 20);
                    storageLow = fs.getAvailableBytes() < low;
                } catch (Throwable t) {
                    System.err.println(TAG + " storage could not be read: " + t);
                }
            }
            return storageLow;
        }
    }

    /** What a job waits for, as getPendingJobReason answers; UNDEFINED once it is ready. */
    private static int waitingFor(Job job, long now, Conditions conditions) {
        if (job.run != null) return JobScheduler.PENDING_JOB_REASON_EXECUTING;
        if (now < job.earliest) return JobScheduler.PENDING_JOB_REASON_CONSTRAINT_MINIMUM_LATENCY;
        if (job.deadlinePassed(now)) return JobScheduler.PENDING_JOB_REASON_UNDEFINED;
        JobInfo info = job.info;
        if (info.isRequireDeviceIdle()) return JobScheduler.PENDING_JOB_REASON_CONSTRAINT_DEVICE_IDLE;
        if (info.getTriggerContentUris() != null) return JobScheduler.PENDING_JOB_REASON_CONSTRAINT_CONTENT_TRIGGER;
        if (info.isRequireStorageNotLow() && conditions.storageLow()) {
            return JobScheduler.PENDING_JOB_REASON_CONSTRAINT_STORAGE_NOT_LOW;
        }
        if (info.getRequiredNetwork() != null && conditions.networkFor(info.getRequiredNetwork()) == null) {
            return JobScheduler.PENDING_JOB_REASON_CONSTRAINT_CONNECTIVITY;
        }
        return JobScheduler.PENDING_JOB_REASON_UNDEFINED;
    }

    /** With sLock held: the scheduler looks at the jobs again, now. */
    private static void kick() {
        Handler handler = handler();
        handler.removeCallbacks(EVALUATE);
        handler.post(EVALUATE);
    }

    /** On the scheduler's thread: starts every job that is ready, then waits for the next. */
    private static void evaluate() {
        synchronized (sLock) {
            long now = SystemClock.elapsedRealtime();
            Conditions conditions = new Conditions();
            long wait = Long.MAX_VALUE;
            for (Job job : sJobs) {
                if (job.run != null) continue;
                int reason = waitingFor(job, now, conditions);
                if (reason == JobScheduler.PENDING_JOB_REASON_UNDEFINED) {
                    startLocked(job, now, conditions);
                } else if (reason == JobScheduler.PENDING_JOB_REASON_CONSTRAINT_MINIMUM_LATENCY) {
                    wait = Math.min(wait, job.earliest - now);
                } else {
                    wait = Math.min(wait, job.latest != NO_LATEST && !job.info.isPeriodic()
                            ? Math.max(0, job.latest - now) : RECHECK_MS);
                }
            }
            if (wait != Long.MAX_VALUE) handler().postDelayed(EVALUATE, Math.min(wait, RECHECK_MS));
        }
    }

    /** With sLock held: the job's run begins, on the main thread as JobServiceContext binds. */
    private static void startLocked(Job job, long now, Conditions conditions) {
        Run run = new Run(job);
        JobInfo info = job.info;
        Network network = info.getRequiredNetwork() != null ? conditions.networkFor(info.getRequiredNetwork()) : null;
        PersistableBundle extras = info.getExtras() != null ? new PersistableBundle(info.getExtras()) : null;
        Bundle transientExtras = info.getTransientExtras() != null ? new Bundle(info.getTransientExtras()) : null;
        run.params = new JobParameters(run.callback, job.namespace, info.getId(), extras, transientExtras,
                info.getClipData(), info.getClipGrantFlags(), job.deadlinePassed(now), info.isExpedited(),
                info.isUserInitiated(), null, null, network);
        job.run = run;
        handler().postDelayed(run.timeout, RUNTIME_LIMIT_MS);
        sMain.post(() -> begin(run));
    }

    private static void begin(Run run) {
        ComponentName component = run.job.info.getService();
        IJobService known = sServices.get(component);
        if (known != null && !known.asBinder().isBinderAlive()) sServices.remove(component);   // its process ended
        if (!sServices.containsKey(component) && !sHere.contains(component)) {
            // A job service declared for another of the app's processes runs there (ADR-0002), bound from a
            // thread since that process may have to start; the job begins here once it is.
            String process = InProcessServiceBinder.declaredProcess(component);
            if (!AppProcesses.runsHere(process)) {
                Intent bind = new Intent().setComponent(component).setFlags(Intent.FLAG_FROM_BACKGROUND);
                Thread thread = new Thread(() -> {
                    IBinder binder = PlayServicesChannel.bindInOwnProcess(process, bind);
                    sMain.post(() -> {
                        IJobService remote = binder == null ? null : IJobService.Stub.asInterface(binder);
                        if (remote != null) {
                            sServices.put(component, remote);
                        } else {
                            sHere.add(component);
                        }
                        begin(run);
                    });
                }, "app-process-job");
                thread.setDaemon(true);
                thread.start();
                return;
            }
        }
        IJobService service;
        try {
            service = service(run.job.info.getService());
        } catch (Throwable t) {
            System.err.println(TAG + " " + run.job + ": its service could not be bound: " + t);
            finish(run, true, "its service could not be bound");
            return;
        }
        synchronized (sLock) {
            if (run.done) return;   // stopped before it began
            run.service = service;
        }
        System.err.println(TAG + " started " + run.job);
        try {
            service.startJob(run.params);
        } catch (Throwable t) {
            System.err.println(TAG + " " + run.job + " could not be started: " + t);
            finish(run, true, "it could not be started");
        }
    }

    /** Main thread: the job service bound once, as the system keeps a binding while jobs run. */
    private static IJobService service(ComponentName component) throws Exception {
        IJobService service = sServices.get(component);
        if (service == null) {
            Intent bind = new Intent().setComponent(component).setFlags(Intent.FLAG_FROM_BACKGROUND);
            IBinder binder = InProcessServiceBinder.bindForSystem(component, bind);
            service = IJobService.Stub.asInterface(binder);
            if (service == null) throw new IllegalStateException(component + " answered onBind with no binder");
            sServices.put(component, service);
        }
        return service;
    }

    /** The job is asked to stop (onStopJob); it is taken as stopped if it does not answer. */
    private static void stop(Run run, int reason, int internalReason, String why) {
        IJobService service;
        synchronized (sLock) {
            if (run.done || run.stopping) return;
            run.stopping = true;
            run.params.setStopReason(reason, internalReason, why);
            if (run.service == null) {
                finishLocked(run, false, why + ", before it began");
                return;
            }
            service = run.service;
            handler().postDelayed(run.stopTimeout, STOP_TIMEOUT_MS);
        }
        System.err.println(TAG + " stopping " + run.job + ": " + why);
        try {
            service.stopJob(run.params);
        } catch (Throwable t) {
            finish(run, false, "onStopJob could not be called: " + t);
        }
    }

    private static void finish(Run run, boolean reschedule, String why) {
        synchronized (sLock) {
            finishLocked(run, reschedule, why);
        }
    }

    /** With sLock held: the run is over; the job is dropped, backs off, or waits for its next period. */
    private static void finishLocked(Run run, boolean reschedule, String why) {
        if (run.done) return;
        run.done = true;
        Handler handler = handler();
        handler.removeCallbacks(run.timeout);
        handler.removeCallbacks(run.stopTimeout);
        Job job = run.job;
        if (job.run == run) job.run = null;
        long now = SystemClock.elapsedRealtime();
        String next;
        if (!sJobs.contains(job)) {
            next = "cancelled";
        } else if (reschedule) {
            job.failures++;
            long delay = backoff(job.info, job.failures);
            job.earliest = now + delay;
            if (!job.info.isPeriodic()) job.latest = NO_LATEST;
            job.pendingWork.addAll(0, job.executingWork);
            job.executingWork.clear();
            next = "again in " + delay + " ms";
        } else if (job.info.isPeriodic()) {
            // As JobSchedulerService reschedules a periodic job: its next window, skipping the ones
            // it ran past.
            long period = Math.max(JobInfo.getMinPeriodMillis(), job.info.getIntervalMillis());
            long flex = Math.max(JobInfo.getMinFlexMillis(), Math.min(period, job.info.getFlexMillis()));
            long windows = now > job.latest ? (now - job.latest) / period + 1 : 1;
            job.latest += windows * period;
            job.earliest = job.latest - flex;
            job.failures = 0;
            next = "next window in " + (job.earliest - now) + " ms";
        } else {
            sJobs.remove(job);
            job.pendingWork.clear();
            job.executingWork.clear();
            next = "done";
        }
        if (run.notificationPolicy == JobService.JOB_END_NOTIFICATION_POLICY_REMOVE) {
            try {
                ActivityThread.currentApplication().getSystemService(NotificationManager.class)
                        .cancel(run.notificationId);
            } catch (Throwable t) {
                System.err.println(TAG + " its notification could not be removed: " + t);
            }
        }
        System.err.println(TAG + " finished " + job + " (" + why + "): " + next);
        kick();
    }

    /** JobSchedulerService's backoff: linear or exponential from at least 10 s, at most 5 h. */
    private static long backoff(JobInfo info, int attempts) {
        long initial = Math.max(info.getInitialBackoffMillis(), JobInfo.MIN_BACKOFF_MILLIS);
        long delay = info.getBackoffPolicy() == JobInfo.BACKOFF_POLICY_LINEAR
                ? initial * attempts : (long) Math.scalb((double) initial, attempts - 1);
        return Math.min(delay, JobInfo.MAX_BACKOFF_DELAY_MILLIS);
    }

    /** JobServiceContext.doDequeueWork: the next work item; once none is left, the job is done. */
    private static JobWorkItem dequeueWork(Run run) {
        synchronized (sLock) {
            if (run.done || run.stopping) return null;
            Job job = run.job;
            if (job.pendingWork.isEmpty()) {
                if (job.executingWork.isEmpty()) {
                    run.params.setStopReason(JobParameters.STOP_REASON_UNDEFINED,
                            JobParameters.INTERNAL_STOP_REASON_SUCCESSFUL_FINISH, "last work dequeued");
                    finishLocked(run, false, "last work dequeued");
                }
                return null;
            }
            JobWorkItem work = job.pendingWork.remove(0);
            job.executingWork.add(work);
            work.bumpDeliveryCount();
            return copy(work, JobWorkItem.CREATOR);
        }
    }

    private static boolean completeWork(Run run, int workId) {
        synchronized (sLock) {
            for (Iterator<JobWorkItem> it = run.job.executingWork.iterator(); it.hasNext(); ) {
                if (it.next().getWorkId() == workId) {
                    it.remove();
                    return true;
                }
            }
            return false;
        }
    }

    /** A user-initiated job's notification, posted for the app; removed at its end if it asks. */
    private static void setNotification(Run run, int id, Notification notification, int policy) {
        synchronized (sLock) {
            if (run.done) return;
            run.notificationId = id;
            run.notificationPolicy = policy;
        }
        try {
            ActivityThread.currentApplication().getSystemService(NotificationManager.class).notify(id, notification);
        } catch (Throwable t) {
            System.err.println(TAG + " " + run.job + ": its notification could not be posted: " + t);
        }
    }
}
