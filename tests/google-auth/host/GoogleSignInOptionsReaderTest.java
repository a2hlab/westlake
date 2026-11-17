package adapter.core;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Set;

public final class GoogleSignInOptionsReaderTest {
    private static int checks;
    private static byte[] i(int value) { return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(value).array(); }
    private static byte[] join(byte[]... arrays) {
        int size = 0; for (byte[] a : arrays) size += a.length;
        ByteBuffer out = ByteBuffer.allocate(size); for (byte[] a : arrays) out.put(a); return out.array();
    }
    private static byte[] s(String value) {
        if (value == null) return i(-1);
        byte[] text = (value + "\0").getBytes(StandardCharsets.UTF_16LE);
        return join(i(value.length()), Arrays.copyOf(text, (text.length + 3) & ~3));
    }
    private static byte[] field(int id, byte[] value) { return join(i(0xffff0000 | id), i(value.length), value); }
    private static byte[] object(byte[]... fields) { return field(20293, join(fields)); }
    private static byte[] scope(String name) { return object(field(1, i(1)), field(2, s(name))); }
    private static byte[] scopes(String... names) {
        byte[] out = i(names.length);
        for (String name : names) { byte[] item = scope(name); out = join(out, i(item.length), item); }
        return field(2, out);
    }
    private static byte[] options(byte[]... extra) {
        return join(i(1), object(field(1, i(3)), scopes("openid", "email", "profile"),
                field(4, i(0)), field(5, i(0)), field(6, i(0)), join(extra)));
    }
    private static final class Reader implements GoogleSignInOptionsReader.Reader {
        final ByteBuffer b;
        Reader(byte[] data) { b = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN); }
        public int position() { return b.position(); }
        public int size() { return b.limit(); }
        public int integer() { return b.getInt(); }
        public String string() {
            int n = integer(); if (n == -1) return null;
            if (n < 0 || n > 8192) throw new SecurityException();
            byte[] text = new byte[n * 2]; b.get(text); b.getShort();
            b.position((b.position() + 3) & ~3); return new String(text, StandardCharsets.UTF_16LE);
        }
        public String[] account() { String name = string(), type = string(); string(); return new String[] {name, type}; }
    }
    private static void require(boolean value) { checks++; if (!value) throw new AssertionError("check " + checks); }
    private static void reject(byte[] bytes) {
        try { GoogleSignInOptionsReader.read(new Reader(bytes)); }
        catch (RuntimeException denied) { checks++; return; }
        throw new AssertionError("accepted invalid options");
    }
    public static void main(String[] args) {
        GoogleSignInOptionsReader a = GoogleSignInOptionsReader.read(new Reader(options(field(9, i(0)), field(10, s("opaque-log-id")))));
        require(a.scopes.equals(Set.of("openid", "email", "profile")) && a.accountName == null);
        a = GoogleSignInOptionsReader.read(new Reader(options(field(3, join(s("test@example.invalid"), s("com.google"), s(null))))));
        require("test@example.invalid".equals(a.accountName));
        reject(options(field(3, join(s("test@example.invalid"), s("wrong.type"), s(null)))));
        reject(options(field(7, s("client.apps.googleusercontent.com"))));
        reject(options(field(8, s("restricted.example"))));
        reject(options(field(9, i(1))));
        reject(options(field(11, i(0))));
        reject(options(field(10, s("x".repeat(257)))));
        reject(options(field(4, i(1)))); // Duplicate known field cannot conceal a different value.
        for (int flag : new int[] {4, 5, 6}) reject(join(i(1), object(field(1, i(3)), scopes("email"), field(flag, i(1)))));
        reject(join(i(1), object(scopes("email", "email"))));
        reject(join(i(1), object(scopes())));
        reject(join(i(1), object(scopes("email profile"))));
        reject(join(i(1), object(field(1, i(99)), scopes("email"))));
        reject(join(options(), i(0)));
        byte[] shortObject = options(); ByteBuffer.wrap(shortObject).order(ByteOrder.LITTLE_ENDIAN).putInt(8, 4); reject(shortObject);
        byte[] overflow = options(); ByteBuffer.wrap(overflow).order(ByteOrder.LITTLE_ENDIAN).putInt(8, Integer.MAX_VALUE); reject(overflow);
        reject(Arrays.copyOf(options(), options().length - 1));
        reject(join(i(0), object(scopes("email"))));
        System.out.println("GOOGLE_SIGNIN_OPTIONS_PASS " + checks);
    }
}
