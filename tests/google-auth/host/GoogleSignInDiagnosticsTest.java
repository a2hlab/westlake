package adapter.core;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public final class GoogleSignInDiagnosticsTest {
    private static final String LEGACY = "com.google.android.gms.auth.api.signin.internal.ISignInCallbacks";
    private static final String BEGIN = "com.google.android.gms.auth.api.identity.internal.IBeginSignInCallback";
    private static int checks;

    private static String decode(String descriptor, int code, int... words) {
        ByteBuffer data = ByteBuffer.allocate(words.length * 4).order(ByteOrder.LITTLE_ENDIAN);
        for (int word : words) data.putInt(word);
        data.flip();
        return GoogleSignInDiagnostics.describe(descriptor, code, new GoogleSignInDiagnostics.Reader() {
            public int position() { return data.position(); }
            public int size() { return data.limit(); }
            public int readInt() { return data.getInt(); }
            public void seek(int at) { data.position(at); }
        });
    }

    private static void eq(String expected, String actual) {
        checks++;
        if (!java.util.Objects.equals(expected, actual)) throw new AssertionError("check " + checks);
    }

    public static void main(String[] args) {
        eq("kind=legacy status=4 account_present=false", decode(LEGACY, 101, 0, 1, 0xffff4f45, 8, 0x40001, 4));
        eq("kind=legacy status=10 account_present=false", decode(LEGACY, 101, 0, 1, 0xffff4f45, 8, 0x40001, 10));
        // An account object and a Status message are skipped without examining their private payload.
        eq("kind=legacy status=0 account_present=true", decode(LEGACY, 101,
                1, 0xffff4f45, 8, 0x73656372, 0x65747321,
                1, 0xffff4f45, 20, 0x80002, 0x73656372, 0x65747321, 0x40001, 0));
        eq("kind=begin status=16 result_present=false", decode(BEGIN, 1, 1, 0xffff4f45, 8, 0x40001, 16, 0));
        eq("kind=begin status=0 result_present=true", decode(BEGIN, 1, 1, 0xffff4f45, 8, 0x40001, 0, 1));
        eq("kind=legacy status=absent account_present=false", decode(LEGACY, 101, 0, 0));
        eq("kind=legacy status=unreadable", decode(LEGACY, 101, 2));
        eq("kind=legacy status=unreadable", decode(LEGACY, 101, 0, 1, 0xffff4f45, -1));
        eq("kind=legacy status=unreadable", decode(LEGACY, 101, 0, 1, 0xffff4f45, Integer.MAX_VALUE));
        eq("kind=legacy status=unreadable", decode(LEGACY, 101, 0, 1, 0xffff4f45, 16, 0x40001, 0, 0x40001, 4));
        eq("kind=legacy status=unreadable", decode(LEGACY, 101, 0, 1, 0xffff4f45, 8, 0x40002, 0));
        eq("kind=legacy status=unreadable", decode(LEGACY, 101, 0, 1, 0xffff4f45, 4, 0x40001));
        eq(null, decode(LEGACY, 102, 0, 0));
        eq(null, decode("unrelated", 101, 0, 0));
        System.out.println("GOOGLE_SIGNIN_DIAGNOSTICS_PASS " + checks);
    }
}
