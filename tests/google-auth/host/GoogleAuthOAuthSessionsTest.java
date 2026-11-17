package adapter.core;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.List;

/** OAuth selection is scoped to the verified caller, account, completed UI and revocation epoch. */
public final class GoogleAuthOAuthSessionsTest {
    private static int checks;
    private static final String OWNER = "org.example.client", SIGNER = "a".repeat(64), LEGACY = "b".repeat(40);
    private static final String ACCOUNT = "private-account@example.invalid", TYPE = "com.google";
    private static final String PREFIX = OWNER + ":" + LEGACY + ":";
    private static final String MODERN = "com.google.android.gms.auth.api.credentials.assistedsignin.ui.GoogleSignInActivity";
    private static final String LEGACY_UI = "com.google.android.gms.auth.api.signin.ui.SignInActivity";
    private interface Checked { void run() throws Exception; }
    private static void require(boolean value) { checks++; if (!value) throw new AssertionError("check " + checks); }
    private static void denied(Checked action) throws Exception {
        try { action.run(); } catch (SecurityException | java.io.IOException expected) { checks++; return; }
        throw new AssertionError("expected denial");
    }
    private static GoogleAuthDelegationPolicy request(String service) { return GoogleAuthDelegationPolicy.parse(PREFIX + service); }
    private static boolean allows(Path root, String service, String epoch) throws Exception {
        return GoogleAuthSessions.allows(root, OWNER, SIGNER, LEGACY, TYPE, ACCOUNT, request(service), epoch);
    }
    private static void grant(Path root, List<String> services, String epoch) throws Exception {
        GoogleAuthSessions.grantOAuth(root, OWNER, SIGNER, LEGACY, TYPE, ACCOUNT,
                services.stream().map(s -> PREFIX + s).toList(), epoch);
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("wl-oauth-session-").resolve("private");
        String epoch = GoogleAuthSessions.generation(root, OWNER);
        require(!allows(root, "oauth2:openid email", epoch));
        require(GoogleAuthSessions.sameService(request("oauth2:openid email"), request("oauth2:email openid")));
        require(!GoogleAuthSessions.sameService(request("oauth2:email"), request("oauth2:email profile")));
        require(!GoogleAuthSessions.sameService(request("oauth2:email"), request("oauth2:https://www.googleapis.com/auth/userinfo.email")));
        require(!GoogleAuthSessions.sameService(request("oauth2:email"), request("audience:server:client_id:test.apps.googleusercontent.com")));
        grant(root, List.of("oauth2:openid email?include_email=1", "oauth2:profile https://www.googleapis.com/auth/user.birthday.read"), epoch);
        require(allows(root, "oauth2:profile email openid", epoch));
        require(allows(root, "oauth2:email?include_email=1", epoch));
        require(!allows(root, "oauth2:email?include_profile=1", epoch));
        require(!allows(root, "oauth2:email contacts", epoch));
        require(!allows(root, "oauth2:https://www.googleapis.com/auth/userinfo.email", epoch));
        require(!allows(root, "audience:server:client_id:test.apps.googleusercontent.com", epoch));
        require(!GoogleAuthSessions.allows(root, "org.example.other", SIGNER, LEGACY, TYPE, ACCOUNT, request("oauth2:email"), epoch));
        require(!GoogleAuthSessions.allows(root, OWNER, "c".repeat(64), LEGACY, TYPE, ACCOUNT, request("oauth2:email"), epoch));
        require(!GoogleAuthSessions.allows(root, OWNER, SIGNER, "d".repeat(40), TYPE, ACCOUNT, request("oauth2:email"), epoch));
        require(!GoogleAuthSessions.allows(root, OWNER, SIGNER, LEGACY, TYPE, "other@example.invalid", request("oauth2:email"), epoch));
        require(!GoogleAuthSessions.allows(root, OWNER, SIGNER, LEGACY, "com.other", ACCOUNT, request("oauth2:email"), epoch));
        denied(() -> grant(root, List.of(), epoch));
        denied(() -> grant(root, List.of("audience:server:client_id:test.apps.googleusercontent.com"), epoch));
        denied(() -> grant(root, List.of("oauth2:server:client_id:test.apps.googleusercontent.com:api_scope:email"), epoch));
        denied(() -> grant(root, List.of("oauth2:email?token_request_options=nonce"), epoch));
        denied(() -> grant(root, List.of("oauth2:email\nprofile"), epoch));
        denied(() -> GoogleAuthSessions.grantOAuth(root, OWNER, SIGNER, LEGACY, TYPE, ACCOUNT,
                List.of("org.example.other:" + LEGACY + ":oauth2:email"), epoch));
        denied(() -> GoogleAuthSessions.grantOAuth(root, OWNER, SIGNER, LEGACY, TYPE, ACCOUNT,
                List.of(OWNER + ":" + "c".repeat(40) + ":oauth2:email"), epoch));
        denied(() -> allows(root, "oauth2:email?token_request_options=nonce", epoch));
        denied(() -> allows(root, "oauth2:server:client_id:test.apps.googleusercontent.com:api_scope:email", epoch));
        Path file;
        try (var files = Files.list(root)) { file = files.filter(p -> p.getFileName().toString().startsWith("selection-")).findFirst().orElseThrow(); }
        require(!Files.readString(file).contains(ACCOUNT));
        require(Files.getPosixFilePermissions(file).equals(PosixFilePermissions.fromString("rw-------")));
        require(allows(root, "oauth2:email", GoogleAuthSessions.generation(root, OWNER)));
        // A new interactive selection replaces the previous scopes, rather than accumulating old grants.
        grant(root, List.of("oauth2:email"), epoch);
        require(allows(root, "oauth2:email", epoch));
        require(!allows(root, "oauth2:email openid", epoch));
        require(!allows(root, "oauth2:email?include_email=1", epoch));
        GoogleAuthSessions.revoke(root, OWNER);
        String next = GoogleAuthSessions.generation(root, OWNER);
        require(!allows(root, "oauth2:email", epoch));
        require(!allows(root, "oauth2:email", next));
        denied(() -> grant(root, List.of("oauth2:email"), epoch));
        grant(root, List.of("oauth2:email"), next);
        require(allows(root, "oauth2:email", next));
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-r--r--"));
        denied(() -> allows(root, "oauth2:email", next));
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-------"));
        Path original = file.resolveSibling("original"); Files.move(file, original); Files.createSymbolicLink(file, original);
        denied(() -> allows(root, "oauth2:email", next));
        require(GoogleAuthSessions.selectionActivity("com.google.android.gms", LEGACY_UI, true));
        require(GoogleAuthSessions.selectionActivity("com.google.android.gms", MODERN, false));
        require(!GoogleAuthSessions.selectionActivity("com.google.android.gms", MODERN, true));
        require(!GoogleAuthSessions.selectionActivity("com.google.android.gms", LEGACY_UI, false));
        require(!GoogleAuthSessions.selectionActivity("org.example.other", LEGACY_UI, true));
        require(!GoogleAuthSessions.selectionActivity("com.google.android.gms", LEGACY_UI + "Extra", true));
        require(!GoogleAuthSessions.selectionActivity("com.google.android.gms", "com.google.android.gms.signin.activity.SignInActivity", true));
        System.out.println("GOOGLE_AUTH_OAUTH_SESSIONS_PASS " + checks);
    }
}
