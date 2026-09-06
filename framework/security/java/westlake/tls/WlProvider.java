package westlake.tls;

import java.security.Provider;

/**
 * A deliberately tiny JCA provider that sits ahead of everything else and answers
 * exactly one question: "what are the platform trust anchors?"
 *
 * Everything else -- SSLContext, the handshake, the crypto -- still comes from
 * BouncyCastle, which sits directly behind this.  See
 * {@link WlTrustManagerFactorySpi} for why that one service is worth intercepting.
 *
 * The aliases matter: okhttp asks for TrustManagerFactory.getDefaultAlgorithm(),
 * which security.properties on this board defines as "PKIX", but other code
 * reaches for "X509" or "SunPKIX", and all three must land here or they fall
 * through to a provider that cannot find a trust store.
 */
public final class WlProvider extends Provider {

    public static final String NAME = "WLTLS";

    @SuppressWarnings("deprecation")   // the (String,double,String) ctor is the
                                       // only one AOSP's Provider offers
    public WlProvider() {
        super(NAME, 1.0, "Westlake TLS front-end");
        put("TrustManagerFactory.PKIX", WlTrustManagerFactorySpi.class.getName());
        put("Alg.Alias.TrustManagerFactory.SunPKIX", "PKIX");
        put("Alg.Alias.TrustManagerFactory.X509", "PKIX");
        put("Alg.Alias.TrustManagerFactory.X.509", "PKIX");

        // SSLContext too, so that every context the app builds gets the broken
        // platform trust manager swapped out -- see WlSslContextSpi.  BouncyCastle
        // still does all the actual work behind it.
        put("SSLContext.TLS", WlSslContextSpi.class.getName());
        put("Alg.Alias.SSLContext.Default", "TLS");
        put("Alg.Alias.SSLContext.SSL", "TLS");
        put("Alg.Alias.SSLContext.TLSv1", "TLS");
        put("Alg.Alias.SSLContext.TLSv1.1", "TLS");
        put("Alg.Alias.SSLContext.TLSv1.2", "TLS");
        put("Alg.Alias.SSLContext.TLSv1.3", "TLS");
    }
}
