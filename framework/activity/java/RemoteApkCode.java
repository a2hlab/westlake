package adapter.activity;

import android.os.ParcelFileDescriptor;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Map;

/** Read-only public APK code vouched for by the launcher's pinned root spawner. */
final class RemoteApkCode implements AutoCloseable {
    private static final class Entry {
        final ParcelFileDescriptor file;
        final long device, inode, size;
        Entry(ParcelFileDescriptor file, StructStat stat) {
            this.file = file;
            device = stat.st_dev;
            inode = stat.st_ino;
            size = stat.st_size;
        }
        boolean sameFile(Entry other) {
            return other != null && device == other.device && inode == other.inode && size == other.size;
        }
    }

    private final Map<String, Entry> files = new LinkedHashMap<>();

    private RemoteApkCode() {}

    static RemoteApkCode open(String spawner, String expectedBaseHash, int peerPid) throws IOException {
        if (expectedBaseHash == null || !expectedBaseHash.matches("[0-9a-f]{64}"))
            throw new SecurityException("remote APK pin missing");
        SpawnerIdentity.Vouched proof = SpawnerIdentity.ask(spawner, peerPid);
        if (proof == null) throw new SecurityException("remote APK host not vouched for");
        RemoteApkCode result = new RemoteApkCode();
        boolean accepted = false;
        try {
            if (!"com.google.android.gms".equals(proof.packageName)
                    || proof.apks.length == 0 || proof.apks.length > 64)
                throw new SecurityException("remote APK host package differs");
            long total = 0;
            for (java.io.FileDescriptor fd : proof.apks) {
                StructStat stat = Os.fstat(fd);
                if (!OsConstants.S_ISREG(stat.st_mode) || stat.st_size <= 0
                        || stat.st_size > (512L << 20)
                        || (Os.fcntlInt(fd, OsConstants.F_GETFL, 0) & OsConstants.O_ACCMODE) != OsConstants.O_RDONLY)
                    throw new SecurityException("remote APK descriptor invalid");
                total += stat.st_size;
                if (total > (1L << 30)) throw new SecurityException("remote APK set too large");
                ParcelFileDescriptor copy = ParcelFileDescriptor.dup(fd);
                boolean kept = false;
                try {
                    String name = new File(Os.readlink("/proc/self/fd/" + copy.getFd())).getName();
                    if (!name.matches("[A-Za-z0-9_.-]+\\.apk") || result.files.containsKey(name))
                        throw new SecurityException("remote APK filename invalid");
                    result.files.put(name, new Entry(copy, stat));
                    kept = true;
                } finally { if (!kept) copy.close(); }
            }
            // Positional reads leave the spawner's shared open-file offset intact.
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            long baseSize = result.files.values().iterator().next().size;
            try (ParcelFileDescriptor.AutoCloseInputStream input = new ParcelFileDescriptor.AutoCloseInputStream(
                    ParcelFileDescriptor.dup(proof.apks[0]))) {
                FileChannel channel = input.getChannel();
                ByteBuffer buffer = ByteBuffer.allocate(65536);
                long position = 0;
                for (;;) {
                    int count = channel.read(buffer, position);
                    if (count < 0) break;
                    if (count == 0) throw new IOException("remote APK read stalled");
                    position += count;
                    if (position > baseSize) throw new SecurityException("remote APK grew during verification");
                    buffer.flip();
                    digest.update(buffer);
                    buffer.clear();
                }
                if (position != baseSize) throw new SecurityException("remote APK size changed during verification");
            }
            StringBuilder actual = new StringBuilder(64);
            final char[] hex = "0123456789abcdef".toCharArray();
            for (byte value : digest.digest()) actual.append(hex[(value & 255) >>> 4]).append(hex[value & 15]);
            if (!expectedBaseHash.contentEquals(actual)) throw new SecurityException("remote APK pin differs");
            accepted = true;
            return result;
        } catch (android.system.ErrnoException | java.security.NoSuchAlgorithmException error) {
            throw new IOException("remote APK descriptors unavailable", error);
        } finally {
            proof.close();
            if (!accepted) result.close();
        }
    }

    boolean sameFiles(RemoteApkCode other) {
        if (!files.keySet().equals(other.files.keySet())) return false;
        for (Map.Entry<String, Entry> file : files.entrySet())
            if (!file.getValue().sameFile(other.files.get(file.getKey()))) return false;
        return true;
    }

    int size() { return files.size(); }

    String path(String original, String logicalRoot, String realRoot) {
        if (original == null) return null;
        for (Entry entry : files.values())
            if (original.equals("/proc/self/fd/" + entry.file.getFd())) return original;
        String name = relative(original, logicalRoot);
        if (name == null) name = relative(original, realRoot);
        Entry entry = name == null ? null : files.get(name);
        if (entry == null) throw new SecurityException("remote APK path outside vouched code set");
        return "/proc/self/fd/" + entry.file.getFd();
    }

    private static String relative(String path, String root) {
        if (root == null || !path.startsWith(root + "/")) return null;
        String name = path.substring(root.length() + 1);
        return name.indexOf('/') < 0 ? name : null;
    }

    @Override public void close() {
        for (Entry entry : files.values()) {
            try { entry.file.close(); } catch (IOException ignored) {}
        }
        files.clear();
    }
}
