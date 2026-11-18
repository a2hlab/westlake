package adapter.core;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** Pinned GMS TokenData fields, read only after an authenticated official-GMS reply. */
public final class GoogleAccountTokenData {
    public final String token;
    public final Long expiryMillis;

    private GoogleAccountTokenData(String token, Long expiryMillis) {
        this.token = token;
        this.expiryMillis = expiryMillis;
    }

    public static GoogleAccountTokenData parse(byte[] encoded) {
        if (encoded == null || encoded.length < 8 || encoded.length > 131072)
            throw new SecurityException("token data size");
        try {
            ByteBuffer data = ByteBuffer.wrap(encoded).order(ByteOrder.LITTLE_ENDIAN);
            int object = data.getInt();
            if ((object & 65535) != 20293 || size(data, object) != data.remaining())
                throw new SecurityException("token data object");
            String token = null;
            Long expiry = null;
            boolean expirySeen = false;
            while (data.hasRemaining()) {
                int header = data.getInt(), length = size(data, header);
                if (length < 0 || length > data.remaining()) throw new SecurityException("token data field");
                int end = data.position() + length;
                if ((header & 65535) == 2) {
                    if (token != null || length < 8) throw new SecurityException("token data string");
                    int chars = data.getInt();
                    if (chars <= 0 || chars > 16384 || ((4 + (chars + 1) * 2 + 3) & ~3) != length)
                        throw new SecurityException("token data string size");
                    byte[] text = new byte[chars * 2];
                    data.get(text);
                    if (data.getShort() != 0) throw new SecurityException("token data string terminator");
                    token = new String(text, StandardCharsets.UTF_16LE);
                    if (!token.matches("[\\x21-\\x7e]+")) throw new SecurityException("token data characters");
                } else if ((header & 65535) == 3) {
                    if (expirySeen || (length != 0 && length != 8)) throw new SecurityException("token data expiry");
                    expirySeen = true;
                    if (length == 8) {
                        long seconds = data.getLong();
                        if (seconds <= 0 || seconds > Long.MAX_VALUE / 1000)
                            throw new SecurityException("token data expiry range");
                        expiry = seconds * 1000;
                    }
                }
                data.position(end);
            }
            if (token == null) throw new SecurityException("token data absent");
            return new GoogleAccountTokenData(token, expiry);
        } catch (java.nio.BufferUnderflowException | IllegalArgumentException error) {
            throw new SecurityException("token data encoding");
        }
    }

    private static int size(ByteBuffer data, int header) {
        return (header >>> 16) == 65535 ? data.getInt() : header >>> 16;
    }
}
