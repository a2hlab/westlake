/* Installs the OpenHarmony-backed TrustManagerFactory into Android's active JCA provider. */
package adapter.security;

import java.security.Provider;
import java.security.Security;

/** Process-wide entry point called after AppSpawnX initializes its JCA providers. */
public final class OhTrustBridge {
    private static final String ALGORITHM = "OH-PKIX";
    private static boolean installed;

    private OhTrustBridge() {}

    public static synchronized void install() {
        if (installed) {
            return;
        }
        // The standalone launcher enters its optional POSIX extension here,
        // after core native registration and before any application starts.
        // It opens no sockets/IPC clients in the prefork process.
        String networkHelper = System.getenv("WESTLAKE_NET_HELPER_PATH");
        if (networkHelper != null && !networkHelper.isEmpty()) System.load(networkHelper);
        Provider provider = Security.getProvider("BC");
        if (provider == null) {
            throw new IllegalStateException("BC provider is not initialized");
        }
        initializeBouncyCastleDigests();
        // Conscrypt is absent, so publish the existing OH kernel-backed SPI
        // before JceSecurity's static initializer requests its default PRNG.
        // Never substitute a deterministic seed or retry with weak randomness.
        provider.put("SecureRandom.WestlakeKernel", "adapter.compat.WestlakeSecureRandomSpi");
        provider.put("SecureRandom.WestlakeKernel ThreadSafe", "true");
        // The trimmed Android boot class path includes Bouncy Castle's RSA
        // implementation classes, but not the provider mapping class that
        // normally publishes their JCA names.  PKIX certificate validation
        // looks signatures up by both digest names and ASN.1 OIDs, so restore
        // the standard Android BC registrations at the platform boundary.
        registerRsaDigestSignature(provider, "SHA1",
                "com.android.org.bouncycastle.jcajce.provider.asymmetric.rsa."
                        + "DigestSignatureSpi$SHA1",
                "1.2.840.113549.1.1.5");
        registerRsaDigestSignature(provider, "SHA224",
                "com.android.org.bouncycastle.jcajce.provider.asymmetric.rsa."
                        + "DigestSignatureSpi$SHA224",
                "1.2.840.113549.1.1.14");
        registerRsaDigestSignature(provider, "SHA256",
                "com.android.org.bouncycastle.jcajce.provider.asymmetric.rsa."
                        + "DigestSignatureSpi$SHA256",
                "1.2.840.113549.1.1.11");
        registerRsaDigestSignature(provider, "SHA384",
                "com.android.org.bouncycastle.jcajce.provider.asymmetric.rsa."
                        + "DigestSignatureSpi$SHA384",
                "1.2.840.113549.1.1.12");
        registerRsaDigestSignature(provider, "SHA512",
                "com.android.org.bouncycastle.jcajce.provider.asymmetric.rsa."
                        + "DigestSignatureSpi$SHA512",
                "1.2.840.113549.1.1.13");
        // EC$Mappings is trimmed in the same boot class path. Restore the
        // existing BC implementations, including certificate public-key OID
        // lookup. This does not add trust anchors or relax PKIX validation.
        provider.put("KeyFactory.EC", "com.android.org.bouncycastle.jcajce.provider.asymmetric.ec.KeyFactorySpi$EC");
        provider.put("Alg.Alias.KeyFactory.ECDSA", "EC");
        provider.put("Alg.Alias.KeyFactory.1.2.840.10045.2.1", "EC");
        provider.put("Alg.Alias.KeyFactory.OID.1.2.840.10045.2.1", "EC");
        provider.put("AlgorithmParameters.EC", "com.android.org.bouncycastle.jcajce.provider.asymmetric.ec.AlgorithmParametersSpi");
        provider.put("Alg.Alias.AlgorithmParameters.1.2.840.10045.2.1", "EC");
        registerEcSignature(provider, "SHA1", "ecDSA", "1.2.840.10045.4.1");
        registerEcSignature(provider, "SHA224", "ecDSA224", "1.2.840.10045.4.3.1");
        registerEcSignature(provider, "SHA256", "ecDSA256", "1.2.840.10045.4.3.2");
        registerEcSignature(provider, "SHA384", "ecDSA384", "1.2.840.10045.4.3.3");
        registerEcSignature(provider, "SHA512", "ecDSA512", "1.2.840.10045.4.3.4");
        String factory = OhTrustManagerFactorySpi.class.getName();
        provider.put("TrustManagerFactory." + ALGORITHM, factory);
        provider.put("TrustManagerFactory.PKIX", factory);
        provider.put("TrustManagerFactory.X509", factory);
        provider.put("Alg.Alias.TrustManagerFactory.X.509", "PKIX");
        provider.put("Alg.Alias.TrustManagerFactory.X509", "PKIX");
        // Android's libcore security.properties defines BKS as the default
        // keystore type.  The trimmed OH boot class path does not always load
        // that resource, in which case upstream KeyStore falls back to the
        // desktop-only JKS name.  Restore Android's default without overriding
        // an explicit platform or application choice.
        if (Security.getProperty("keystore.type") == null) {
            Security.setProperty("keystore.type", "BKS");
        }
        // NetworkSecurityConfigProvider is inserted at position 1 later in
        // ActivityThread and intentionally owns the Android name "PKIX".
        // Select a boundary-specific default name so Chromium's standard
        // TrustManagerFactory.getDefaultAlgorithm() cannot be shadowed.
        Security.setProperty("ssl.TrustManagerFactory.algorithm", ALGORITHM);
        installed = true;
        System.err.println("[WESTLAKE-TLS] TrustManagerFactory." + ALGORITHM
                + " installed via " + provider.getName());
    }

