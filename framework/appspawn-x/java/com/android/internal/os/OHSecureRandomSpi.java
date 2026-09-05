/*
 * OHSecureRandomSpi.java
 *
 * A SecureRandomSpi backed directly by /dev/urandom.
 *
 * Why this is needed at all
 * -------------------------
 * On Android, SecureRandom is owned by Conscrypt. This adapter has no Conscrypt
 * -- libconscrypt_jni.so does not exist on OH -- so AppSpawnXInit's
 * overrideJcaProvidersForOH() prunes both Conscrypt entries from
 * Security.props, leaving BouncyCastle and CertPathProvider. That is enough for
 * message digests (which the same method patches back in, because Android's
 * repackaged BC has them "Android-removed" for exactly the same reason), but it
 * leaves the process with no SecureRandom service at all:
 *
 *     NoSuchAlgorithmException: SecureRandom not available
 *
 * which surfaces during static initialisation of AndroidX components -- the
 * NavHostFragment path was the first one observed -- and, more importantly,
 * inside any TLS handshake, which cannot proceed without a random source.
 *
 * Why /dev/urandom, and why not a Java fallback
 * ---------------------------------------------
 * /dev/urandom is the kernel CSPRNG. It is the correct source on Linux and on
 * OH: it is seeded before userspace starts, it never blocks after that, and it
 * needs no JNI, no native library and no entropy accounting of our own. This
 * class is therefore self-contained -- it can be installed before any adapter
 * native library is loaded.
 *
 * If /dev/urandom cannot be opened, this class throws. It deliberately does not
 * degrade to java.util.Random, System.nanoTime() or any other guessable source.
 * A SecureRandom that silently stops being secure is worse than one that is
 * absent: the absence is a loud NoSuchAlgorithmException at start-up, whereas a
 * weak one produces TLS session keys and nonces that look fine and are not.
 */
package com.android.internal.os;

import java.io.DataInputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.security.SecureRandomSpi;

public final class OHSecureRandomSpi extends SecureRandomSpi {

    private static final long serialVersionUID = 1L;

    private static final File URANDOM = new File("/dev/urandom");

    /**
     * One shared reader for the whole process.
     *
     * SecureRandom instances are cheap and applications create them freely; a
     * file descriptor per instance would leak descriptors under any code that
     * allocates one per request. The kernel CSPRNG is a shared source anyway,
     * so sharing the descriptor costs nothing in randomness quality.
     */
    private static final Object READ_LOCK = new Object();
    private static DataInputStream sInput;

    /**
     * @throws SecurityException if the kernel random source is unavailable.
     *         Thrown from the constructor so JCA reports the provider as
     *         unusable rather than handing out a broken SecureRandom.
     */
    public OHSecureRandomSpi() {
        openIfNeeded();
    }

    private static void openIfNeeded() {
        synchronized (READ_LOCK) {
            if (sInput != null) {
                return;
            }
            try {
                sInput = new DataInputStream(new java.io.FileInputStream(URANDOM));
            } catch (FileNotFoundException e) {
                throw new SecurityException("no kernel random source at " + URANDOM, e);
            }
        }
    }

    @Override
    protected void engineNextBytes(byte[] bytes) {
        if (bytes == null || bytes.length == 0) {
            return;
        }
        synchronized (READ_LOCK) {
            openIfNeeded();
            try {
                // readFully, not read: a short read here would leave the tail of
                // the buffer as zeroes, which is precisely the silent weakness
                // this class refuses to have.
                sInput.readFully(bytes);
            } catch (IOException e) {
                // Drop the descriptor so the next call retries from scratch
                // rather than reusing a stream that has already failed.
                closeQuietly();
                throw new SecurityException("failed to read " + URANDOM, e);
            }
        }
    }

    @Override
    protected byte[] engineGenerateSeed(int numBytes) {
        if (numBytes < 0) {
            throw new IllegalArgumentException("numBytes < 0: " + numBytes);
        }
        byte[] seed = new byte[numBytes];
        engineNextBytes(seed);
        return seed;
    }

    /**
     * Stir the caller's seed into the kernel pool.
     *
     * Writing to /dev/urandom mixes the bytes in without crediting entropy,
     * which is exactly the right semantics for a seed supplied by application
     * code: it can only help, and a caller who supplies a constant seed cannot
     * make our output predictable. That is a deliberate difference from
     * SHA1PRNG, where setSeed() with a fixed value makes the stream
     * reproducible; code that depends on that behaviour is misusing
     * SecureRandom, and quietly obliging it would be the insecure choice.
     *
     * Failure is ignored: the write needs no permission we are guaranteed to
     * have, and not stirring the pool is harmless.
     */
    @Override
    protected void engineSetSeed(byte[] seed) {
        if (seed == null || seed.length == 0) {
            return;
        }
        OutputStream out = null;
        try {
            out = new FileOutputStream(URANDOM);
            out.write(seed);
            out.flush();
        } catch (IOException | SecurityException ignored) {
            // Best effort by design; see the method comment.
        } finally {
            if (out != null) {
                try {
                    out.close();
                } catch (IOException ignored) {
                    // Nothing useful to do.
                }
            }
        }
    }

    private static void closeQuietly() {
        synchronized (READ_LOCK) {
            if (sInput != null) {
                try {
                    sInput.close();
                } catch (IOException ignored) {
                    // Nothing useful to do.
                }
                sInput = null;
            }
        }
    }
}
