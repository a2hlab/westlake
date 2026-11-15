package adapter.core;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Properties;

/** Executes actual transfer code with synthetic credentials; never contacts Google. */
public final class BootstrapTransferTest {
    private interface Action { void run() throws Exception; }
    private static int count;
    private static void denied(Action action) throws Exception {
        try { action.run(); } catch (IOException expected) { count++; return; }
        throw new AssertionError("invalid transfer accepted");
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("bootstrap-test-");
        try {
            long now = 1700000000000L;
            String hash = "a".repeat(64);
            Properties p = BootstrapTransfer.envelope("run1", "test@example.invalid", "synthetic-credential", hash, hash, now);
            Path file = root.resolve("transfer.properties");
            BootstrapTransfer.writePrivate(file, p);
            Properties read = BootstrapTransfer.read(file, "run1", hash, now);
            Properties accounts = BootstrapTransfer.accountStore(read, new Properties());
            if (accounts.size() != 3 || !"synthetic-credential".equals(accounts.getProperty("password/com.google/test@example.invalid")))
                throw new AssertionError("incorrect account handoff");
            count++;
            denied(() -> BootstrapTransfer.accountStore(read, accounts));
            denied(() -> BootstrapTransfer.read(file, "other-run", hash, now));
            denied(() -> BootstrapTransfer.read(file, "run1", "different-profile", now));
            denied(() -> BootstrapTransfer.read(file, "run1", hash, now + 3600001));
            denied(() -> BootstrapTransfer.read(file, "run1", hash, now - 1));
            Properties injected = new Properties(); injected.putAll(p); injected.setProperty("token/com.google/test/ac2dm", "not-allowed");
            denied(() -> BootstrapTransfer.validate(injected, "run1", hash, now));
            denied(() -> BootstrapTransfer.envelope("run1", "test/other", "credential", hash, hash, now));
            denied(() -> BootstrapTransfer.envelope("run1", "test@example.invalid", "", hash, hash, now));
            Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-r--r--"));
            denied(() -> BootstrapTransfer.read(file, "run1", hash, now));
            Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-------"));
            Path link = root.resolve("link"); Files.createSymbolicLink(link, file);
            denied(() -> BootstrapTransfer.read(link, "run1", hash, now));
            denied(() -> BootstrapTransfer.writePrivate(link, p));
            Files.delete(link);
            Properties paired = BootstrapTransfer.envelope("run1", "test@example.invalid", "synthetic-credential", hash, hash, System.currentTimeMillis());
            paired.setProperty("androidId", "123"); paired.setProperty("securityToken", "456");
            Path accountOnly = root.resolve("account-only");
            BootstrapStage.stage(paired, accountOnly, "account-only", hash);
            if (Files.exists(accountOnly.resolve("files/checkin_id_token"))) throw new AssertionError("account-only copied device identity");
            Properties only = BootstrapTransfer.read(accountOnly.resolve("westlake-bootstrap/incoming.properties"), "run1", hash, System.currentTimeMillis());
            if (!"0".equals(only.getProperty("securityToken"))) throw new AssertionError("account-only retained device secret");
            count++;
            Path both = root.resolve("both");
            BootstrapStage.stage(paired, both, "account-and-device", BootstrapStage.GMS_244034029);
            if (!"123:456".equals(Files.readString(both.resolve("files/checkin_id_token")))) throw new AssertionError("wrong check-in encoding");
            count++;
            denied(() -> BootstrapStage.stage(paired, both, "account-only", hash));
            try {
                BootstrapStage.stage(paired, root.resolve("wrong-apk"), "account-and-device", hash);
                throw new AssertionError("unknown official APK accepted");
            } catch (IllegalArgumentException expected) { count++; }
            paired.setProperty("securityToken", "0");
            denied(() -> BootstrapTransfer.validate(paired, "run1", hash, System.currentTimeMillis()));
            System.out.println("BOOTSTRAP_TRANSFER_PASS cases=" + count);
        } finally {
            try (java.util.stream.Stream<Path> paths = Files.walk(root)) {
                for (Path path : paths.sorted(java.util.Comparator.reverseOrder()).toArray(Path[]::new)) Files.delete(path);
            }
        }
    }
}
