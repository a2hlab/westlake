package adapter.activity;

import android.database.Cursor;
import android.database.CursorWrapper;
import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;

import java.io.FileDescriptor;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * Module files a Play host shares with its clients (westlake ADR-0005), as GrapheneOS's FileProxyService does.
 *
 * A client learns where a module is from the host's provider: Play services' chimera provider answers paths in
 * its own storage, such as .../.device_protected/app_chimera/m/00000000/AdsDynamite.uncompressed.apk, and the
 * module loader running in the client opens them. A client cannot see the host's storage at those paths (its
 * /data/data is its own), so the class loader found nothing ("ClassLoader referenced unknown path").
 *
 * Host side, {@link #open}: a file is served only if, opened read-only without following a final symlink, it is
 * a regular file that Play services made world-readable (its own statement that clients may load it, the
 * GrapheneOS rule), and the kernel's name for the open file is the requested path inside the host's data
 * directory, so no directory on the way was a symlink out of it. Validation is of the opened object.
 *
 * Client side, {@link #view}: a cursor from a host's provider whose string names a file in the host's storage
 * logs it ([WL-MODULE]). With WESTLAKE_PLAY_MODULE_FILES=descriptor the client asks the host for that file and
 * reads /proc/self/fd/<n> instead: a read-only descriptor it keeps open for the process's life, since class
 * loaders and mappings keep using it. Without the switch the paths pass unchanged (observe only).
 */
final class PlayModuleFiles {
    private PlayModuleFiles() {}

    private static final String TAG = "[WL-MODULE] ";
    static final boolean DESCRIPTORS = "descriptor".equals(System.getenv("WESTLAKE_PLAY_MODULE_FILES"));

    // ---- Client side ----

    private static final Set<String> sLogged = new HashSet<>();
    private static final Map<String, ParcelFileDescriptor> sOpened = new HashMap<>();   // guarded by sOpened

    /** The prefixes naming the host package's storage in Android's layout. */
    private static String[] storage(String hostPackage) {
        return new String[] {"/data/data/" + hostPackage + "/", "/data/user/0/" + hostPackage + "/",
                "/data/user_de/0/" + hostPackage + "/"};
    }

    static boolean inStorage(String hostPackage, String value) {
        if (value == null || hostPackage == null) return false;
        for (String prefix : storage(hostPackage)) {
            if (value.startsWith(prefix)) return true;
        }
        return false;
    }

    /** A cursor from a host's provider, whose strings naming the host's files are logged and, if switched on, served. */
    static Cursor view(Cursor cursor, String hostPackage, String authority) {
        if (cursor == null || hostPackage == null) return cursor;
        return new CursorWrapper(cursor) {
            @Override
            public String getString(int column) {
                String value = super.getString(column);
                if (!inStorage(hostPackage, value)) return value;
                String name;
                try {
                    name = getColumnName(column);
                } catch (RuntimeException e) {
                    name = String.valueOf(column);
                }
                return named(hostPackage, authority + " " + name, value);
            }
        };
    }

    /** What the client reads for a host file a provider named: a descriptor path, or the path as given. */
    private static String named(String hostPackage, String where, String path) {
        String answer = path;
        String how = "observed";
        if (DESCRIPTORS) {
            ParcelFileDescriptor file;
            synchronized (sOpened) {
                file = sOpened.get(path);
                if (file == null) {
                    file = PlayServicesChannel.moduleFile(hostPackage, path);
                    if (file != null) sOpened.put(path, file);
                }
            }
            if (file != null) {
                answer = "/proc/self/fd/" + file.getFd();
                how = "descriptor " + file.getFd();
            } else {
                how = "not served";
            }
        }
        synchronized (sLogged) {
            if (sLogged.add(where + " " + path)) {
                System.err.println(TAG + where + " names " + hostPackage + "'s " + relative(hostPackage, path) + ": " + how);
            }
        }
        return answer;
    }

    static String relative(String hostPackage, String path) {
        if (path == null) return "null";
        for (String prefix : storage(hostPackage)) {
            if (path.startsWith(prefix)) return path.substring(prefix.length());
        }
        return path;
    }

    // ---- Host side ----

    /**
     * Opens a file of this process's own storage for a client, read-only, or answers null with the reason in
     * refusal[0]. dataDir is this application's data directory as this process names it.
     */
    static ParcelFileDescriptor open(String dataDir, String path, String[] refusal) {
        if (path == null || dataDir == null || !path.startsWith(dataDir + "/")) {
            refusal[0] = "not in this application's storage";
            return null;
        }
        for (String part : path.substring(dataDir.length() + 1).split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) {
                refusal[0] = "not a plain path";
                return null;
            }
        }
        FileDescriptor fd;
        try {
            fd = Os.open(path, OsConstants.O_RDONLY | OsConstants.O_CLOEXEC | OsConstants.O_NOFOLLOW, 0);
        } catch (ErrnoException e) {
            refusal[0] = "open: " + OsConstants.errnoName(e.errno);
            return null;
        }
        try {
            StructStat stat = Os.fstat(fd);
            if (!OsConstants.S_ISREG(stat.st_mode)) {
                refusal[0] = "not a regular file";
                return null;
            }
            if ((stat.st_mode & OsConstants.S_IROTH) == 0) {
                refusal[0] = "not world-readable (the host did not share it)";
                return null;
            }
            String opened = Os.readlink("/proc/self/fd/" + fd.getInt$());
            if (!path.equals(opened)) {
                refusal[0] = "opened another file (a symlink on the way)";
                return null;
            }
            return ParcelFileDescriptor.dup(fd);
        } catch (ErrnoException | java.io.IOException e) {
            refusal[0] = "check: " + e;
            return null;
        } finally {
            try {
                Os.close(fd);
            } catch (ErrnoException ignored) {
                // Closing a descriptor it opened.
            }
        }
    }
}
