/* OpenHarmony TrustManagerFactory bridge for the Android compatibility runtime. */
package adapter.security;

import java.io.IOException;
import java.security.GeneralSecurityException;
import java.security.InvalidAlgorithmParameterException;
import java.security.KeyStore;
import java.security.KeyStoreException;

import javax.net.ssl.ManagerFactoryParameters;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactorySpi;

/** Creates a PKIX trust manager backed by Android or OpenHarmony system roots. */
public final class OhTrustManagerFactorySpi extends TrustManagerFactorySpi {
    private static volatile TrustManager platformManager;
    private TrustManager[] managers;

    @Override
    protected void engineInit(KeyStore keyStore) throws KeyStoreException {
        try {
            TrustManager manager;
            if (keyStore == null) {
                manager = platformManager;
                if (manager == null) {
                    synchronized (OhTrustManagerFactorySpi.class) {
                        manager = platformManager;
                        if (manager == null) {
                            manager = new OhSystemTrustManager(null);
                            platformManager = manager;
                        }
                    }
                }
            } else {
                manager = new OhSystemTrustManager(keyStore);
            }
            managers = new TrustManager[] { manager };
        } catch (GeneralSecurityException | IOException e) {
            throw new KeyStoreException("Unable to initialize platform trust manager", e);
        }
    }

    @Override
    protected void engineInit(ManagerFactoryParameters parameters)
            throws InvalidAlgorithmParameterException {
        throw new InvalidAlgorithmParameterException(
                "ManagerFactoryParameters are not supported by the OH trust bridge");
    }

    @Override
    protected TrustManager[] engineGetTrustManagers() {
        if (managers == null) {
            throw new IllegalStateException("TrustManagerFactory is not initialized");
        }
        return managers.clone();
    }
}
