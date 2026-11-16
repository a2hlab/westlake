package adapter.core;

import java.util.Properties;

public final class GoogleIdentityClaimsTest {
    private static int passed;
    private static final String HOST = "com.google.android.gms";
    private static final String ACTIVITY = "com.google.android.gms.auth.api.credentials.assistedsignin.ui.GoogleSignInActivity";
    private static final String APP = "com.example.client", SHA1 = "a".repeat(40), SHA256 = "b".repeat(64);
    private static final String CLIENT = "123-test.apps.googleusercontent.com";
    private static final String PREFIX = APP + ":" + SHA1 + ":";
    private static void check(boolean value) {
        if (!value) throw new AssertionError("identity request boundary failed");
        passed++;
    }
    private static void denied(Runnable action) {
        try { action.run(); throw new AssertionError("unauthorized claims accepted"); }
        catch (SecurityException expected) { passed++; }
    }
    public static void main(String[] args) {
        String key = PREFIX + "audience:server:client_id:" + CLIENT + "?include_email=0&include_profile=0";
        check(key.equals(GoogleIdentityClaims.request(key, false, HOST, ACTIVITY)));
        check(key.equals(GoogleIdentityClaims.request(key, true, APP, ACTIVITY)));
        check(key.equals(GoogleIdentityClaims.request(key, true, HOST, "example.OtherActivity")));
        for (String unchanged : new String[] {PREFIX + "oauth2:openid",
                PREFIX + "oauth2:server:client_id:" + CLIENT + ":api_scope:openid"})
            check(unchanged.equals(GoogleIdentityClaims.request(unchanged, true, HOST, ACTIVITY)));
        String adapted = GoogleIdentityClaims.request(key, true, HOST, ACTIVITY);
        GoogleAuthDelegationPolicy parsed = GoogleAuthDelegationPolicy.parse(adapted);
        check(parsed.owner.equals(APP) && parsed.signer.equals(SHA1) && parsed.clientId.equals(CLIENT));
        check(parsed.options.get("include_email").equals("1") && parsed.options.get("include_profile").equals("1"));
        check(adapted.equals(GoogleIdentityClaims.request(adapted, true, HOST, ACTIVITY)));
        String opaque = GoogleIdentityClaims.request(key + "&token_request_options=nonce%2Bvalue%3D", true, HOST, ACTIVITY);
        check(GoogleAuthDelegationPolicy.parse(opaque).options.get("token_request_options").equals("nonce+value="));
        Properties grants = new Properties();
        String prefix = "delegate." + APP + ".";
        grants.setProperty(prefix + "sha256", SHA256);
        grants.setProperty(prefix + "client_id", CLIENT);
        grants.setProperty(prefix + "scopes", "openid,email,profile");
        parsed.require(grants, APP, SHA256, SHA1); passed++;
        denied(() -> parsed.require(grants, "com.example.other", SHA256, SHA1));
        denied(() -> parsed.require(grants, APP, "c".repeat(64), SHA1));
        denied(() -> parsed.require(grants, APP, SHA256, "d".repeat(40)));
        denied(() -> GoogleAuthDelegationPolicy.parse(opaque).require(grants, APP, SHA256, SHA1));
        grants.setProperty(prefix + "scopes", "openid,profile");
        denied(() -> parsed.require(grants, APP, SHA256, SHA1));
        grants.setProperty(prefix + "scopes", "openid,email");
        denied(() -> parsed.require(grants, APP, SHA256, SHA1));
        grants.setProperty(prefix + "scopes", "openid,email,profile");
        grants.setProperty(prefix + "client_id", "456-other.apps.googleusercontent.com");
        denied(() -> parsed.require(grants, APP, SHA256, SHA1));
        System.out.println("GOOGLE_IDENTITY_CLAIMS_PASS " + passed);
    }
}
