package adapter.core;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;

/** Authorization boundary cases, including revocation racing with an older successful picker. */
public final class GoogleAuthSessionsTest {
    private static int checks;
    private static final String OWNER = "org.example.client", SIGNER = "a".repeat(64), LEGACY = "b".repeat(40);
    private static final String SERVICE = "audience:server:client_id:test.apps.googleusercontent.com";
    private static final String ACCOUNT = "test-account@example.invalid", TYPE = "com.google";
    private static final String PREFIX = OWNER + ":" + LEGACY + ":";
    private interface Checked { void run() throws Exception; }
    private static void require(boolean value) { checks++; if (!value) throw new AssertionError("check " + checks); }
    private static void denied(Checked action) throws Exception {
        try { action.run(); } catch (SecurityException | java.io.IOException expected) { checks++; return; }
        throw new AssertionError("expected denial");
    }
    private static GoogleAuthDelegationPolicy request(String suffix) { return GoogleAuthDelegationPolicy.parse(PREFIX + suffix); }
    private static boolean allows(Path p, String owner, String signer, String legacy, String account,
            GoogleAuthDelegationPolicy request, String epoch) throws Exception {
        return GoogleAuthSessions.allows(p, owner, signer, legacy, TYPE, account, request, epoch);
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("wl-auth-sessions-").resolve("private");
        String epoch = GoogleAuthSessions.generation(root, OWNER);
        GoogleAuthDelegationPolicy silent = request(SERVICE + "?include_email=1");
        require(!allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, silent, epoch));
        String approved = PREFIX + SERVICE + "?include_email=1&include_profile=1";
        GoogleAuthSessions.grant(root, OWNER, SIGNER, LEGACY, TYPE, ACCOUNT, approved, epoch);
        require(allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, silent, epoch));
        require(allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, request(SERVICE), epoch));
        require(!allows(root, "org.example.attacker", SIGNER, LEGACY, ACCOUNT, silent, epoch));
        require(!allows(root, OWNER, "c".repeat(64), LEGACY, ACCOUNT, silent, epoch));
        require(!allows(root, OWNER, SIGNER, "d".repeat(40), ACCOUNT, silent, epoch));
        require(!allows(root, OWNER, SIGNER, LEGACY, "other@example.invalid", silent, epoch));
        require(!allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, request(SERVICE.replace("test.apps", "other.apps")), epoch));
        require(!GoogleAuthSessions.allows(root, OWNER, SIGNER, LEGACY, "com.other", ACCOUNT, silent, epoch));
        denied(() -> allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, request("oauth2:email"), epoch));
        denied(() -> allows(root, OWNER, SIGNER, LEGACY, ACCOUNT,
                request("oauth2:server:client_id:test.apps.googleusercontent.com:api_scope:email"), epoch));
        denied(() -> allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, request(SERVICE + "?token_request_options=nonce"), epoch));
        Path file;
        try (java.util.stream.Stream<Path> files = Files.list(root)) {
            file = files.filter(p -> p.getFileName().toString().startsWith("selection-")).findFirst().orElseThrow();
        }
        require(!Files.readString(file).contains(ACCOUNT));
        require(Files.getPosixFilePermissions(file).equals(PosixFilePermissions.fromString("rw-------")));
        // A separate store reader after restart sees the same persisted selection.
        require(allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, silent, GoogleAuthSessions.generation(root, OWNER)));
        GoogleAuthSessions.revoke(root, OWNER);
        require(!allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, silent, epoch));
        String next = GoogleAuthSessions.generation(root, OWNER);
        require(!allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, silent, next));
        denied(() -> GoogleAuthSessions.grant(root, OWNER, SIGNER, LEGACY, TYPE, ACCOUNT, approved, epoch));
        // A new selection may authorize fewer claims; silent reads must not enlarge them.
        GoogleAuthSessions.grant(root, OWNER, SIGNER, LEGACY, TYPE, ACCOUNT, PREFIX + SERVICE, next);
        require(!allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, silent, next));
        require(allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, request(SERVICE), next));
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-r--r--"));
        denied(() -> allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, request(SERVICE), next));
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-------"));
        Path moved = file.resolveSibling("original"); Files.move(file, moved); Files.createSymbolicLink(file, moved);
        denied(() -> allows(root, OWNER, SIGNER, LEGACY, ACCOUNT, request(SERVICE), next));
        Path epochFile;
        try (java.util.stream.Stream<Path> files = Files.list(root)) {
            epochFile = files.filter(p -> p.getFileName().toString().startsWith("epoch-")).findFirst().orElseThrow();
        }
        Files.writeString(epochFile, "");
        denied(() -> GoogleAuthSessions.generation(root, OWNER));
        System.out.println("GOOGLE_AUTH_SESSIONS_PASS " + checks);
    }
}
