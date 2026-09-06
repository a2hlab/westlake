package westlake.tls;

import java.security.KeyManagementException;
import java.security.SecureRandom;

import javax.net.ssl.KeyManager;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLContextSpi;
import javax.net.ssl.SSLEngine;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLServerSocketFactory;
import javax.net.ssl.SSLSessionContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

/**
 * The SSLContext the app actually gets: BouncyCastle underneath, with one
 * substitution applied to whatever trust managers the caller passes in.
 *
 * okhttp asks the platform for its trust managers and hands them to
 * SSLContext.init().  On this adapter the platform answers with
 * android.security.net.config.RootTrustManager, whose checkServerTrusted()
 * constructs com.android.org.conscrypt.TrustManagerImpl -- absent here, so
 * mid-handshake it throws
 *
 *   java.lang.NoSuchMethodError: No direct method
 *     <init>(KeyStore;CertPinManager;ConscryptCertStore)V
 *     in class Lcom/android/org/conscrypt/TrustManagerImpl;
 *       at android.security.net.config.RootTrustManager.checkServerTrusted
 *       at okhttp3.internal.connection.RealConnection.connectTls
 *
 * Being an Error rather than an Exception, it slips past okhttp's
 * `catch (Exception)` and unwinds through connectTls(), whose finally-block closes
 * the socket -- which is the user_canceled(90) alert seen on nearly every app
 * connection.  Swapping that one trust manager for ours is enough; anything else
 * the caller supplies (pinning, a private CA) is passed through untouched.
 */
public final class WlSslContextSpi extends SSLContextSpi {

    static final String ANDROID_ROOT_TRUST_MANAGER =
            "android.security.net.config.RootTrustManager";

    private volatile SSLContext mDelegate;

    public WlSslContextSpi() { }

    /** A BouncyCastle context, default-initialised, for callers that skip init(). */
    private SSLContext ready() {
        SSLContext d = mDelegate;
        if (d != null) return d;
        synchronized (this) {
            if (mDelegate == null) {
                SSLContext base = TlsBootstrap.context();
                if (base == null) {
                    TlsBootstrap.install();
                    base = TlsBootstrap.context();
                }
                if (base == null) {
                    throw new IllegalStateException(
                            "TLS not installed: " + TlsBootstrap.status());
                }
                mDelegate = base;
            }
            return mDelegate;
        }
    }

    @Override
    protected void engineInit(KeyManager[] km, TrustManager[] tm, SecureRandom sr)
            throws KeyManagementException {
        try {
            // Name BouncyCastle explicitly: resolving by order would come straight
            // back here.
            SSLContext c = SSLContext.getInstance("TLS", TlsBootstrap.jsseProvider());
            c.init(km, substitute(tm), sr);
            mDelegate = c;
        } catch (KeyManagementException e) {
            throw e;
        } catch (Exception e) {
            throw new KeyManagementException("TLS front-end init failed", e);
        }
    }

    static TrustManager[] substitute(TrustManager[] tms) {
        X509TrustManager ours = TlsBootstrap.trustManager();
        if (ours == null) return tms;
        if (tms == null || tms.length == 0) return tms;   // null means "use default"
        TrustManager[] out = null;
        for (int i = 0; i < tms.length; i++) {
            if (tms[i] == null) continue;
            if (ANDROID_ROOT_TRUST_MANAGER.equals(tms[i].getClass().getName())) {
                if (out == null) {
                    out = new TrustManager[tms.length];
                    System.arraycopy(tms, 0, out, 0, tms.length);
                }
                out[i] = ours;
                TlsBootstrap.log("replaced " + ANDROID_ROOT_TRUST_MANAGER
                        + " (wants conscrypt's TrustManagerImpl, absent here)");
            }
        }
        return out != null ? out : tms;
    }

    @Override protected SSLSocketFactory engineGetSocketFactory() {
        SSLSocketFactory f = ready().getSocketFactory();
        boolean tap = TlsBootstrap.tapEnabled();
        TlsBootstrap.log("front-end getSocketFactory (tap=" + tap + ")");
        return tap ? TlsBootstrap.tap(f) : f;
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
