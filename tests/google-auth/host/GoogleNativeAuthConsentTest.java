package adapter.core;

import java.util.Properties;
import java.util.Set;

public final class GoogleNativeAuthConsentTest {
    private static final String OWNER = "com.example.nativeapp", SIGNER = "a".repeat(64), LEGACY = "b".repeat(40);
    private static final String TYPE = "com.google", ACCOUNT = "native-test@example.invalid";
    private static final String PREFIX = "delegate." + OWNER + ".native.";
    private static final long NOW = 1_800_000_000_000L;
    private static int checks;

    private static Properties policy() {
        Properties p = new Properties();
        p.setProperty("delegate." + OWNER + ".sha256", SIGNER);
        p.setProperty("delegate." + OWNER + ".scopes", "scopeA,scopeB,scopeC");
        p.setProperty(PREFIX + "source", GoogleNativeAuthConsent.SOURCE);
        p.setProperty(PREFIX + "account_sha256", GoogleNativeAuthConsent.accountHash(TYPE, ACCOUNT));
        p.setProperty(PREFIX + "epoch", "0");
        p.setProperty(PREFIX + "issued_at_ms", Long.toString(NOW - 100));
        p.setProperty(PREFIX + "expires_at_ms", Long.toString(NOW + 100));
        p.setProperty(PREFIX + "scopes", "scopeA,scopeB");
        return p;
    }

    private static GoogleAuthDelegationPolicy request(String service) {
        return GoogleAuthDelegationPolicy.parse(OWNER + ":" + LEGACY + ":" + service);
    }
    private static Set<String> require(Properties p, String service, String epoch, long now) {
        return GoogleNativeAuthConsent.require(p, OWNER, SIGNER, LEGACY, TYPE, ACCOUNT, request(service), epoch, now);
    }
    private static void denied(Runnable call) {
        try { call.run(); throw new AssertionError("accepted unauthorized native request at " + checks); }
        catch (SecurityException expected) { checks++; }
    }
    private static void yes(boolean value) { if (!value) throw new AssertionError("check " + checks); checks++; }

    public static void main(String[] args) {
        Properties p = policy();
        yes(require(p, "oauth2:scopeA", "0", NOW).equals(Set.of("scopeA", "scopeB")));
        yes(require(p, "oauth2:scopeB scopeA ", "0", NOW).contains("scopeA"));
        require(p, "oauth2:scopeA", "0", NOW - 100); checks++;
        require(p, "oauth2:scopeA", "0", NOW + 99); checks++;
        denied(() -> require(p, "oauth2:scopeA", "0", NOW - 101));
        denied(() -> require(p, "oauth2:scopeA", "0", NOW + 100));
        denied(() -> require(p, "oauth2:scopeC", "0", NOW));
        denied(() -> require(p, "oauth2:scopeD", "0", NOW));
        denied(() -> require(p, "oauth2:scopeA?include_email=0", "0", NOW));
        p.setProperty("delegate." + OWNER + ".client_id", "client.apps.googleusercontent.com");
        denied(() -> require(p, "audience:server:client_id:client.apps.googleusercontent.com", "0", NOW));
        denied(() -> require(p, "oauth2:server:client_id:client.apps.googleusercontent.com:api_scope:scopeA", "0", NOW));
        denied(() -> GoogleNativeAuthConsent.require(p, "com.example.other", SIGNER, LEGACY, TYPE, ACCOUNT,
                request("oauth2:scopeA"), "0", NOW));
        denied(() -> GoogleNativeAuthConsent.require(p, OWNER, "c".repeat(64), LEGACY, TYPE, ACCOUNT,
                request("oauth2:scopeA"), "0", NOW));
        denied(() -> GoogleNativeAuthConsent.require(p, OWNER, SIGNER, "c".repeat(40), TYPE, ACCOUNT,
                request("oauth2:scopeA"), "0", NOW));
        for (String account : new String[] {"other@example.invalid", "", "a\u0000b", "a\nb", "x".repeat(321)})
            denied(() -> GoogleNativeAuthConsent.require(p, OWNER, SIGNER, LEGACY, TYPE, account,
                    request("oauth2:scopeA"), "0", NOW));
        denied(() -> GoogleNativeAuthConsent.accountHash(TYPE, null));
        denied(() -> GoogleNativeAuthConsent.accountHash("com.example", ACCOUNT));
        for (String key : new String[] {"source", "account_sha256", "epoch", "issued_at_ms", "expires_at_ms", "scopes"}) {
            Properties removed = policy(); removed.remove(PREFIX + key);
            denied(() -> require(removed, "oauth2:scopeA", "0", NOW));
        }
        for (String key : new String[] {"source", "account_sha256", "epoch"}) {
            Properties changed = policy(); changed.setProperty(PREFIX + key, "incorrect");
            denied(() -> require(changed, "oauth2:scopeA", "0", NOW));
        }
        for (String stamp : new String[] {"", "0", "-1", "+1800000000000", "01", "9999999999999999999", "1800000000000 ", "n/a"})
            for (String key : new String[] {"issued_at_ms", "expires_at_ms"}) {
                Properties bad = policy(); bad.setProperty(PREFIX + key, stamp);
                denied(() -> require(bad, "oauth2:scopeA", "0", NOW));
            }
        Properties longLife = policy();
        longLife.setProperty(PREFIX + "expires_at_ms", Long.toString(NOW - 100 + GoogleNativeAuthConsent.MAX_LIFETIME_MILLIS + 1));
        denied(() -> require(longLife, "oauth2:scopeA", "0", NOW));
        longLife.setProperty(PREFIX + "expires_at_ms", Long.toString(NOW - 100 + GoogleNativeAuthConsent.MAX_LIFETIME_MILLIS));
        require(longLife, "oauth2:scopeA", "0", NOW); checks++;
        for (String scopes : new String[] {"", "scopeA,", "scopeA,scopeA", "scopeA, scopeB", "scopeA,scope\tB", "scopeA," + "x".repeat(4096)}) {
            Properties bad = policy(); bad.setProperty(PREFIX + "scopes", scopes);
            denied(() -> require(bad, "oauth2:scopeA", "0", NOW));
        }
        String revoked = "01234567-89ab-cdef-0123-456789abcdef";
        denied(() -> require(p, "oauth2:scopeA", revoked, NOW));
        Properties reapproved = policy(); reapproved.setProperty(PREFIX + "epoch", revoked);
        require(reapproved, "oauth2:scopeA", revoked, NOW); checks++;
        denied(() -> require(reapproved, "oauth2:scopeA", "0", NOW));
        denied(() -> require(p, "oauth2:scopeA", null, NOW));
        denied(() -> require(p, "oauth2:scopeA", "", NOW));
        Set<String> approved = require(p, "oauth2:scopeA", "0", NOW);
        GoogleNativeAuthConsent.requireReturnedScopes(approved, null); checks++;
        GoogleNativeAuthConsent.requireReturnedScopes(approved, ""); checks++;
        GoogleNativeAuthConsent.requireReturnedScopes(approved, "scopeB scopeA"); checks++;
        denied(() -> GoogleNativeAuthConsent.requireReturnedScopes(approved, "scopeA scopeC"));
        denied(() -> GoogleNativeAuthConsent.requireReturnedScopes(approved, "scopeA "));
        denied(() -> GoogleNativeAuthConsent.requireReturnedScopes(approved, "scopeA  scopeB"));
        p.remove("delegate." + OWNER + ".sha256");
        denied(() -> require(p, "oauth2:scopeA", "0", NOW));
        System.out.println("GOOGLE_NATIVE_AUTH_CONSENT_PASS " + checks);
    }
}
