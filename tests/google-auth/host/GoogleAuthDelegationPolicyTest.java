package adapter.core;

import java.util.Properties;

/** Cache-key text must never expand an independently verified identity's grant. */
public final class GoogleAuthDelegationPolicyTest {
    private static int passed;
    private static final String APP = "com.spotify.music", SHA1 = "a".repeat(40), SHA256 = "b".repeat(64);
    private static final String CLIENT = "123-test.apps.googleusercontent.com";
    private static final String PREFIX = APP + ":" + SHA1 + ":";
    private static final Properties POLICY = new Properties();
    private static void allow(String key) {
        GoogleAuthDelegationPolicy.parse(key).require(POLICY, APP, SHA256, SHA1); passed++;
    }
    private static void deny(Runnable action) {
        try { action.run(); throw new AssertionError("unauthorized request accepted"); }
        catch (SecurityException expected) { passed++; }
    }
    private static void denyKey(String key) { deny(() -> allow(key)); }
    public static void main(String[] args) {
        String grant = "delegate." + APP + ".";
        POLICY.setProperty(grant + "sha256", SHA256);
        POLICY.setProperty(grant + "scopes", "openid,email,profile,https://www.googleapis.com/auth/userinfo.email");
        POLICY.setProperty(grant + "client_id", CLIENT);
        POLICY.setProperty("delegation.broker.sha256", "c".repeat(64));
        allow(PREFIX + "oauth2:openid email profile");
        allow(PREFIX + "oauth2:openid email profile ");
        allow(PREFIX + "oauth2: email");
        allow(PREFIX + "oauth2: openid email profile ");
        GoogleAuthDelegationPolicy leading = GoogleAuthDelegationPolicy.parse(PREFIX + "oauth2: email profile");
        if (!leading.service.equals("oauth2: email profile") || !leading.scopes.equals(java.util.Set.of("email", "profile")))
            throw new AssertionError("leading delimiter changed service or scope set");
        passed++;
        GoogleAuthDelegationPolicy trailing = GoogleAuthDelegationPolicy.parse(PREFIX + "oauth2:email ");
        if (!trailing.service.equals("oauth2:email ") || !trailing.scopes.equals(java.util.Set.of("email")))
            throw new AssertionError("service or scope set changed");
        passed++;
        allow(PREFIX + "oauth2:server:client_id:" + CLIENT + ":api_scope:openid email?include_email=1&include_profile=0");
        allow(PREFIX + "audience:server:client_id:" + CLIENT);
        allow(PREFIX + "oauth2:https://www.googleapis.com/auth/userinfo.email");
        GoogleAuthDelegationPolicy.requireBroker(POLICY, "com.google.android.gms", "c".repeat(64)); passed++;
        deny(() -> GoogleAuthDelegationPolicy.requireBroker(POLICY, APP, "c".repeat(64)));
        deny(() -> GoogleAuthDelegationPolicy.requireBroker(POLICY, "com.google.android.gms", SHA256));
        deny(() -> GoogleAuthDelegationPolicy.parse(PREFIX + "oauth2:email").require(POLICY, "com.evil.app", SHA256, SHA1));
        deny(() -> GoogleAuthDelegationPolicy.parse(PREFIX + "oauth2:email").require(POLICY, APP, "d".repeat(64), SHA1));
        deny(() -> GoogleAuthDelegationPolicy.parse(PREFIX + "oauth2:email").require(POLICY, APP, SHA256, "e".repeat(40)));
        for (String key : new String[] {
                "hub:" + PREFIX + "oauth2:email", PREFIX + "ac2dm", PREFIX + "oauth2:",
                PREFIX + "oauth2:email email", PREFIX + "oauth2:email  profile", PREFIX + "oauth2:email\nprofile",
                PREFIX + "oauth2:email  ", PREFIX + "oauth2:  email", PREFIX + "oauth2: ",
                PREFIX + "oauth2:  ", PREFIX + "oauth2: email  ", PREFIX + "oauth2: email  profile",
                PREFIX + "oauth2: email email", PREFIX + "oauth2: email drive",
                PREFIX + "oauth2:\temail", PREFIX + "oauth2:\nemail", PREFIX + "oauth2:\remail",
                PREFIX + "oauth2:\u00a0email", PREFIX + "oauth2: email\t",
                PREFIX + "oauth2:server:client_id:" + CLIENT + ":api_scope: email",
                PREFIX + "oauth2:email\t", PREFIX + "oauth2:email\r", PREFIX + "oauth2:email\u00a0",
                PREFIX + "oauth2:email drive ",
                PREFIX + "oauth2:server:client_id:" + CLIENT + ":api_scope:email ",
                PREFIX + "oauth2:email drive", PREFIX + "oauth2:server:client_id:wrong.apps.googleusercontent.com:api_scope:email",
                PREFIX + "audience:server:client_id:wrong.apps.googleusercontent.com",
                PREFIX + "oauth2:server:client_id:" + CLIENT, PREFIX + "oauth2:email?delegatee_user_id=1",
                PREFIX + "oauth2:email?include_email=2", PREFIX + "oauth2:email?include_email=1&include_email=0",
                PREFIX + "oauth2:email?include_email=1&include%5femail=0", PREFIX + "oauth2:email?include_email=%Q0",
                PREFIX + "oauth2:email?include_email=1?include_profile=1", PREFIX + "oauth2:email?token_request_options=unapproved",
                PREFIX + "oauth2:email?include_email", "x".repeat(4097)}) denyKey(key);
        POLICY.setProperty(grant + "token_request_options", "opaque+value=");
        allow(PREFIX + "oauth2:email?token_request_options=opaque%2Bvalue%3D");
        denyKey(PREFIX + "oauth2:server:client_id:" + CLIENT + ":api_scope:");
        GoogleAuthDelegationPolicy parsed = GoogleAuthDelegationPolicy.parse(PREFIX + "oauth2:email");
        parsed.requireGrantedScopes(POLICY, "email https://www.googleapis.com/auth/userinfo.email"); passed++;
        deny(() -> parsed.requireGrantedScopes(POLICY, "email drive"));
        POLICY.setProperty(grant + "scopes", "openid");
        denyKey(PREFIX + "oauth2:openid?include_email=1");
        denyKey(PREFIX + "oauth2:openid?include_profile=1");
        // The same bounded parser handles Gmail-sized requests; size acceptance is not a grant.
        String wideScope = "https://scope.example.invalid/" + "x".repeat(600);
        String wideService = "oauth2:" + wideScope + " ";
        denyKey(PREFIX + wideService);
        POLICY.setProperty(grant + "scopes", wideScope);
        allow(PREFIX + wideService);
        String limitScope = "x".repeat(GoogleAuthDelegationPolicy.MAX_KEY_LENGTH - PREFIX.length() - 7);
        POLICY.setProperty(grant + "scopes", limitScope);
        allow(PREFIX + "oauth2:" + limitScope);
        denyKey(PREFIX + "oauth2:" + limitScope + " ");
        POLICY.remove(grant + "sha256");
        denyKey(PREFIX + "oauth2:email");
        System.out.println("GOOGLE_AUTH_DELEGATION_POLICY_PASS " + passed);
    }
}
