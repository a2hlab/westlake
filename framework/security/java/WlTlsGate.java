/*
 * WlTlsGate.java
 *
 * The JSSE front door for the adapter: a pair of JCA SPIs that make every
 * caller wait for a working TLS stack instead of handing out a broken one.
 *
 * The problem
 * -----------
 * The adapter registers a provider named "TlsShim" whose SSLContext can be
 * constructed but cannot connect -- its own log line is
 *
 *     TLS shim: no real networking (construct-only SSLContext on OH)
 *
 * A real JSSE provider can be stood up (see WlTlsBootstrap), but it costs
 * seconds: several thousand classes on a runtime forced to -Xint. That creates
 * a race nobody wins by being fast. On the reproduction target TTNet's first
 * DoConnect lands before t=30s while the provider takes ~31s, and okhttp caches
 * the SSLSocketFactory it obtained from that first lookup -- so one early
 * question permanently pins the whole process to the broken shim.
 *
 * The approach
 * ------------
 * Do not race; take the shim's place. java.security.Provider is a Map, so its
 * service entries can be re-pointed at the SPIs below (hijackShimProvider()).
 * JCA then instantiates these instead. A caller arriving before the real stack
 * is up blocks on a latch rather than receiving a broken context, so there is
 * no wrong answer to cache: whoever asks first simply waits.
 *
 * Once WlTlsBootstrap publishes the real provider, both SPIs become thin
 * delegates and stay out of the way.
 *
 * Ownership of the pieces
 * -----------------------
 * This class is the part that is pure source and belongs in the framework. It
 * knows nothing about where a real provider comes from: something calls
 * publish(), and until it does, callers wait. WlTlsBootstrap is one such
 * supplier; a future in-tree Conscrypt replacement would be another, and would
 * not require changing anything here.
 */
package adapter.security;

import java.io.IOException;
import java.security.KeyManagementException;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.InvalidAlgorithmParameterException;
import java.security.Provider;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import javax.net.ssl.KeyManager;
import javax.net.ssl.ManagerFactoryParameters;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLContextSpi;
import javax.net.ssl.SSLEngine;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLServerSocketFactory;
import javax.net.ssl.SSLSessionContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.TrustManagerFactorySpi;
import javax.net.ssl.X509TrustManager;

public final class WlTlsGate {

    private static final String TAG = "[WL-TLS]";

    /** The adapter's construct-only provider, whose services we take over. */
    private static final String SHIM_PROVIDER = "TlsShim";

    /**
     * Android's Network Security Config trust manager, which cannot work here.
     *
     * okhttp asks the platform for its trust managers and hands whatever it gets
     * to SSLContext.init(). On this adapter that is RootTrustManager, whose
     * checkServerTrusted() builds a com.android.org.conscrypt.TrustManagerImpl
     * -- a class the adapter does not ship. Mid-handshake:
     *
     *   NoSuchMethodError: No direct method
     *     <init>(KeyStore;CertPinManager;ConscryptCertStore)V
     *     in class Lcom/android/org/conscrypt/TrustManagerImpl;
     *       at android.security.net.config.NetworkSecurityTrustManager.<init>
     *       at android.security.net.config.RootTrustManager.checkServerTrusted
     *       at okhttp3.internal.connection.RealConnection.connectTls
     *
     * That is an Error, not an Exception, so okhttp's `catch (Exception)` misses
     * it; it unwinds through connectTls(), whose finally block does
     * closeQuietly(sslSocket) -- surfacing as the user_canceled(90) alerts that
     * killed nearly every connection in the process.
     */
    private static final String ANDROID_ROOT_TRUST_MANAGER =
            "android.security.net.config.RootTrustManager";

    /** How long a caller waits for the real stack before giving up. */
    private static final long WAIT_MS = 120_000L;

    private static final CountDownLatch sReady = new CountDownLatch(1);

    /** The real JSSE provider, published by whoever stands one up. */
    private static volatile Provider sProvider;

