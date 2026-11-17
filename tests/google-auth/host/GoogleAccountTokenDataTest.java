package adapter.core;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** TokenData field 2 is the token, field 3 the optional expiry in seconds in the pinned SDK. */
public final class GoogleAccountTokenDataTest {
    private static int passed;
    private static byte[] string(String value) {
        byte[] text = value.getBytes(StandardCharsets.UTF_16LE);
        ByteBuffer b = ByteBuffer.allocate((4 + text.length + 2 + 3) & ~3).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(value.length()).put(text).putShort((short) 0);
        return b.array();
    }
    private static byte[] field(int id, byte[] value) {
        ByteBuffer b = ByteBuffer.allocate(8 + value.length).order(ByteOrder.LITTLE_ENDIAN);
        return b.putInt(0xffff0000 | id).putInt(value.length).put(value).array();
    }
    private static byte[] object(byte[]... fields) {
        int length = Arrays.stream(fields).mapToInt(f -> f.length).sum();
        ByteBuffer b = ByteBuffer.allocate(8 + length).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(0xffff0000 | 20293).putInt(length);
        for (byte[] f : fields) b.put(f);
        return b.array();
    }
    private static byte[] expiry(long seconds) {
        return field(3, ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).putLong(seconds).array());
    }
    private static void deny(byte[] data) {
        try { GoogleAccountTokenData.parse(data); throw new AssertionError("malformed token accepted"); }
        catch (SecurityException expected) { passed++; }
    }
    public static void main(String[] args) {
        String token = "ya29.fixture-not-a-real-token";
        byte[] t = field(2, string(token));
        GoogleAccountTokenData parsed = GoogleAccountTokenData.parse(object(field(1, new byte[4]), t, expiry(2000000000L), field(6, new byte[8])));
        if (!parsed.token.equals(token) || parsed.expiryMillis != 2000000000000L) throw new AssertionError("token/expiry changed");
        passed++;
        if (GoogleAccountTokenData.parse(object(t)).expiryMillis != null) throw new AssertionError("expiry fabricated");
        passed++;
        if (GoogleAccountTokenData.parse(object(field(3, new byte[0]), t)).expiryMillis != null) throw new AssertionError("null expiry changed");
        passed++;
        // Fixed-size headers are valid SafeParcelable headers too.
        byte[] shortHeader = ByteBuffer.allocate(4 + string(token).length).order(ByteOrder.LITTLE_ENDIAN)
                .putInt((string(token).length << 16) | 2).put(string(token)).array();
        if (!GoogleAccountTokenData.parse(object(shortHeader)).token.equals(token)) throw new AssertionError("short header");
        passed++;
        deny(null); deny(new byte[0]); deny(new byte[131073]); deny(object());
        deny(object(t, t)); deny(object(t, expiry(1), expiry(2)));
        deny(object(t, expiry(0))); deny(object(t, expiry(-1))); deny(object(t, expiry(Long.MAX_VALUE)));
        deny(object(t, field(3, new byte[4])));
        deny(object(field(2, string(""))));
        for (String invalid : new String[] {"has space", "has\nnewline", "has\rreturn", "has\ttab", "nul\0here", "\u00e9", "x".repeat(16385)})
            deny(object(field(2, string(invalid))));
        byte[] bad = object(t); bad[0] = 0; deny(bad);
        bad = object(t); ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN).putInt(4, bad.length); deny(bad);
        bad = object(t); ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN).putInt(12, -1); deny(bad);
        bad = object(t); ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN).putInt(16, Integer.MAX_VALUE); deny(bad);
        bad = object(t); bad[20 + token.length() * 2] = 1; deny(bad);
        for (int end = 0; end < object(t).length; end++) deny(Arrays.copyOf(object(t), end));
        System.out.println("GOOGLE_ACCOUNT_TOKEN_DATA_PASS " + passed);
    }
}
