package adapter.core;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Properties;
import java.util.Set;

/** Private, one-account experiment envelope. No scoped-token cache or policy flags are copied. */
final class BootstrapTransfer {
    private static final Set<String> FIELDS = new HashSet<>(Arrays.asList(
            "schema", "run", "created", "account", "credential", "profile", "sourceApk", "androidId", "securityToken"));
    private static final long MAX_AGE_MS = 60 * 60 * 1000;

    static Properties envelope(String run, String account, String credential, String profile,
            String sourceApk, long now) throws IOException {
        Properties p = new Properties();
        p.setProperty("schema", "westlake-microg-account-v2");
        p.setProperty("run", run);
        p.setProperty("created", Long.toString(now));
        p.setProperty("account", account);
        p.setProperty("credential", credential);
        p.setProperty("profile", profile);
        p.setProperty("sourceApk", sourceApk);
        p.setProperty("androidId", "0");
        p.setProperty("securityToken", "0");
        validate(p, run, profile, now);
        return p;
    }

    static void validate(Properties p, String run, String profile, long now) throws IOException {
        if (!p.stringPropertyNames().equals(FIELDS)
                || !"westlake-microg-account-v2".equals(p.getProperty("schema"))
                || !run.matches("[A-Za-z0-9_-]{1,64}") || !run.equals(p.getProperty("run"))
                || !profile.equals(p.getProperty("profile"))
                || !p.getProperty("sourceApk", "").matches("[0-9a-f]{64}")) {
            throw new IOException("bootstrap metadata mismatch");
        }
        String account = p.getProperty("account", "");
        String credential = p.getProperty("credential", "");
        if (account.isEmpty() || account.length() > 320 || account.indexOf('/') >= 0
                || account.chars().anyMatch(Character::isISOControl)
                || credential.isEmpty() || credential.length() > 16384
                || credential.chars().anyMatch(Character::isISOControl)) {
            throw new IOException("invalid bootstrap account or credential");
        }
        try {
            long created = Long.parseLong(p.getProperty("created"));
            if (created > now || now - created > MAX_AGE_MS) throw new IOException("expired bootstrap");
            long id = Long.parseLong(p.getProperty("androidId"));
            long token = Long.parseLong(p.getProperty("securityToken"));
            if ((id == 0) != (token == 0)) throw new IOException("incomplete check-in identity");
        } catch (NumberFormatException e) {
            throw new IOException("invalid bootstrap timestamp");
        }
    }

    static Properties read(Path file, String run, String profile, long now) throws IOException {
        if (!Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS) || Files.size(file) > 32768
                || !Files.getPosixFilePermissions(file, LinkOption.NOFOLLOW_LINKS)
                    .equals(PosixFilePermissions.fromString("rw-------"))) {
            throw new IOException("bootstrap input must be a private regular file");
        }
        Properties p = new Properties();
        try (InputStream in = Files.newInputStream(file, LinkOption.NOFOLLOW_LINKS)) { p.load(in); }
        validate(p, run, profile, now);
        return p;
    }

    static Properties accountStore(Properties envelope, Properties existing) throws IOException {
        // An existing Google private credential can win before AccountManager is consulted. A fresh
        // target is mandatory; don't silently merge an experiment into an ordinary account store.
        if (!existing.isEmpty()) throw new IOException("bootstrap requires an empty account store");
        Properties result = new Properties();
        String suffix = "/com.google/" + envelope.getProperty("account");
        result.setProperty("account" + suffix, "1");
        result.setProperty("password" + suffix, envelope.getProperty("credential"));
        result.setProperty("bootstrap/consumed", envelope.getProperty("run"));
        return result;
    }

    static void writePrivate(Path file, Properties p) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        p.store(out, null);
        writePrivateBytes(file, out.toByteArray());
    }

    static void writePrivateBytes(Path file, byte[] bytes) throws IOException {
        Path parent = file.toAbsolutePath().getParent();
        Files.createDirectories(parent);
        if (Files.isSymbolicLink(parent) || Files.isSymbolicLink(file)) {
            throw new IOException("symlink in bootstrap destination");
        }
        Files.setPosixFilePermissions(parent, PosixFilePermissions.fromString("rwx------"));
        Path tmp = Files.createTempFile(parent, ".bootstrap-", ".tmp",
                PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")));
        try {
            try (OutputStream out = Files.newOutputStream(tmp)) { out.write(bytes); }
            // Require atomic replacement. Never leave a half-written account credential behind.
            Files.move(tmp, file, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(tmp);
        }
    }
}
