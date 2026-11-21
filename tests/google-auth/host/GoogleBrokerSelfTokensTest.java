package adapter.core;

public final class GoogleBrokerSelfTokensTest {
    private static int checks;
    private static void ok(boolean condition) {
        checks++;
        if (!condition) throw new AssertionError("check " + checks);
    }
    private static void eq(Object expected, Object actual) { ok(java.util.Objects.equals(expected, actual)); }

    public static void main(String[] args) {
        String gms = "38918a453d07199354f8b19af05ec6562ced5788";
        String other = "0000000000000000000000000000000000000000";
        String lams = "oauth2:https://www.googleapis.com/auth/lams";
        String state = "oauth2:https://www.googleapis.com/auth/userinfo.email https://www.googleapis.com/auth/account.capabilities"
                + " https://www.googleapis.com/auth/account.service_flags";
        // The two approved requests, under Play services' own signer, map to the broker's exact grant entries.
        eq(lams, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms + ":" + lams, gms));
        eq(state, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms + ":" + state, gms));
        // Scope order and the signer's case do not matter; the canonical request is what the broker grants.
        eq(state, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms.toUpperCase()
                + ":oauth2:https://www.googleapis.com/auth/account.service_flags https://www.googleapis.com/auth/userinfo.email"
                + " https://www.googleapis.com/auth/account.capabilities", gms));
        // Another package, another signer, more or fewer scopes, options, client ids: never.
        eq(null, GoogleBrokerSelfTokens.allowedService("com.naver.linewebtoon:" + gms + ":" + lams, gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + other + ":" + lams, gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms + ":" + lams
                + " https://www.googleapis.com/auth/drive", gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms
                + ":oauth2:https://www.googleapis.com/auth/userinfo.email", gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms + ":" + lams + "?include_email=1", gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms
                + ":audience:server:client_id:1-a.apps.googleusercontent.com", gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms
                + ":oauth2:server:client_id:1-a.apps.googleusercontent.com:api_scope:https://www.googleapis.com/auth/lams", gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms + ":ac2dm", gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("garbage", gms));
        eq(null, GoogleBrokerSelfTokens.allowedService(null, gms));
        eq(null, GoogleBrokerSelfTokens.allowedService("com.google.android.gms:" + gms + ":" + lams, null));
        eq("lams", GoogleBrokerSelfTokens.label(lams));
        eq("account_state", GoogleBrokerSelfTokens.label(state));

        // The cache answers a token, its expiry and scopes until the token's lifetime ends.
        GoogleBrokerSelfTokens cache = new GoogleBrokerSelfTokens();
        eq(null, cache.cachedToken("a", lams, 0));
        cache.put("a", lams, "t1", 100);
        eq("t1", cache.cachedToken("a", lams, 100));
        eq(Long.toString(100 + GoogleBrokerSelfTokens.LIFETIME_S), cache.expiry("a", lams, 101));
        eq("https://www.googleapis.com/auth/lams", cache.scopes("a", lams, 101));
        eq(null, cache.cachedToken("b", lams, 101));
        eq(null, cache.cachedToken("a", state, 101));
        eq(null, cache.cachedToken("a", lams, 100 + GoogleBrokerSelfTokens.LIFETIME_S));
        eq(null, cache.expiry("a", lams, 100 + GoogleBrokerSelfTokens.LIFETIME_S));
        // Invalidation removes the token at once.
        cache.put("a", state, "t2", 200);
        ok(cache.invalidate("t2"));
        eq(null, cache.cachedToken("a", state, 201));
        ok(!cache.invalidate("t2"));
        ok(!cache.invalidate(null));
        // Empty tokens are not held, and the cache stays bounded.
        cache.put("a", lams, "", 300);
        eq(null, cache.cachedToken("a", lams, 300));
        for (int i = 0; i < 20; i++) cache.put("acct" + i, lams, "t" + i, 400);
        eq(GoogleBrokerSelfTokens.MAX_CACHED, cache.size());
        eq("t19", cache.cachedToken("acct19", lams, 401));
        eq(null, cache.cachedToken("acct0", lams, 401));
        System.out.println("GOOGLE_BROKER_SELF_TOKENS_PASS " + checks);
    }
}
