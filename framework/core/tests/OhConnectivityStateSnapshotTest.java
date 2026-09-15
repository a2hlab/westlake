package adapter.core;

/** Host-only checks for the native network snapshot contract. */
public final class OhConnectivityStateSnapshotTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static String wire(int backend, int netId, int defaultRc, int capsRc,
            int propertiesRc, long caps) {
        return String.join("|", "3", "201", Integer.toString(backend),
                Integer.toString(defaultRc), Integer.toString(netId), "0", "1",
                Integer.toString(capsRc), Long.toString(caps), "2", "0", "0",
                Integer.toString(propertiesRc), "wlan0", "1500", "2", "2", "2",
                "10.145.202.149/24;fe80::4245:daff:fe3d:c652/64",
                "114.114.114.114;8.8.8.8",
                "10.145.202.0/24;fe80::/64", "0", "", "0", "");
    }

    public static void main(String[] args) {
        OhConnectivityState.Snapshot recovered = OhConnectivityState.Snapshot.parse(
                wire(3, 100, 201, 0, 0, 0x19000));
        check(recovered.querySucceeded && recovered.available, "recovery availability");
        check(recovered.uncachedOhRecovery && !recovered.kernelFallback, "recovery backend");
        check(recovered.netId == 100 && recovered.isValidated(), "real handle and caps");
        check(recovered.routeCount == 2 && recovered.routes.length == 2, "merged routes");
        check(recovered.diagnosticSignature().contains("oh-uncached+kernel-links"),
                "recovery diagnostic");

        OhConnectivityState.Snapshot fallback = OhConnectivityState.Snapshot.parse(
                wire(2, 33, 201, -1, -1, 0x9000));
        check(fallback.available && fallback.kernelFallback && !fallback.uncachedOhRecovery,
                "kernel fallback");
        check(!fallback.isValidated(), "fallback does not fabricate validation");

        OhConnectivityState.Snapshot incomplete = OhConnectivityState.Snapshot.parse(
                wire(3, 100, 201, -1, 0, 0x19000));
        check(!incomplete.available && !incomplete.querySucceeded,
                "incomplete recovery is fail closed");

        OhConnectivityState.Snapshot absent = OhConnectivityState.Snapshot.parse(
                wire(4, 0, 201, -1, -1, 0));
        check(absent.querySucceeded && !absent.available && absent.netId == 0,
                "confirmed absence must permit loss callbacks");
        check(absent.diagnosticSignature().contains("oh-empty+kernel-no-route"),
                "confirmed absence diagnostic");

        OhConnectivityState.Snapshot unknown = OhConnectivityState.Snapshot.parse(
                wire(0, 0, 201, -1, -1, 0));
        check(!unknown.querySucceeded && !unknown.available,
                "failed OH query and route probe must remain unknown");

        OhConnectivityState.Snapshot direct = OhConnectivityState.Snapshot.parse(
                wire(1, 100, 0, 0, 0, 0x19000));
        check(direct.available && !direct.kernelFallback && !direct.uncachedOhRecovery,
                "direct OH default path");

        check(!OhConnectivityState.Snapshot.parse("2|bad").available, "schema rejection");
        System.out.println("OH_CONNECTIVITY_SNAPSHOT_HOST_PASS recovered fallback absent unknown incomplete direct schema");
    }
}
