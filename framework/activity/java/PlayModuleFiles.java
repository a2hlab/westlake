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
 * logs it ([WL-MODULE]). WESTLAKE_PLAY_MODULE_FILES picks what the client does with such a file:
 * - unset: nothing; the paths pass unchanged (observe only).
 * - descriptor: the client asks the host for the file and reads /proc/self/fd/<n> instead, a read-only descriptor
 *   kept open for the process's life, since class loaders and mappings keep using it.
 * - copy: the client makes a private copy at the same path in its own view of /data/data, from the host's
 *   read-only descriptor, checked by SHA-256 and made read-only (Android refuses writable dex) before it is renamed
 *   into place; the path then works for every loader (dex, resources, native libraries) unchanged. ADR-0005
 *   decision 3's integrity-checked copy in the receiving app's private storage: a client can change only its own
 *   copy. Play services' module code running in the client also finds modules through its config, not only
 *   through cursors, so the first time the client reaches Play services' module provider it copies every module
 *   file the host lists ({@link #list}, MODULE_LIST).
 */
final class PlayModuleFiles {
    private PlayModuleFiles() {}

    private static final String TAG = "[WL-MODULE] ";
    private static final String MODE = System.getenv("WESTLAKE_PLAY_MODULE_FILES");
    static final boolean DESCRIPTORS = "descriptor".equals(MODE);
    static final boolean COPIES = "copy".equals(MODE);
    /** Module files a host lists at most, and their total size: copies are per client. */
    static final int MAX_LISTED = 32;
    static final long MAX_LISTED_BYTES = 64L << 20;

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

    private static final Set<String> sCalls = new HashSet<>();

    /** Play services' module provider, whose answers make the module inventory ADR-0005 asks for. */
    static boolean modules(String authority) {
        return authority != null && authority.endsWith(".chimera");
    }

    /** A client's call to a host's module provider: the method and the URI's path, once each (inventory). */
    static void observeCall(String authority, String method, Object[] args) {
        if (!modules(authority)) return;
        String uri = null;
        if (args != null) {
            for (Object arg : args) {
                if (arg instanceof android.net.Uri) {
                    uri = ((android.net.Uri) arg).getPath();
                    break;
                }
            }
        }
        synchronized (sCalls) {
            if (!sCalls.add(method + " " + uri)) return;
        }
        System.err.println(TAG + authority + " " + method + " " + uri);
    }

    /** A cursor from a host's provider, whose strings naming the host's files are logged and, if switched on, served. */
    static Cursor view(Cursor cursor, String hostPackage, String authority) {
        if (cursor == null || hostPackage == null) return cursor;
        if (modules(authority)) {
            synchronized (sCalls) {
                if (sCalls.add("columns " + java.util.Arrays.toString(cursor.getColumnNames()))) {
                    System.err.println(TAG + authority + " columns " + java.util.Arrays.toString(cursor.getColumnNames()));
                }
            }
        }
        return new CursorWrapper(cursor) {
            @Override
            public int getInt(int column) {
                int value = super.getInt(column);
                number(column, value);
                return value;
            }

            @Override
            public long getLong(int column) {
                long value = super.getLong(column);
                number(column, value);
                return value;
            }

            private void number(int column, long value) {
                if (!modules(authority)) return;
                String name;
                try {
                    name = getColumnName(column);
                } catch (RuntimeException e) {
                    name = String.valueOf(column);
                }
                synchronized (sCalls) {
                    if (!sCalls.add("value " + name + " " + value)) return;
                }
                System.err.println(TAG + authority + " " + name + " = " + value);
            }

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
        if (COPIES) {
            how = copy(hostPackage, path) ? "copied" : "not copied";
        } else if (DESCRIPTORS) {
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

    private static final Map<String, Boolean> sCopied = new HashMap<>();   // guarded by sCopied
    private static final Set<String> sListed = new HashSet<>();            // guarded by sListed

    /**
     * Copies every module file the host lists, once per host, the first time this app reaches its module provider:
     * Play services' module code in this app finds modules through its config as well as through cursors.
     */
    static void copyListed(String hostPackage) {
        if (!COPIES) return;
        synchronized (sListed) {
            if (!sListed.add(hostPackage)) return;
        }
        String[] paths = PlayServicesChannel.moduleList(hostPackage);
        int copied = 0;
        for (String path : paths) {
            if (inStorage(hostPackage, path) && copy(hostPackage, path)) copied++;
        }
        System.err.println(TAG + hostPackage + " lists " + paths.length + " module files; " + copied + " copied");
    }

    /** A private, read-only copy of a host's file at the same path in this app's view, made once; false if not. */
    static boolean copy(String hostPackage, String path) {
        if (!inStorage(hostPackage, path)) return false;
        for (String part : path.substring(1).split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) return false;   // only a plain path
        }
        synchronized (sCopied) {
            Boolean done = sCopied.get(path);
            if (done == null) {
                done = makeCopy(hostPackage, path);
                sCopied.put(path, done);
            }
            return done;
        }
    }

    private static boolean makeCopy(String hostPackage, String path) {
        ParcelFileDescriptor file = PlayServicesChannel.moduleFile(hostPackage, path);
        if (file == null) return false;
        java.io.File target = new java.io.File(path);
        java.io.File temp = new java.io.File(target.getParentFile(), "." + target.getName() + ".wl-" + android.os.Process.myPid());
        try (java.io.InputStream in = new ParcelFileDescriptor.AutoCloseInputStream(file)) {
            java.io.File dir = target.getParentFile();
            if (!dir.isDirectory() && !dir.mkdirs()) throw new java.io.IOException("cannot create " + relative(hostPackage, dir.getPath()));
            java.security.MessageDigest streamed = java.security.MessageDigest.getInstance("SHA-256");
            long bytes = 0;
            try (java.io.FileOutputStream out = new java.io.FileOutputStream(temp)) {
                byte[] buffer = new byte[1 << 16];
                for (int n; (n = in.read(buffer)) > 0; bytes += n) {
                    streamed.update(buffer, 0, n);
                    out.write(buffer, 0, n);
                }
                out.getFD().sync();
            }
            byte[] wanted = streamed.digest();
            Os.chmod(temp.getPath(), 0444);
            if (!java.util.Arrays.equals(wanted, digest(temp))) throw new java.io.IOException("the copy differs from what was read");
            if (target.exists()) {
                if (java.util.Arrays.equals(wanted, digest(target))) {
                    temp.delete();
                    return true;
                }
                Os.chmod(target.getPath(), 0644);
            }
            Os.rename(temp.getPath(), target.getPath());
            System.err.println(TAG + "copied " + hostPackage + "'s " + relative(hostPackage, path) + " (" + bytes
                    + " bytes, sha256 " + hex(wanted).substring(0, 12) + "...) read-only");
            return true;
        } catch (Exception e) {
            temp.delete();
            System.err.println(TAG + "no copy of " + hostPackage + "'s " + relative(hostPackage, path) + ": " + e);
            return false;
        }
    }

    private static byte[] digest(java.io.File file) throws Exception {
        java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
        try (java.io.InputStream in = new java.io.FileInputStream(file)) {
            byte[] buffer = new byte[1 << 16];
            for (int n; (n = in.read(buffer)) > 0; ) md.update(buffer, 0, n);
        }
        return md.digest();
    }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder();
        for (byte b : bytes) out.append(String.format("%02x", b & 0xff));
        return out.toString();
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
     * The module files this host shares, as it names them: the regular, world-readable files under its Chimera
     * module directories (app_chimera/m, in credential and device-protected storage), at most MAX_LISTED files and
     * MAX_LISTED_BYTES bytes, smallest first.
     */
    static String[] list(String dataDir) {
        if (dataDir == null) return new String[0];
        java.util.List<java.io.File> files = new java.util.ArrayList<>();
        for (String root : new String[] {dataDir + "/app_chimera/m", dataDir + "/.device_protected/app_chimera/m"}) {
            java.io.File[] modules = new java.io.File(root).listFiles();
            if (modules == null) continue;
            for (java.io.File module : modules) {
                java.io.File[] entries = module.listFiles();
                if (entries == null) continue;
                for (java.io.File f : entries) {
                    try {
                        StructStat stat = Os.lstat(f.getPath());
                        if (OsConstants.S_ISREG(stat.st_mode) && (stat.st_mode & OsConstants.S_IROTH) != 0
                                && closedDirectory(dataDir, f.getPath()) == null) {
                            files.add(f);
                        }
                    } catch (ErrnoException ignored) {
                        // Gone while listing.
                    }
                }
            }
        }
        files.sort(java.util.Comparator.comparingLong(java.io.File::length));
        java.util.List<String> out = new java.util.ArrayList<>();
        long total = 0;
        for (java.io.File f : files) {
            if (out.size() == MAX_LISTED || total + f.length() > MAX_LISTED_BYTES) break;
            total += f.length();
            out.add(f.getPath());
        }
        return out.toArray(new String[0]);
    }

    /**
     * Opens a file of this process's own storage for a client, read-only, or answers null with the reason in
     * refusal[0]. dataDir is this application's data directory as this process names it.
     */
    /**
     * The first directory between the storage root and path that other applications cannot search, relative to
     * dataDir, or null. With the file's own world-readable bit this is GrapheneOS's FileProxyService rule: what
     * Play services shares, it makes reachable, as it does its module directories. The roots are the data
     * directory and its device-protected storage (.device_protected), as GrapheneOS's are the CE and DE
     * directories; the roots themselves are not checked.
     */
    static String closedDirectory(String dataDir, String path) throws ErrnoException {
        String deviceProtected = dataDir + "/.device_protected/";
        String root = path.startsWith(deviceProtected) ? deviceProtected : dataDir + "/";
        for (int i = path.indexOf('/', root.length()); i > 0; i = path.indexOf('/', i + 1)) {
            StructStat stat = Os.stat(path.substring(0, i));
            if (!OsConstants.S_ISDIR(stat.st_mode) || (stat.st_mode & OsConstants.S_IXOTH) == 0) {
                return path.substring(dataDir.length() + 1, i);
            }
        }
        return null;
    }

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
            String closed = closedDirectory(dataDir, path);
            if (closed != null) {
                refusal[0] = "directory " + closed + " is not world-searchable (the host did not share it)";
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