    private static void registerRsaDigestSignature(
            Provider provider, String digest, String implementation, String oid) {
        String mainName = digest + "WITHRSA";
        provider.put("Signature." + mainName, implementation);
        provider.put("Alg.Alias.Signature." + digest + "withRSA", mainName);
        provider.put("Alg.Alias.Signature." + digest + "WithRSA", mainName);
        provider.put("Alg.Alias.Signature." + digest + "WITHRSAENCRYPTION", mainName);
        provider.put("Alg.Alias.Signature." + digest + "withRSAEncryption", mainName);
        provider.put("Alg.Alias.Signature." + digest + "WithRSAEncryption", mainName);
        provider.put("Alg.Alias.Signature." + digest + "/RSA", mainName);
        provider.put("Alg.Alias.Signature." + oid, mainName);
        provider.put("Alg.Alias.Signature.OID." + oid, mainName);
    }

    private static void registerEcSignature(Provider provider, String digest, String implementation, String oid) {
        String name = digest + "WITHECDSA";
        provider.put("Signature." + name,
                "com.android.org.bouncycastle.jcajce.provider.asymmetric.ec.SignatureSpi$" + implementation);
        provider.put("Alg.Alias.Signature." + digest + "withECDSA", name);
        provider.put("Alg.Alias.Signature." + digest + "/ECDSA", name);
        provider.put("Alg.Alias.Signature." + oid, name);
        provider.put("Alg.Alias.Signature.OID." + oid, name);
    }

    private static void initializeBouncyCastleDigests() {
        // java.vendor is read-only in Android's System properties. Mutating it
        // neither selects a crypto backend nor safely initializes this class.
        // The OHOS Bouncy Castle build explicitly permits its existing Java
        // digest backend when WESTLAKE_USE_BC_DIGESTS=1 and Conscrypt is absent.
        // Without that capability selection, retain the normal fail-closed
        // initialization error; never invent an empty AndroidOpenSSL provider.
        try {
            Class.forName(
                    "com.android.org.bouncycastle.crypto.digests.AndroidDigestFactory");
        } catch (ClassNotFoundException failure) {
            throw new IllegalStateException("Bouncy Castle digest factory is unavailable", failure);
        }
    }
}
