package adapter.compat;

import java.io.FileInputStream;
import java.io.IOException;
import java.security.SecureRandomSpi;

/**
 * OpenHarmony entropy source for Android's class-library SecureRandom contract.
 * The kernel CSPRNG already incorporates system entropy, so caller-provided
 * seeds do not replace or weaken it.
 */
public final class WestlakeSecureRandomSpi extends SecureRandomSpi {
    private static final long serialVersionUID = 1L;
    private static FileInputStream urandom;

    private static synchronized FileInputStream urandom() throws IOException {
        if (urandom == null) {
            urandom = new FileInputStream("/dev/urandom");
        }
        return urandom;
    }

    @Override
    protected void engineSetSeed(byte[] seed) {
        // The OH kernel CSPRNG is the authoritative entropy pool.
    }

    @Override
    protected void engineNextBytes(byte[] bytes) {
        if (bytes == null || bytes.length == 0) return;
        try {
            FileInputStream input = urandom();
            synchronized (WestlakeSecureRandomSpi.class) {
                int offset = 0;
                while (offset < bytes.length) {
                    int count = input.read(bytes, offset, bytes.length - offset);
                    if (count <= 0) {
                        throw new IOException("short read from /dev/urandom: " + count);
                    }
                    offset += count;
                }
            }
        } catch (IOException e) {
            throw new IllegalStateException("no entropy source available", e);
        }
    }

    @Override
    protected byte[] engineGenerateSeed(int numBytes) {
        byte[] result = new byte[numBytes];
        engineNextBytes(result);
        return result;
    }
}
