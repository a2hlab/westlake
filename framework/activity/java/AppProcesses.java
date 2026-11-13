package adapter.activity;

import android.content.pm.ComponentInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.UnixSocketAddress;

import java.io.FileDescriptor;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * An app's processes (ADR-0002). An Android app may declare components in processes of their own
 * (android:process). Westlake ran all of an app's components in the one process it launched, so code
 * that keys on its process name took the wrong branch there: the Play Store builds a different Dagger
 * component per process name, and every component it declares for its :background process failed to
 * inject in the main one (its downloads, Age Signals, Engage, the key transfer for PairIP-encrypted
 * apps, and the MultiUserCoordinatorService its own main process waits on).
 *
 * Here a component declared for another process runs in that process. The process the app was
 * launched as starts it the first time one of its components is needed: it sends its own appspawn-x
 * the request that spawned it (the launcher leaves it at WESTLAKE_SPAWN_REQUEST, and the zygote's
 * socket at WESTLAKE_SPAWN_SOCKET) under the other process's name, as Android's activity manager
 * starts a process on demand. The child is forked from the same zygote, so it has the app's runtime,
 * data and identity; it binds the same Application under its own name, starts no activity, and
 * serves its components on a socket of its own (socketOf), as a host does (PlayServicesChannel).
 * Calls for its components reach it there: from the app's other processes, and from other apps,
 * which connect to it with their own identity, as on Android.
 *
 * Off unless WESTLAKE_APP_PROCESSES=1: then everything runs in the launched process, as before.
 */
public final class AppProcesses {
    static final String ENABLE = "WESTLAKE_APP_PROCESSES";
    private static final String SPAWN_SOCKET = "WESTLAKE_SPAWN_SOCKET";
    private static final String SPAWN_REQUEST = "WESTLAKE_SPAWN_REQUEST";
    private static final String PLAY_SERVICES_SERVE = "WESTLAKE_PLAY_SERVICES_SERVE";
    private static final String PLAY_STORE_SERVE = "WESTLAKE_PLAY_STORE_SERVE";
    private static final String TAG = "[WL-PROC] ";

    // OH's AppSpawnMsg (appspawn-x spawn_msg.h, packed to 4): magic, type, length, id and TLV count,
    // then the 256-byte process name; the response is that header, then the result and the pid.
    private static final int ID_OFFSET = 12;
    private static final int NAME_OFFSET = 20;
    private static final int NAME_LENGTH = 256;
    private static final int RESULT_OFFSET = NAME_OFFSET + NAME_LENGTH;
    private static final int RESPONSE_MIN = RESULT_OFFSET + 8;
    private static final int RESPONSE_MAX = 512;
    /** A process binds the application before it serves: Play's ran its Application for seconds. */
    private static final long START_TIMEOUT_MS = 90_000;

    private static volatile String sPackage;
    private static volatile String sHome;
    private static volatile String sSelf;
    private static volatile boolean sSecondary;
    private static volatile Set<String> sDeclared = new HashSet<>();
    /** Process name -> pid, for the processes this one started. Guarded by the class. */
    private static final Map<String, Integer> sStarted = new HashMap<>();
    /** Process name -> when it may be tried again, after it failed to start. Guarded by the class. */
    private static final Map<String, Long> sFailed = new HashMap<>();
    /** A process that did not come up is not started again for this long; its components run here meanwhile. */
    private static final long RETRY_AFTER_MS = 5 * 60_000;
    private static int sRequests;

    private AppProcesses() {}

    public static boolean enabled() {
        return "1".equals(System.getenv(ENABLE));
    }

    /**
     * Says which of the app's processes this is, before the application is bound. The launched process
     * was spawned under the host's OH name; one this app started was spawned under its own process
     * name, one of the processes the package declares.
     */
    public static void identify(String packageName, String launchedProcess) {
        sPackage = packageName;
        sHome = launchedProcess != null ? launchedProcess : packageName;
        sSelf = sHome;
        if (!enabled()) return;
        sDeclared = declaredProcesses(packageName);
        String spawned = spawnedName();
        if (spawned != null && !spawned.equals(sHome) && sDeclared.contains(spawned)) {
            sSelf = spawned;
            sSecondary = true;
            System.err.println(TAG + "this process is " + spawned + ", beside " + sHome);
        } else {
            System.err.println(TAG + "this process is " + sHome + "; the package declares " + sDeclared);
        }
    }

    public static boolean isSecondary() {
        return sSecondary;
    }

    public static String self() {
        return sSelf;
    }

    public static String home() {
        return sHome;
    }

    /** The process a component runs in: its own android:process, else its app's. */
    public static String processOf(ComponentInfo info) {
        if (info == null) return null;
        return info.processName != null && !info.processName.isEmpty() ? info.processName : info.packageName;
    }

