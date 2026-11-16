package adapter.core;

import java.util.Properties;

/** Authorization checks are independent of an app's mutable claimed caller fields. */
public final class GoogleAuthPolicyTest {
    private static int passed;
    private static void deny(Runnable test) {
        try { test.run(); throw new AssertionError("unauthorized request accepted"); }
        catch (SecurityException expected) { passed++; }
    }
    public static void main(String[] args) {
        Properties grants = new Properties();
        String packageName = "org.westlake.googlebootstrap.probe";
        String certificate = "a".repeat(64);
        grants.setProperty("client." + packageName + ".sha256", certificate);
        grants.setProperty("client." + packageName + ".scopes", "ac2dm,oauth2:test");
        GoogleAuthPolicy.require(grants, packageName, certificate, "ac2dm"); passed++;
        GoogleAuthPolicy.require(grants, packageName, certificate, "oauth2:test"); passed++;
        deny(() -> GoogleAuthPolicy.require(grants, "com.google.android.gms", certificate, "ac2dm"));
        deny(() -> GoogleAuthPolicy.require(grants, packageName, "b".repeat(64), "ac2dm"));
        deny(() -> GoogleAuthPolicy.require(grants, packageName, certificate, "androidmarket"));
        deny(() -> GoogleAuthPolicy.require(grants, packageName, certificate, "oauth2:test other"));
        deny(() -> GoogleAuthPolicy.require(grants, packageName, certificate, "AC2DM"));
        grants.remove("client." + packageName + ".sha256");
        deny(() -> GoogleAuthPolicy.require(grants, packageName, certificate, "ac2dm"));
        System.out.println("GOOGLE_AUTH_POLICY_PASS " + passed);
    }
}
