package adapter.packagemanager;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

/**
 * The app id that every Westlake process gives a package, kept as PackageManagerService keeps the app ids it
 * gave at install (packages.xml): one text file with a line "<package> <app id>" for each package, in the
 * order they were first seen. A process adds a package while it holds an exclusive lock on the file, so
 * two processes that meet a new app at the same moment give it one id. An id, once given, never changes.
 *
 * That file is in the host app's files, which every Westlake app can write. The registry the spawners keep
 * where only root can enter (SpawnerRegistry, ADR-0003 decision 1) answers first; it started from this file,
 * so the ids are the same. The file serves only a process whose spawner predates the registry.
 */
final class PackageUidTable {
    private final File mFile;
    private final int mFirst;
    /** What this process has read from the registry or the file so far. */
    private final Map<String, Integer> mIds = new HashMap<>();
    private boolean mOldTableLogged;

    PackageUidTable(File file, int firstAppId) {
        mFile = file;
        mFirst = firstAppId;
    }

    /**
     * The package's app id: the registry's, or else the one the file holds, or the next free one, added to the
     * file now.
     */
    synchronized int appId(String packageName) throws IOException {
        Integer known = mIds.get(packageName);
        if (known != null) return known;
        int registered = SpawnerRegistry.appId(packageName);
        if (registered >= mFirst) {
            mIds.put(packageName, registered);
            return registered;
        }
        if (!mOldTableLogged) {
            mOldTableLogged = true;
            System.err.println("[WL-PLAY] no spawner registry answered for " + packageName
                    + "; app ids come from the host's table");
        }
        File dir = mFile.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory()) {
            throw new IOException("cannot create " + dir);
        }
        try (RandomAccessFile file = new RandomAccessFile(mFile, "rw");
                FileLock lock = file.getChannel().lock()) {
            byte[] text = new byte[(int) file.length()];
            file.readFully(text);
            read(new String(text, StandardCharsets.UTF_8));
            known = mIds.get(packageName);
            if (known != null) return known;
            int next = mFirst;
            for (int id : mIds.values()) next = Math.max(next, id + 1);
            // A line that a killed process left without its end is ended first, so that this one stays whole.
            boolean ended = text.length == 0 || text[text.length - 1] == '\n';
            String line = (ended ? "" : "\n") + packageName + " " + next + "\n";
            file.seek(text.length);
            file.write(line.getBytes(StandardCharsets.UTF_8));
            mIds.put(packageName, next);
            return next;
        }
    }

    /**
     * Takes in every line it can read. An id cut short by a killed writer has fewer digits than any id
     * given out, so it is below the first one and is passed over: that package gets a new id when it next
     * comes.
     */
    private void read(String text) {
        for (String line : text.split("\n")) {
            String[] fields = line.trim().split(" ");
            if (fields.length != 2 || fields[0].isEmpty()) continue;
            try {
                int id = Integer.parseInt(fields[1]);
                if (id >= mFirst) mIds.putIfAbsent(fields[0], id);
            } catch (NumberFormatException e) {
                // not a line this class wrote
            }
        }
    }
}
