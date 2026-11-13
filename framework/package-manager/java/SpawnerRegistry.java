package adapter.packagemanager;

import android.net.Credentials;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.regex.Pattern;

/**
 * App ids from the registry the spawners keep (ADR-0003 decision 1).
 *
 * Every Westlake process gives a package the same app id. The ids were kept in a table in the host app's
 * files, which every Westlake app can write, so an app could have given itself Play services' id. Each
 * launch's appspawn-x now keeps the table where only root can enter and answers WL_MSG_APPID for a package
 * name; a process asks its own spawner, the one WESTLAKE_SPAWN_SOCKET names, and an answer counts only from
 * a root peer.
 */
final class SpawnerRegistry {
    private static final Pattern SPAWNER_SOCKET = Pattern.compile("/dev/unix/socket/A2HSource[0-9A-Za-z]+");
    private static final int MAGIC = 0xEF201234;
    private static final int APPID = 0x574C4149;   // spawn_msg.h WL_MSG_APPID ("WLAI")
    private static final int HEADER = 20 + 256;
    private static final int NAME_OFFSET = 20;
    private static final int RESULT_OFFSET = HEADER;
    private static final int RESPONSE_MIN = HEADER + 8;
    private static final int TIMEOUT_MS = 2_000;

    private SpawnerRegistry() {}

    /** The package's app id from this process's spawner, or -1 when no spawner answers with one. */
    static int appId(String packageName) {
        String socketPath = System.getenv("WESTLAKE_SPAWN_SOCKET");
        if (packageName == null || socketPath == null || !SPAWNER_SOCKET.matcher(socketPath).matches()) return -1;
        byte[] name = packageName.getBytes(StandardCharsets.UTF_8);
        if (name.length == 0 || name.length >= 256) return -1;
        LocalSocket socket = new LocalSocket();
        try {
            socket.connect(new LocalSocketAddress(socketPath, LocalSocketAddress.Namespace.FILESYSTEM));
            socket.setSoTimeout(TIMEOUT_MS);
            Credentials peer = socket.getPeerCredentials();
            if (peer == null || peer.getUid() != 0) return -1;   // every spawner runs as root
            byte[] request = new byte[HEADER];
            ByteBuffer.wrap(request).order(ByteOrder.LITTLE_ENDIAN)
                    .putInt(0, MAGIC).putInt(4, APPID).putInt(8, HEADER).putInt(12, 0).putInt(16, 0);
            System.arraycopy(name, 0, request, NAME_OFFSET, name.length);
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
            if (got < RESPONSE_MIN) return -1;
            ByteBuffer in = ByteBuffer.wrap(response).order(ByteOrder.LITTLE_ENDIAN);
            if (in.getInt(0) != MAGIC || in.getInt(4) != APPID || in.getInt(RESULT_OFFSET) != 0) return -1;
            int id = in.getInt(RESULT_OFFSET + 4);
            return id > 0 ? id : -1;
        } catch (IOException | RuntimeException | LinkageError e) {
            return -1;
        } finally {
            try {
                socket.close();
            } catch (IOException ignored) {
            }
        }
    }
}
