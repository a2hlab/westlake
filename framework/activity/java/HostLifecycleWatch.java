package adapter.activity;

import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import java.io.FileDescriptor;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.json.JSONObject;

/**
 * Ends this app process when the OpenHarmony host instance whose window it draws in ends.
 *
 * In a development launch the app's windows are sub-windows of its host ability's main window (WL_PARENT_ID).
 * Under memory pressure OpenHarmony kills a background host like any small HAP, but not the much larger Android
 * process, which keeps its memory and can never show a window again: the window it attaches to is gone, and a new
 * host instance has another. Android's low-memory killer ends the app process itself; this ends it with the host
 * instance, so the memory is freed and the next launch starts clean.
 *
 * Host and app processes are not dumpable and /proc is mounted hidepid=invisible, so the app cannot find its host
 * in /proc. The host records its pid and main window id in window-state.json in its files directory. When that
 * window is this app's parent, the watch checks the pid with signal 0, which needs only the shared uid and signals
 * nothing. A host that does not record its pid is not watched.
 *
 * A launch that serves other apps (Play services, the Play Store, the account broker) keeps running: other apps
 * depend on its services. WESTLAKE_EXIT_WITH_HOST=0 turns the watch off. The watch starts at the top of the
 * child's initialization, so it uses only libcore's Os, org.json and variables read by name.
 */
public final class HostLifecycleWatch {
    private static final Pattern BASE = Pattern.compile("^(/data/app/el2/\\d+/base/[A-Za-z0-9_.]+)/");
    private static final long PERIOD_MS = 3000;
    private static final int ATTEMPTS = 20;
    private static volatile boolean started;

    private HostLifecycleWatch() {}

    /** Called once in each spawned app process, before the app starts. Never throws. */
    public static void start(String procName) {
        if (started) return;
        started = true;
        try {
            if ("0".equals(System.getenv("WESTLAKE_EXIT_WITH_HOST"))) {
                log("off (WESTLAKE_EXIT_WITH_HOST=0)");
                return;
            }
            if (servesOtherApps()) {
                log("launch serves other apps; " + procName + " keeps running if its host ends");
                return;
            }
            String window = System.getenv("WL_PARENT_ID");
            String root = System.getenv("WESTLAKE_RUNTIME_REAL_ROOT");
            Matcher base = root == null ? null : BASE.matcher(root + "/");
            if (window == null || base == null || !base.find()) {
                log("no host window; not watching " + procName);
                return;
            }
            String state = base.group(1) + "/haps/entry/files/window-state.json";
            Thread thread = new Thread(() -> run(state, window, procName), "WlHostWatch");
            thread.setDaemon(true);
            thread.start();
        } catch (Throwable error) {
            log("not watching: " + error);
        }
    }

    /** The tests the serving launches' own code uses (LocalServiceBinders, GoogleAuthProxy.provider()). */
    static boolean servesOtherApps() {
        return System.getenv("WESTLAKE_PLAY_SERVICES_SERVE") != null
                || System.getenv("WESTLAKE_PLAY_STORE_SERVE") != null
                || "microg".equals(System.getenv("WESTLAKE_GOOGLE_AUTH_PROVIDER"));
    }

    private static void run(String state, String window, String procName) {
        try {
            int pid = 0;
            for (int attempt = 0; attempt < ATTEMPTS && pid == 0; attempt++) {
                pid = owner(state, window);
                if (pid == 0 && !pause()) return;
            }
            if (pid == 0 || !alive(pid)) {
                log("no running host owns window " + window + "; not watching " + procName);
                return;
            }
            log("watching host pid " + pid + ", owner of window " + window + ", for " + procName);
            while (pause()) {
                if (alive(pid)) continue;
                log("host pid " + pid + " ended; ending " + procName + ", whose windows belonged to it"
                        + " (as Android's low-memory killer ends an app)");
                Os.kill(Os.getpid(), OsConstants.SIGKILL);
            }
        } catch (Throwable error) {
            log("stopped watching: " + error);
        }
    }

    /** The pid the host recorded for its main window, if that window is this app's parent; otherwise 0. */
    static int owner(String path, String window) {
        try {
            JSONObject state = new JSONObject(read(path));
            if (!window.equals(String.valueOf(state.optLong("mainWindowId", -1)))) return 0;
            return Math.max(0, state.optInt("ownerPid", 0));
        } catch (Exception unreadable) {
            return 0;   // Not written yet, rewritten while being read, or written by a host that records no pid.
        }
    }

    /** Signal 0 signals nothing; it fails once the process is gone (ESRCH) or its pid has another owner (EPERM). */
    static boolean alive(int pid) {
        try {
            Os.kill(pid, 0);
            return true;
        } catch (ErrnoException gone) {
            return false;
        }
    }

    private static boolean pause() {
        try {
            Thread.sleep(PERIOD_MS);
            return true;
        } catch (InterruptedException interrupted) {
            return false;
        }
    }

    private static String read(String path) throws Exception {
        FileDescriptor fd = Os.open(path, OsConstants.O_RDONLY | OsConstants.O_CLOEXEC, 0);
        try {
            byte[] buffer = new byte[65536];
            int length = 0, count;
            while (length < buffer.length
                    && (count = Os.read(fd, buffer, length, buffer.length - length)) > 0) {
                length += count;
            }
            return new String(buffer, 0, length, StandardCharsets.UTF_8);
        } finally {
            Os.close(fd);
        }
    }

    private static void log(String text) {
        System.err.println("[WL-HOST] " + text);
    }
}
