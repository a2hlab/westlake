package adapter.core;

public final class GoogleAuthorizationLeasesTest {
    private static int checks;
    private static void ok(boolean condition) {
        checks++;
        if (!condition) throw new AssertionError("check " + checks);
    }
    private static void eq(Object expected, Object actual) { ok(java.util.Objects.equals(expected, actual)); }
    private static void rejects(Runnable action) {
        try { action.run(); } catch (SecurityException expected) { checks++; return; }
        throw new AssertionError("check " + (checks + 1) + " accepted");
    }

    public static void main(String[] args) {
        final boolean[] alive = {true};
        GoogleAuthorizationLeases.Liveness liveness = () -> alive[0];
        GoogleAuthorizationLeases table = new GoogleAuthorizationLeases();

        // Nothing live: no signer.
        eq(null, table.liveSigner("app.a", 0));
        // A live request exposes its own signer, to its own owner only.
        GoogleAuthorizationLeases.Lease a = table.open("app.a", "aa", 1000, liveness);
        eq("aa", table.liveSigner("app.a", 1000));
        eq(null, table.liveSigner("app.b", 1000));
        eq(null, table.liveSigner(null, 1000));
        // It ends at its callback (close) ...
        table.close(a);
        eq(null, table.liveSigner("app.a", 1001));
        // ... at its deadline ...
        table.open("app.a", "aa", 0, liveness);
        eq("aa", table.liveSigner("app.a", GoogleAuthorizationLeases.LIFETIME_MS - 1));
        eq(null, table.liveSigner("app.a", GoogleAuthorizationLeases.LIFETIME_MS));
        // ... or at its caller's death.
        table.open("app.a", "aa", 5000, liveness);
        alive[0] = false;
        eq(null, table.liveSigner("app.a", 5001));
        alive[0] = true;
        // A liveness check that throws counts as dead.
        table.open("app.c", "cc", 6000, () -> { throw new IllegalStateException("gone"); });
        eq(null, table.liveSigner("app.c", 6001));
        // Live requests that disagree on the signer expose nothing.
        GoogleAuthorizationLeases fresh = new GoogleAuthorizationLeases();
        fresh.open("app.d", "d1", 0, liveness);
        fresh.open("app.d", "d2", 0, liveness);
        eq(null, fresh.liveSigner("app.d", 1));
        // Invalid owners are refused.
        rejects(() -> fresh.open(null, "x", 0, liveness));
        rejects(() -> fresh.open("", "x", 0, liveness));
        rejects(() -> fresh.open("app.e", "", 0, liveness));
        rejects(() -> fresh.open("app.e", "x", 0, null));
        // At most MAX_LIVE live requests; expired ones make room again.
        GoogleAuthorizationLeases full = new GoogleAuthorizationLeases();
        for (int i = 0; i < GoogleAuthorizationLeases.MAX_LIVE; i++) full.open("app." + i, "s", 0, liveness);
        rejects(() -> full.open("app.x", "s", 1, liveness));
        full.open("app.x", "s", GoogleAuthorizationLeases.LIFETIME_MS, liveness);
        eq(1, full.size());
        eq("s", full.liveSigner("app.x", GoogleAuthorizationLeases.LIFETIME_MS + 1));
        System.out.println("GOOGLE_AUTHORIZATION_LEASES_PASS " + checks);
    }
}