    /** A working trust manager, published alongside it. May stay null. */
    private static volatile X509TrustManager sTrustManager;

    private WlTlsGate() { }

    /**
     * Publish the real TLS stack and release everyone waiting on it.
     *
     * Must be called exactly once per process, including on failure: a supplier
     * that gives up has to pass null so blocked callers get a prompt error
     * instead of sitting out the full two-minute timeout.
     *
     * @param provider     the real JSSE provider, or null if none could be built
     * @param trustManager a usable trust manager, or null to leave the caller's
     *                     own anchors untouched
     */
    public static void publish(Provider provider, X509TrustManager trustManager) {
        if (provider != null && delegatesBackToGate(provider)) {
            // Refuse rather than accept: every SPI method here resolves its
            // delegate through the published provider, so a provider that
            // resolves back to us recurses until StackOverflowError -- thrown
            // from inside SSLContext.init(), several frames deep, on whatever
            // thread happened to open the first connection. That is a miserable
            // thing to diagnose on a board we cannot attach to, and it is cheap
            // to rule out here. Reached if a supplier passes the shim provider
            // itself, or passes the result of a plain SSLContext.getInstance()
            // taken *after* hijackShimProvider() has run.
            System.err.println(TAG + " REFUSED provider '" + provider.getName()
                    + "': its SSLContext service resolves back to this gate."
                    + " Capture the real provider before hijackShimProvider() runs.");
            provider = null;
            trustManager = null;
        }
        sProvider = provider;
        sTrustManager = trustManager;
        sReady.countDown();
        System.err.println(TAG + " gate opened: provider="
                + (provider == null ? "NONE" : provider.getName())
                + " trustManager=" + (trustManager == null ? "none" : "yes"));
    }

    /** True if this provider's SSLContext services are served by our own SPI. */
    private static boolean delegatesBackToGate(Provider provider) {
        String ours = TlsGateSpi.class.getName();
        for (Object key : provider.keySet()) {
            if (key instanceof String && ((String) key).startsWith("SSLContext.")
                    && ours.equals(provider.get(key))) {
                return true;
            }
        }
        return false;
    }

    /** True once publish() has been called, whatever the outcome. */
    public static boolean isSettled() {
        return sReady.getCount() == 0;
    }

