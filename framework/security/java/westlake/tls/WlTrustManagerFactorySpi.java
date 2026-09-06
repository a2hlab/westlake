package westlake.tls;

import java.security.InvalidAlgorithmParameterException;
import java.security.KeyStore;
import java.security.KeyStoreException;

import javax.net.ssl.ManagerFactoryParameters;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.TrustManagerFactorySpi;
import javax.net.ssl.X509TrustManager;

/**
 * Serves {@code TrustManagerFactory.getInstance("PKIX").init((KeyStore) null)} --
 * the "give me the platform trust anchors" request okhttp makes -- from the
 * anchors {@link TlsBootstrap} has already loaded.
 *
 * Two reasons this exists rather than letting BouncyCastle answer it:
 *
 *  1. CORRECTNESS.  BC resolves a null KeyStore through the
 *     javax.net.ssl.trustStore* system properties and, failing those, looks for
 *     $JAVA_HOME/lib/security/cacerts.  There is no such file on the board, so the
 *     app would end up with an empty trust set and every handshake would fail.
 *     Pointing those properties at a KeyStore file is what we used to do, which
 *     leads directly to the second reason.
 *
 *  2. COST.  That file had to be a KeyStore, and PKCS12's integrity check is an
 *     iterated password-based MAC over the whole file.  Measured, it was 81% of
 *     the entire TLS install (truststore=3066ms out of 3796ms on a desktop JVM;
 *     scaled to the board, ~23s of ~28s) -- and the app waits on that install.
 *     Worse, every app-side TrustManagerFactory would pay it again.
 *
 * Answering from memory makes both problems go away: the anchors are the ones we
 * already parsed, and it costs nothing.
 */
public final class WlTrustManagerFactorySpi extends TrustManagerFactorySpi {

    public WlTrustManagerFactorySpi() { }

    @Override
    protected void engineInit(KeyStore ks) throws KeyStoreException {
        // A non-null KeyStore means the caller has its own anchors and is not
        // asking for the platform set, so honour it rather than substituting ours.
        if (ks != null) {
            mCallerSupplied = fromKeyStore(ks);
        }
    }

    @Override
    protected void engineInit(ManagerFactoryParameters spec)
            throws InvalidAlgorithmParameterException {
        // Delegate parameter-driven init to the real provider; nothing to shortcut.
        try {
            TrustManagerFactory f = TrustManagerFactory.getInstance(
                    "PKIX", TlsBootstrap.jsseProvider());
            f.init(spec);
            TrustManager[] tms = f.getTrustManagers();
            for (int i = 0; i < tms.length; i++) {
                if (tms[i] instanceof X509TrustManager) {
                    mCallerSupplied = (X509TrustManager) tms[i];
                    return;
                }
            }
        } catch (InvalidAlgorithmParameterException e) {
            throw e;
        } catch (Exception e) {
            throw new InvalidAlgorithmParameterException(e);
        }
    }

    @Override
    protected TrustManager[] engineGetTrustManagers() {
        X509TrustManager tm = mCallerSupplied;
        if (tm == null) tm = TlsBootstrap.trustManager();
        if (tm == null) {
            throw new IllegalStateException(
                    "TLS not installed: " + TlsBootstrap.status());
        }
        return new TrustManager[] { tm };
    }

    private volatile X509TrustManager mCallerSupplied;

    private static X509TrustManager fromKeyStore(KeyStore ks) throws KeyStoreException {
        try {
            TrustManagerFactory f = TrustManagerFactory.getInstance(
                    "PKIX", TlsBootstrap.jsseProvider());
            f.init(ks);
            TrustManager[] tms = f.getTrustManagers();
            for (int i = 0; i < tms.length; i++) {
                if (tms[i] instanceof X509TrustManager) return (X509TrustManager) tms[i];
            }
            return null;
        } catch (KeyStoreException e) {
            throw e;
        } catch (Exception e) {
            throw new KeyStoreException(e);
        }
    }
}
