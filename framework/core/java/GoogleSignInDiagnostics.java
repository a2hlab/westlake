package adapter.core;

/** Reads only callback presence bits and numeric Status fields; never account or token contents. */
final class GoogleSignInDiagnostics {
    private GoogleSignInDiagnostics() {}

    interface Reader {
        int position();
        int size();
        int readInt();
        void seek(int position);
    }

    static boolean supported(String descriptor, int code) {
        return code == 101 && "com.google.android.gms.auth.api.signin.internal.ISignInCallbacks".equals(descriptor)
                || code == 1 && "com.google.android.gms.auth.api.identity.internal.IBeginSignInCallback".equals(descriptor);
    }

    /** Reader starts immediately after the validated interface token. Caller restores its position. */
    static String describe(String descriptor, int code, Reader data) {
        if (!supported(descriptor, code)) return null;
        try {
            boolean legacy = code == 101;
            boolean account = legacy && present(data);
            if (account) data.seek(objectEnd(data));
            String status = "absent";
            if (present(data)) {
                int end = objectEnd(data);
                Integer value = null;
                while (data.position() < end) {
                    int header = integer(data);
                    int length = length(data, header);
                    int next = end(data, length, end);
                    if ((header & 65535) == 1) {
                        if (length != 4 || value != null) throw new IllegalArgumentException();
                        value = integer(data);
                    }
                    data.seek(next);
                }
                if (value == null) throw new IllegalArgumentException();
                status = Integer.toString(value);
            }
            return "kind=" + (legacy ? "legacy" : "begin") + " status=" + status
                    + (legacy ? " account_present=" + account : " result_present=" + present(data));
        } catch (RuntimeException malformed) {
            return "kind=" + (code == 101 ? "legacy" : "begin") + " status=unreadable";
        }
    }

    private static boolean present(Reader data) {
        int value = integer(data);
        if (value != 0 && value != 1) throw new IllegalArgumentException();
        return value == 1;
    }

    private static int integer(Reader data) {
        if (data.position() < 0 || data.position() > data.size() - 4) throw new IllegalArgumentException();
        return data.readInt();
    }

    private static int length(Reader data, int header) {
        return (header >>> 16) == 65535 ? integer(data) : header >>> 16;
    }

    private static int end(Reader data, int length, int limit) {
        long end = (long) data.position() + length;
        if (length < 0 || end > limit || limit > data.size()) throw new IllegalArgumentException();
        return (int) end;
    }

    private static int objectEnd(Reader data) {
        int header = integer(data);
        if ((header & 65535) != 20293) throw new IllegalArgumentException();
        return end(data, length(data, header), data.size());
    }
}