    /** Whether a component of this app declared for process runs here. Everything does when this is off. */
    public static boolean runsHere(String process) {
        String self = sSelf;
        return !enabled() || self == null || process == null || process.equals(self);
    }

    /**
     * The socket a process serves on: a host's (Play services', the Play Store's) beside the host's own
     * socket, which its launched process serves, so that other apps find them from the name they
     * connect to; any other app's beside its appspawn-x socket, which is unique to the launch.
     */
    public static String socketOf(String process) {
        String host = hostSocket();
        if (host != null) return process.equals(sHome) ? host : host + "/" + process;
        String spawn = System.getenv(SPAWN_SOCKET);
        String base = spawn == null ? "wl.app." + sPackage : spawn.substring(spawn.lastIndexOf('/') + 1);
        return base + "/" + process;
    }

    /** The socket of another app's process, from the socket a client connects to for that app. */
    public static String socketOf(String hostSocket, String packageName, String process) {
        return process == null || process.equals(packageName) ? hostSocket : hostSocket + "/" + process;
    }

    static String hostSocket() {
        String name = System.getenv(PLAY_SERVICES_SERVE);
        return name != null ? name : System.getenv(PLAY_STORE_SERVE);
    }

    /** Whether process is one this app declares (and so one a caller may ask this app to start). */
    public static boolean declares(String process) {
        return process != null && sDeclared.contains(process);
    }

    /**
     * Makes sure process is running and serving, starting it if it isn't. Only the launched process
     * starts the others; the others ask it (PlayServicesChannel ENSURE_PROCESS). Blocks while the
     * process starts, so never call it on the main thread.
     */
    public static void ensure(String process) throws IOException {
        if (process == null || process.equals(sSelf)) return;
        if (sSecondary) throw new IllegalStateException(sSelf + " does not start processes; " + sHome + " does");
        if (!declares(process)) throw new IllegalArgumentException(sPackage + " declares no process " + process);
        synchronized (AppProcesses.class) {
            String socket = socketOf(process);
            Integer known = sStarted.get(process);
            if (known != null && alive(known) && serving(socket)) return;
            Long retry = sFailed.get(process);
            if (retry != null && System.currentTimeMillis() < retry) {
                throw new IOException(process + " failed to start; not tried again yet");
            }
            PlayServicesChannel.serveLaunchedProcess();
            int pid = spawn(process);
            sStarted.put(process, pid);
            long deadline = System.currentTimeMillis() + START_TIMEOUT_MS;
            while (!serving(socket)) {
                String failure = !alive(pid) ? " ended before it served"
                        : System.currentTimeMillis() > deadline ? " did not serve on " + socket : null;
                if (failure != null) {
                    sFailed.put(process, System.currentTimeMillis() + RETRY_AFTER_MS);
                    if (alive(pid)) kill(pid);
                    throw new IOException(process + " (pid " + pid + ")" + failure);
                }
                try {
                    Thread.sleep(200);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException(e);
                }
            }
            sFailed.remove(process);
            System.err.println(TAG + process + " (pid " + pid + ") serves on " + socket);
        }
    }

    /**
     * Whether pid is a live process. Signal 0, not /proc: OH may hide /proc entries, and right after the
     * fork the child is root's until appspawn-x specializes it, which answers EPERM, alive all the same.
     */
    private static boolean alive(int pid) {
        try {
            Os.kill(pid, 0);
            return true;
        } catch (ErrnoException e) {
            return e.errno != OsConstants.ESRCH;
        }
    }

    private static void kill(int pid) {
        try {
            Os.kill(pid, OsConstants.SIGKILL);
        } catch (ErrnoException ignored) {
        }
    }

    /** Asks this app's appspawn-x for process: the request that spawned this app, under that name. */
    private static int spawn(String process) throws IOException {
        String socket = System.getenv(SPAWN_SOCKET);
        String requestPath = System.getenv(SPAWN_REQUEST);
        if (socket == null || requestPath == null) {
            throw new IOException("launched without " + SPAWN_SOCKET + " and " + SPAWN_REQUEST);
        }
        byte[] request = Files.readAllBytes(Paths.get(requestPath));
        byte[] name = process.getBytes(StandardCharsets.UTF_8);
        if (request.length < RESULT_OFFSET || name.length >= NAME_LENGTH) {
            throw new IOException("cannot name a request for " + process);
        }
        Arrays.fill(request, NAME_OFFSET, NAME_OFFSET + NAME_LENGTH, (byte) 0);
        System.arraycopy(name, 0, request, NAME_OFFSET, name.length);
        ByteBuffer.wrap(request).order(ByteOrder.LITTLE_ENDIAN).putInt(ID_OFFSET, 0x57500000 + ++sRequests);
        FileDescriptor fd = null;
        try {
            fd = Os.socket(OsConstants.AF_UNIX, OsConstants.SOCK_STREAM, 0);
            Os.connect(fd, UnixSocketAddress.createFileSystem(socket));
            for (int sent = 0; sent < request.length; ) {
                sent += Os.write(fd, request, sent, request.length - sent);
            }
            byte[] response = new byte[RESPONSE_MAX];
            int got = 0;
            while (got < RESPONSE_MIN) {
                int n = Os.read(fd, response, got, response.length - got);
                if (n <= 0) break;
                got += n;
            }
            if (got < RESPONSE_MIN) throw new IOException("appspawn-x answered " + got + " bytes for " + process);
            ByteBuffer in = ByteBuffer.wrap(response).order(ByteOrder.LITTLE_ENDIAN);
            int result = in.getInt(RESULT_OFFSET);
            int pid = in.getInt(RESULT_OFFSET + 4);
            if (result != 0 || pid <= 0) throw new IOException("appspawn-x refused " + process + ": result " + result);
            System.err.println(TAG + "started " + process + " pid=" + pid);
            return pid;
        } catch (ErrnoException e) {
            throw new IOException("asking appspawn-x for " + process + ": " + e, e);
        } finally {
            if (fd != null) {
                try {
                    Os.close(fd);
                } catch (ErrnoException ignored) {
                }
            }
        }
    }

