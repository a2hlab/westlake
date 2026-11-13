package adapter.activity;

import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructTimeval;
import android.system.UnixSocketAddress;

import java.io.FileDescriptor;
import java.io.IOException;
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
 * client names its spawner's socket; only root can create a socket in /dev/unix/socket, so a client
 * cannot stand up a spawner of its own, and naming another app's spawner gets "not mine".
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

    private SpawnerIdentity() {}

    /** The package the spawner at socketPath says pid runs, or null if it does not vouch for pid. */
    static String packageOf(String socketPath, int pid) {
        if (socketPath == null || !SPAWNER_SOCKET.matcher(socketPath).matches() || pid <= 0) return null;
        FileDescriptor fd = null;
        try {
            fd = Os.socket(OsConstants.AF_UNIX, OsConstants.SOCK_STREAM, 0);
            StructTimeval timeout = StructTimeval.fromMillis(TIMEOUT_MS);
            Os.setsockoptTimeval(fd, OsConstants.SOL_SOCKET, OsConstants.SO_RCVTIMEO, timeout);
            Os.setsockoptTimeval(fd, OsConstants.SOL_SOCKET, OsConstants.SO_SNDTIMEO, timeout);
            Os.connect(fd, UnixSocketAddress.createFileSystem(socketPath));
            byte[] request = new byte[HEADER];
            ByteBuffer out = ByteBuffer.wrap(request).order(ByteOrder.LITTLE_ENDIAN);
            out.putInt(0, MAGIC).putInt(4, WHOIS).putInt(8, HEADER).putInt(12, pid).putInt(16, 0);
            byte[] digits = Integer.toString(pid).getBytes(StandardCharsets.US_ASCII);
            System.arraycopy(digits, 0, request, NAME_OFFSET, digits.length);
            for (int sent = 0; sent < request.length; ) {
                sent += Os.write(fd, request, sent, request.length - sent);
            }
            byte[] response = new byte[512];
            int got = 0;
            while (got < RESPONSE_MIN) {
                int n = Os.read(fd, response, got, response.length - got);
                if (n <= 0) break;
                got += n;
            }
            if (got < RESPONSE_MIN) return null;
            ByteBuffer in = ByteBuffer.wrap(response).order(ByteOrder.LITTLE_ENDIAN);
            if (in.getInt(0) != MAGIC || in.getInt(4) != WHOIS || in.getInt(RESULT_OFFSET) != 0
                    || in.getInt(RESULT_OFFSET + 4) != pid) {
                return null;
            }
            int end = NAME_OFFSET;
            while (end < NAME_OFFSET + 256 && response[end] != 0) end++;
            String name = new String(response, NAME_OFFSET, end - NAME_OFFSET, StandardCharsets.UTF_8);
            return name.isEmpty() ? null : name;
        } catch (ErrnoException | IOException | RuntimeException | LinkageError e) {
            return null;   // never let the question break an introduction: unverified, logged by the caller
        } finally {
            if (fd != null) {
                try {
                    Os.close(fd);
                } catch (ErrnoException ignored) {
                }
            }
        }
    }
}
