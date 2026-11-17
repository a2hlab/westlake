package adapter.core;

import java.util.Arrays;
import java.util.Properties;

/** Explicit experimental grants. Package and certificate come from the verified launch, never a request. */
public final class GoogleAuthPolicy {
    private GoogleAuthPolicy() {}

    /** One account attribute for the pinned broker; this grants no OAuth scope or credential read. */
    public static void requireAccountId(Properties grants, String packageName, String certificate) {
        require(grants, packageName, certificate, null);
        if (!"com.google.android.gms".equals(packageName)
                || !certificate.equals(grants.getProperty("delegation.broker.sha256"))
                || !"true".equals(grants.getProperty("client." + packageName + ".account_id")))
            throw new SecurityException("account ID metadata not granted");
    }

    public static void require(Properties grants, String packageName, String certificate, String scope) {
        if (packageName == null || !packageName.matches("[A-Za-z0-9_]+(?:\\.[A-Za-z0-9_]+)+")
                || certificate == null || !certificate.matches("[0-9a-f]{64}")) {
            throw new SecurityException("invalid verified identity");
        }
        String prefix = "client." + packageName + ".";
        if (!certificate.equals(grants.getProperty(prefix + "sha256"))) {
            throw new SecurityException("client not granted");
        }
        if (scope != null && !Arrays.asList(grants.getProperty(prefix + "scopes", "").split(",", -1)).contains(scope)) {
            throw new SecurityException("scope not granted");
        }
    }
}
