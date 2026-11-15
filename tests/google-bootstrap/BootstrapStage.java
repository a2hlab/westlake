package adapter.core;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Properties;

/** Offline staging, before the target host starts. Never copy these output files into reports. */
public final class BootstrapStage {
    static final String GMS_244034029 = "1159bb46cad6f4afd7f4a570b1a5674f1285a12e573dbe6501ed4b059fd902d1";

    static void stage(Properties envelope, Path output, String mode, String officialApkHash) throws Exception {
        if (!mode.equals("account-only") && !mode.equals("account-and-device")) throw new IllegalArgumentException("unknown mode");
        BootstrapTransfer.validate(envelope, envelope.getProperty("run"), envelope.getProperty("profile"), System.currentTimeMillis());
        if (mode.equals("account-and-device")) {
            if (!GMS_244034029.equals(officialApkHash)) throw new IllegalArgumentException("device format only verified for pinned GMS APK");
            if (Long.parseLong(envelope.getProperty("androidId")) == 0) throw new IllegalArgumentException("missing check-in identity");
        }
        Files.createDirectory(output); // Refuse existing trees, including another experiment's data.
        Files.setPosixFilePermissions(output, java.nio.file.attribute.PosixFilePermissions.fromString("rwx------"));
        Properties receipt = new Properties();
        receipt.setProperty("run", envelope.getProperty("run"));
        receipt.setProperty("mode", mode);
        receipt.setProperty("gmsApk", officialApkHash);
        Properties incoming = new Properties(); incoming.putAll(envelope);
        if (mode.equals("account-and-device")) {
            // In the pinned GMS build, aizc.j reads UTF-8 id:token, aizt.b reads the first
            // long, and aizc.d reads the second. Stage before Application.onCreate so cached
            // identity cannot precede the handoff. This does not write allowance/policy flags.
            BootstrapTransfer.writePrivateBytes(output.resolve("files/checkin_id_token"),
                    (envelope.getProperty("androidId") + ":" + envelope.getProperty("securityToken"))
                            .getBytes(StandardCharsets.UTF_8));
        } else {
            incoming.setProperty("androidId", "0"); incoming.setProperty("securityToken", "0");
        }
        BootstrapTransfer.writePrivate(output.resolve("westlake-bootstrap/incoming.properties"), incoming);
        BootstrapTransfer.writePrivate(output.resolve("westlake-bootstrap/staging.properties"), receipt);
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) throw new IllegalArgumentException("envelope new-output-directory official-apk account-only|account-and-device");
        Path input = Path.of(args[0]);
        Properties metadata = new Properties();
        try (InputStream in = Files.newInputStream(input)) { metadata.load(in); }
        Properties envelope = BootstrapTransfer.read(input, metadata.getProperty("run", ""),
                metadata.getProperty("profile", ""), System.currentTimeMillis());
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream in = Files.newInputStream(Path.of(args[2]))) {
            byte[] buffer = new byte[65536]; int n;
            while ((n = in.read(buffer)) != -1) digest.update(buffer, 0, n);
        }
        StringBuilder hash = new StringBuilder();
        for (byte b : digest.digest()) hash.append(String.format("%02x", b & 255));
        stage(envelope, Path.of(args[1]), args[3], hash.toString());
        System.out.println("BOOTSTRAP_STAGED mode=" + args[3] + " server_acceptance=untested");
    }
}
