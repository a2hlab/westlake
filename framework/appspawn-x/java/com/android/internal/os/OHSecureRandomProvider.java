/*
 * OHSecureRandomProvider.java
 *
 * The JCA provider that publishes OHSecureRandomSpi.
 *
 * Installed by AppSpawnXInit.overrideJcaProvidersForOH(), immediately after the
 * Conscrypt entries are pruned from Security.props. Conscrypt owned SecureRandom
 * on Android; with it gone, and with Android's repackaged BouncyCastle having
 * its SecureRandom registrations "Android-removed" for the same reason, the
 * process would otherwise have no SecureRandom service at all -- which breaks
 * AndroidX static initialisers and makes every TLS handshake impossible.
 *
 * The service names registered here are the ones callers actually ask for:
 *
 *   SHA1PRNG                is what Android code names explicitly, and what
 *                           SecureRandom.getInstance("SHA1PRNG") looks up.
 *                           Note that this implementation is *not* SHA1PRNG's
 *                           algorithm -- it is the kernel CSPRNG wearing that
 *                           name, which is strictly stronger. See
 *                           OHSecureRandomSpi.engineSetSeed for the one
 *                           behavioural difference that follows from this.
 *   DEFAULT                 satisfies the no-argument SecureRandom() path.
 *   DRBG                    the name JDK 9+ code reaches for.
 *   NativePRNG,
 *   NativePRNGNonBlocking   the OpenJDK names for exactly this source, so an
 *                           alias is the honest mapping rather than a
 *                           substitution.
 *
 * Everything is an alias of one implementation because there is one source:
 * /dev/urandom. Offering several names that all resolve to the kernel CSPRNG is
 * accurate; offering several names backed by different quality levels would not
 * be.
 */
package com.android.internal.os;

import java.security.Provider;

public final class OHSecureRandomProvider extends Provider {

    private static final long serialVersionUID = 1L;

    public static final String NAME = "OHSecureRandom";

    private static final String SPI = OHSecureRandomSpi.class.getName();

    @SuppressWarnings("deprecation")  // Provider(String,double,String) on API < 30
    public OHSecureRandomProvider() {
        super(NAME, 1.0, "OH SecureRandom via /dev/urandom");

        put("SecureRandom.SHA1PRNG", SPI);
        put("SecureRandom.DEFAULT", SPI);
        put("Alg.Alias.SecureRandom.DRBG", "SHA1PRNG");
        put("Alg.Alias.SecureRandom.NativePRNG", "SHA1PRNG");
        put("Alg.Alias.SecureRandom.NativePRNGNonBlocking", "SHA1PRNG");

        // Marked as thread-safe so JCA does not serialise every caller behind a
        // single lock. OHSecureRandomSpi guards its shared descriptor itself.
        put("SecureRandom.SHA1PRNG ThreadSafe", "true");
        put("SecureRandom.DEFAULT ThreadSafe", "true");
    }

    /**
     * Install this provider unless a working SecureRandom already exists.
     *
     * Returns the provider name that ends up serving SecureRandom, or null if
     * none could be established -- in which case the caller should say so
     * loudly, because TLS will not work.
     *
     * Inserted at position 1 rather than appended: BouncyCastle is registered
     * first for general use, and if a future BC build does start offering a
     * SecureRandom we still want the kernel source to win.
     */
    public static String install() {
        try {
            if (java.security.Security.getProvider(NAME) == null) {
                java.security.Security.insertProviderAt(new OHSecureRandomProvider(), 1);
            }
            java.security.SecureRandom probe = java.security.SecureRandom.getInstance("SHA1PRNG");
            byte[] sample = new byte[8];
            probe.nextBytes(sample);
            return probe.getProvider().getName();
        } catch (Throwable t) {
            System.err.println("[WESTLAKE-JCA] OHSecureRandomProvider install FAILED: " + t);
            return null;
        }
    }
}
