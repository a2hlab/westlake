package adapter.activity;

import android.net.Credentials;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.system.ErrnoException;
import android.system.Os;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.regex.Pattern;

/**
 * Which package a connected process runs, as the appspawn-x that started it says (ADR-0003 decision 3).
 *
 * A client introduces itself to a Play services or Play Store host with its package name, and until now
 * nothing checked that name: every Westlake process runs under one OH uid, so the kernel cannot tell two
 * apps apart. The spawner can. Each launch has its own appspawn-x, which knows the package it launches
 * (ASX_LAUNCH_PKG) and answers WL_MSG_WHOIS for its own children only, checked by their parent pid. A
 * client names its spawner's socket; only root can create a socket in /dev/unix/socket, and the answer
 * counts only from a root peer, so a client cannot stand up a spawner of its own, and naming another
 * app's spawner gets "not mine".
 *
 * A spawner that vouches for a process also hands over its launch's APK files, read-only. A host can then
 * check the APK files and the certificates a client presents against the ones that were launched, instead
 * of taking them from the client.
 */
final class SpawnerIdentity {
    private static final Pattern SPAWNER_SOCKET = Pattern.compile("/dev/unix/socket/A2HSource[0-9A-Za-z]+");
    private static final int MAGIC = 0xEF201234;
    private static final int WHOIS = 0x574C5749;   // spawn_msg.h WL_MSG_WHOIS ("WLWI")
    private static final int HEADER = 20 + 256;
    private static final int NAME_OFFSET = 20;
    private static final int RESULT_OFFSET = HEADER;
    private static final int RESPONSE_MIN = HEADER + 8;
    private static final int TIMEOUT_MS = 2_000;

    /** A spawner's answer for one of its processes. */
    static final class Vouched {
        /** The package the spawner launches. */
        final String packageName;
        /** The launch's APK files, read-only: the base APK first, then its splits. Possibly none. */
        final FileDescriptor[] apks;

        Vouched(String packageName, FileDescriptor[] apks) {
            this.packageName = packageName;
            this.apks = apks;
        }

        void close() {
            closeAll(apks);
        }
    }

    private SpawnerIdentity() {}

    /** What the spawner at socketPath says about pid, or null if it does not vouch for pid. */
    static Vouched ask(String socketPath, int pid) {
        if (socketPath == null || !SPAWNER_SOCKET.matcher(socketPath).matches() || pid <= 0) return null;
        LocalSocket socket = new LocalSocket();
        FileDescriptor[] received = null;
        try {
            socket.connect(new LocalSocketAddress(socketPath, LocalSocketAddress.Namespace.FILESYSTEM));
            socket.setSoTimeout(TIMEOUT_MS);
            Credentials peer = socket.getPeerCredentials();
            if (peer == null || peer.getUid() != 0) return null;   // every spawner runs as root
            byte[] request = new byte[HEADER];
            ByteBuffer out = ByteBuffer.wrap(request).order(ByteOrder.LITTLE_ENDIAN);
            out.putInt(0, MAGIC).putInt(4, WHOIS).putInt(8, HEADER).putInt(12, pid).putInt(16, 0);
            byte[] digits = Integer.toString(pid).getBytes(StandardCharsets.US_ASCII);
            System.arraycopy(digits, 0, request, NAME_OFFSET, digits.length);
            OutputStream output = socket.getOutputStream();
            output.write(request);
            output.flush();
            InputStream input = socket.getInputStream();
            byte[] response = new byte[512];
            int got = 0;
            while (got < RESPONSE_MIN) {
                int n = input.read(response, got, response.length - got);
                if (n <= 0) break;
                got += n;
            }
            received = socket.getAncillaryFileDescriptors();
            if (got < RESPONSE_MIN) return null;
            ByteBuffer in = ByteBuffer.wrap(response).order(ByteOrder.LITTLE_ENDIAN);
            if (in.getInt(0) != MAGIC || in.getInt(4) != WHOIS || in.getInt(RESULT_OFFSET) != 0
                    || in.getInt(RESULT_OFFSET + 4) != pid) {
                return null;
            }
            int end = NAME_OFFSET;
            while (end < NAME_OFFSET + 256 && response[end] != 0) end++;
            String name = new String(response, NAME_OFFSET, end - NAME_OFFSET, StandardCharsets.UTF_8);
            if (name.isEmpty()) return null;
            Vouched vouched = new Vouched(name, received != null ? received : new FileDescriptor[0]);
            received = null;   // the answer owns them now
            return vouched;
        } catch (IOException | RuntimeException | LinkageError e) {
            return null;   // never let the question break an introduction: unverified, logged by the caller
        } finally {
            closeAll(received);
            try {
                socket.close();
            } catch (IOException ignored) {
            }
        }
    }

    static void closeAll(FileDescriptor[] fds) {
        if (fds == null) return;
        for (FileDescriptor fd : fds) {
            if (fd == null || !fd.valid()) continue;
            try {
                Os.close(fd);
            } catch (ErrnoException ignored) {
            }
        }
    }
}