    /** Whether something accepts connections on an abstract socket name. */
    static boolean serving(String socket) {
        try {
            ParcelFileDescriptor.adoptFd(adapter.core.SocketBinder.connect(socket)).close();
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    /**
     * The name this process was spawned under (AppSpawnXInit.initChild records it). Not argv[0]: on the
     * board /proc/self/cmdline still showed the zygote's command line there, so every process the store
     * started took itself for the launched one and started another.
     */
    private static String spawnedName() {
        return com.android.internal.os.AppSpawnXInit.sSpawnName;
    }

    /** This app's process names, for maySignal: sDeclared, or, with app processes off, read once here. */
    private static volatile Set<String> sOwnNames;

    /**
     * Whether this process may send pid a signal (Process.sendSignal, so killProcess too): when pid runs under
     * one of this app's process names, or this process forked it. On Android the kernel decides by uid, and
     * each app's uid is its own. Every Westlake process runs under the one OH uid, so the kernel lets any of
     * them signal any other: imo's SIGKILL ended the shared Play services host. libwl_missing_natives asks
     * before each signal to another process, and a refused one is not sent, as Android's kernel refuses it
     * (EPERM, which sendSignal ignores). A refusal is logged with the stack that sent it.
     */
    public static boolean maySignal(int pid, int signal) {
        String app = sPackage;
        String name = commandName(pid);
        if (app == null || name == null) return true;   // not an app yet, or pid gone: as before
        Set<String> own = sOwnNames;
        if (own == null) {
            own = !sDeclared.isEmpty() ? sDeclared : declaredProcesses(app);
            sOwnNames = own;
        }
        if (name.equals(app) || name.startsWith(app + ":") || own.contains(name) || parentOf(pid) == Os.getpid()) {
            return true;
        }
        new Throwable("[WESTLAKE-KILL] refused signal " + signal + " to pid " + pid + " (" + name
                + "): not a process of " + app).printStackTrace();
        return false;
    }

    /** The name pid runs under: its command line's first argument; null if it can't be read. */
    private static String commandName(int pid) {
        try {
            byte[] line = Files.readAllBytes(Paths.get("/proc/" + pid + "/cmdline"));
            int end = 0;
            while (end < line.length && line[end] != 0) end++;
            return end == 0 ? null : new String(line, 0, end, StandardCharsets.UTF_8);
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    /** pid's parent, from /proc/<pid>/stat (the field after the parenthesized name and state); -1 if unknown. */
    private static int parentOf(int pid) {
        try {
            String stat = new String(Files.readAllBytes(Paths.get("/proc/" + pid + "/stat")), StandardCharsets.UTF_8);
            String[] fields = stat.substring(stat.lastIndexOf(')') + 2).split(" ");
            return Integer.parseInt(fields[1]);
        } catch (IOException | RuntimeException e) {
            return -1;
        }
    }

    /** The process names the package's components declare, its own name included. */
    static Set<String> declaredProcesses(String packageName) {
        Set<String> names = new HashSet<>();
        names.add(packageName);
        try {
            PackageInfo info = adapter.packagemanager.SourcePackageRegistry.packageInfo(packageName,
                    PackageManager.GET_ACTIVITIES | PackageManager.GET_SERVICES | PackageManager.GET_RECEIVERS
                            | PackageManager.GET_PROVIDERS);
            if (info == null) return names;
            for (ComponentInfo[] group : new ComponentInfo[][] {info.activities, info.services, info.receivers,
                    info.providers}) {
                if (group == null) continue;
                for (ComponentInfo component : group) {
                    String process = processOf(component);
                    if (process != null) names.add(process);
                }
            }
        } catch (Throwable t) {
            System.err.println(TAG + "processes of " + packageName + " unknown: " + t);
        }
        return names;
    }
}