    /** The real provider, blocking until it exists or the wait times out. */
    static Provider awaitProvider() throws IOException {
        Provider provider = sProvider;
        if (provider != null) {
            return provider;
        }
        try {
            long start = System.currentTimeMillis();
            if (!sReady.await(WAIT_MS, TimeUnit.MILLISECONDS)) {
                throw new IOException("timed out waiting for the TLS provider");
            }
            long waited = System.currentTimeMillis() - start;
            if (waited > 50) {
                System.err.println(TAG + " gate: caller waited " + waited
                        + "ms for the real provider");
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted waiting for the TLS provider", e);
        }
        provider = sProvider;
        if (provider == null) {
            throw new IOException("TLS provider unavailable");
        }
        return provider;
    }

    /**
     * Replace the platform trust manager with ours; leave everything else alone.
     *
     * A caller that brings its own anchors -- certificate pinning, a private CA
     * -- must keep them, so only the known-broken platform one is substituted.
     */
    static TrustManager[] sanitizeTrustManagers(TrustManager[] managers) {
        X509TrustManager ours = sTrustManager;
        if (ours == null) {
            return managers;                       // nothing better to offer
        }
        if (managers == null || managers.length == 0) {
            return new TrustManager[] { ours };
        }
        TrustManager[] out = null;
        for (int i = 0; i < managers.length; i++) {
            if (managers[i] == null) {
                continue;
            }
            if (ANDROID_ROOT_TRUST_MANAGER.equals(managers[i].getClass().getName())) {
                if (out == null) {
                    out = new TrustManager[managers.length];
                    System.arraycopy(managers, 0, out, 0, managers.length);
                }
                out[i] = ours;
                System.err.println(TAG + " replaced " + ANDROID_ROOT_TRUST_MANAGER
                        + " (needs conscrypt's TrustManagerImpl, absent here)"
                        + " with the bridge's trust manager");
            }
        }
        return out != null ? out : managers;
    }

    /**
     * Re-point the adapter's shim provider at this gate.
     *
     * Cheap and synchronous -- it only rewrites map entries -- so it is safe on
     * the attach path, and it closes the window before any app code can obtain a
     * broken SSLContext. Call it as early as possible; everything else about the
     * TLS stack can happen later.
     *
     * @return the number of services re-pointed, or -1 if the shim was absent
     */
    public static int hijackShimProvider() {
        try {
            Provider shim = java.security.Security.getProvider(SHIM_PROVIDER);
            if (shim == null) {
                System.err.println(TAG + " no '" + SHIM_PROVIDER + "' provider to hijack"
                        + " (adapter may have changed); relying on provider order");
                return -1;
            }
            String contextSpi = TlsGateSpi.class.getName();
            String trustSpi = TrustGateSpi.class.getName();
            // Snapshot the keys: the loop mutates the map it is reading.
            List<String> keys = new ArrayList<>();
            for (Object key : shim.keySet()) {
                if (key instanceof String) {
                    keys.add((String) key);
                }
            }
            int repointed = 0;
            for (String key : keys) {
                if (key.startsWith("Alg.Alias.")) {
                    continue;                      // aliases follow the real entry
                }
                if (key.startsWith("SSLContext.")) {
                    shim.put(key, contextSpi);
                    repointed++;
                } else if (key.startsWith("TrustManagerFactory.")) {
                    shim.put(key, trustSpi);
                    repointed++;
                }
            }
            System.err.println(TAG + " hijacked '" + SHIM_PROVIDER + "' provider: "
                    + repointed + " service(s) re-pointed at the gate");
            return repointed;
        } catch (Throwable t) {
            System.err.println(TAG + " hijackShimProvider failed: " + t);
            return -1;
        }
    }

    /** SSLContext.*: waits for the real provider, then is a plain delegate. */
    public static final class TlsGateSpi extends SSLContextSpi {

        private volatile SSLContext mDelegate;

        public TlsGateSpi() { }

        /** The delegate, default-initialised if the caller never called init(). */
        private SSLContext ready() {
            SSLContext delegate = mDelegate;
            if (delegate != null) {
                return delegate;
            }
            synchronized (this) {
                if (mDelegate == null) {
                    try {
                        SSLContext context = SSLContext.getInstance("TLS", awaitProvider());
                        context.init(null, null, null);
                        mDelegate = context;
                    } catch (Throwable t) {
                        throw new IllegalStateException("TLS gate: no real provider", t);
                    }
                }
                return mDelegate;
            }
        }

        @Override
        protected void engineInit(KeyManager[] keyManagers, TrustManager[] trustManagers,
                                  SecureRandom random) throws KeyManagementException {
            try {
                SSLContext context = SSLContext.getInstance("TLS", awaitProvider());
                context.init(keyManagers, sanitizeTrustManagers(trustManagers), random);
                mDelegate = context;
            } catch (KeyManagementException e) {
                throw e;
            } catch (Throwable t) {
                throw new KeyManagementException("TLS gate init failed", t);
            }
        }

        @Override protected SSLSocketFactory engineGetSocketFactory() {
            return ready().getSocketFactory();
        }

        @Override protected SSLServerSocketFactory engineGetServerSocketFactory() {
            return ready().getServerSocketFactory();
        }

        @Override protected SSLEngine engineCreateSSLEngine() {
            return ready().createSSLEngine();
        }

        @Override protected SSLEngine engineCreateSSLEngine(String host, int port) {
            return ready().createSSLEngine(host, port);
        }

        @Override protected SSLSessionContext engineGetServerSessionContext() {
            return ready().getServerSessionContext();
        }

        @Override protected SSLSessionContext engineGetClientSessionContext() {
            return ready().getClientSessionContext();
        }

        @Override protected SSLParameters engineGetDefaultSSLParameters() {
            return ready().getDefaultSSLParameters();
        }

        @Override protected SSLParameters engineGetSupportedSSLParameters() {
            return ready().getSupportedSSLParameters();
        }
    }

    /**
     * TrustManagerFactory.*: the shim answered this with an accept-all trust
     * manager, so it has to be re-pointed too -- okhttp asks for the platform
     * trust managers here and hands them straight to SSLContext.init().
     */
    public static final class TrustGateSpi extends TrustManagerFactorySpi {

        private volatile TrustManagerFactory mDelegate;

        public TrustGateSpi() { }

        private TrustManagerFactory delegate() throws Exception {
            TrustManagerFactory factory = mDelegate;
            if (factory == null) {
                // Wait for the stack, then honour the normal provider order
                // rather than naming the JSSE provider directly: the bootstrap
                // installs a front end ahead of it that answers the
                // platform-trust-anchor request from memory, and asking the JSSE
                // provider itself would send it looking for a trust store file
                // the board does not have.
                //
                // "Honour the order" cannot mean a bare
                // TrustManagerFactory.getInstance("PKIX"), though. We are
                // registered under the shim provider, the shim sits at position
                // 1, and its TrustManagerFactory.PKIX now points here -- so the
                // unqualified lookup finds *this class* and recurses until
                // StackOverflowError, thrown from inside
                // TrustManagerFactory.init() on whatever thread first asked for
                // trust managers. Walk the order explicitly and skip ourselves.
                awaitProvider();
                factory = firstForeignPkix();
                mDelegate = factory;
            }
            return factory;
        }

        /**
         * The first PKIX TrustManagerFactory in provider order that is not this
         * gate.
         *
         * @throws java.security.NoSuchAlgorithmException if the only PKIX
         *         service in the process is our own -- which means the real
         *         stack never came up, and saying so is far more useful than
         *         recursing.
         */
        private static TrustManagerFactory firstForeignPkix()
                throws java.security.NoSuchAlgorithmException {
            String ours = TrustGateSpi.class.getName();
            for (Provider provider : java.security.Security.getProviders()) {
                Object spi = provider.get("TrustManagerFactory.PKIX");
                if (spi == null || ours.equals(spi)) {
                    continue;
                }
                try {
                    return TrustManagerFactory.getInstance("PKIX", provider);
                } catch (java.security.NoSuchAlgorithmException ignored) {
                    // Advertised but not loadable; keep looking.
                }
            }
            throw new java.security.NoSuchAlgorithmException(
                    "no PKIX TrustManagerFactory outside the TLS gate");
        }

        @Override
        protected void engineInit(KeyStore keyStore) throws KeyStoreException {
            try {
                delegate().init(keyStore);
            } catch (KeyStoreException e) {
                throw e;
            } catch (Exception e) {
                throw new KeyStoreException("TLS gate trust init failed", e);
            }
        }

        @Override
        protected void engineInit(ManagerFactoryParameters parameters)
                throws InvalidAlgorithmParameterException {
            try {
                delegate().init(parameters);
            } catch (InvalidAlgorithmParameterException e) {
                throw e;
            } catch (Exception e) {
                throw new InvalidAlgorithmParameterException(e);
            }
        }

        @Override
        protected TrustManager[] engineGetTrustManagers() {
            try {
                TrustManagerFactory factory = delegate();
                TrustManager[] managers = factory.getTrustManagers();
                if (managers == null || managers.length == 0) {
                    // Not initialised by the caller: PKIX with a null KeyStore
                    // picks up the anchors the bootstrap exported.
                    factory.init((KeyStore) null);
                    managers = factory.getTrustManagers();
                }
                return managers;
            } catch (Exception e) {
                throw new IllegalStateException("TLS gate: no trust managers", e);
            }
        }
    }
}
