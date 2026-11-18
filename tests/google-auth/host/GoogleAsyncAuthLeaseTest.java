package adapter.core;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public final class GoogleAsyncAuthLeaseTest {
    private static int checks;
    private static final String OWNER = "test.owner", SIGNER = "a".repeat(64), SHA1 = "b".repeat(40);
    private static final String ACCOUNT = "test@example.invalid";
    private static GoogleAuthDelegationPolicy key(String owner, String scopes) {
        return GoogleAuthDelegationPolicy.parse(owner + ":" + SHA1 + ":oauth2:" + scopes);
    }
    private static void require(boolean value) { checks++; if (!value) throw new AssertionError("check " + checks); }
    private static void grant(Path d, String owner) throws Exception {
        GoogleAuthSessions.grantOAuth(d, owner, SIGNER, SHA1, "com.google", ACCOUNT,
                List.of(owner + ":" + SHA1 + ":oauth2:email openid profile"), GoogleAuthSessions.generation(d, owner));
    }
    private static GoogleAsyncAuthLease lease(Path d, String owner, String account) throws Exception {
        return new GoogleAsyncAuthLease(d, owner, SIGNER, SHA1, account, "oauth2:email openid", 1000);
    }
    public static void main(String[] args) throws Exception {
        Path d = Files.createTempDirectory("async-auth-lease-");
        GoogleAsyncAuthLease a = lease(d, OWNER, null);
        require(!a.allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, true));
        grant(d, OWNER);
        require(a.allows("com.google", ACCOUNT, key(OWNER, "email openid"), 1001, true));
        require(a.allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, true));
        require(!a.allows("com.google", ACCOUNT, key(OWNER, "profile"), 1001, true)); // saved grant alone is insufficient
        require(!a.allows("com.google", ACCOUNT, key("test.other", "email"), 1001, true));
        require(!a.allows("com.google", ACCOUNT, GoogleAuthDelegationPolicy.parse(OWNER+":"+"c".repeat(40)+":oauth2:email"), 1001, true));
        require(!a.allows("com.google", "different@example.invalid", key(OWNER, "email"), 1001, true));
        require(!a.allows("wrong.type", ACCOUNT, key(OWNER, "email"), 1001, true));
        require(!a.allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, false));
        require(!a.allows("com.google", ACCOUNT, key(OWNER, "email"), 101000, true));
        require(a.allows("com.google", ACCOUNT, key(OWNER, "email"), 100999, true));
        require(!lease(d, OWNER, "different@example.invalid").allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, true));
        GoogleAuthSessions.revoke(d, OWNER);
        require(!a.allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, true));
        grant(d, OWNER);
        require(!a.allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, true)); // old operation cannot adopt new epoch
        a = lease(d, OWNER, null);
        require(a.allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, true));
        a.close();
        require(!a.allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, true));
        grant(d, "test.other");
        GoogleAsyncAuthLease b = lease(d, "test.other", null);
        require(b.allows("com.google", ACCOUNT, key("test.other", "email"), 1001, true));
        require(!b.allows("com.google", ACCOUNT, key(OWNER, "email"), 1001, true));
        require(!b.allows("com.google", ACCOUNT, GoogleAuthDelegationPolicy.parse("test.other:"+SHA1+":oauth2:email?include_email=1"), 1001, true));
        System.out.println("GOOGLE_ASYNC_AUTH_LEASE_PASS " + checks);
    }
}
