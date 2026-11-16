package adapter.core;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermission;
import java.security.MessageDigest;
import java.util.Properties;
import java.util.UUID;

/** Private, revocable account selections. Contains no account name or token; never replaces provider grants. */
public final class GoogleAuthSessions {
    private GoogleAuthSessions() {}

    private static String hash(String text) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(text.getBytes(StandardCharsets.UTF_8));
            StringBuilder out = new StringBuilder();
            for (byte b : digest) out.append(String.format("%02x", b & 255));
            return out.toString();
        } catch (Exception error) { throw new IllegalStateException("session digest unavailable"); }
    }

    private static Properties read(Path file) throws IOException {
        Properties p = new Properties();
        if (!Files.exists(file, LinkOption.NOFOLLOW_LINKS)) return p;
        if (Files.isSymbolicLink(file.getParent()) || Files.isSymbolicLink(file)
                || !Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS) || Files.size(file) > 8192)
            throw new IOException("invalid session file");
        for (PosixFilePermission permission : Files.getPosixFilePermissions(file)) {
            if (permission.name().startsWith("GROUP_") || permission.name().startsWith("OTHERS_"))
                throw new IOException("session file is not private");
        }
        try (java.io.InputStream in = Files.newInputStream(file)) { p.load(in); }
        return p;
    }

    private static Path generationFile(Path directory, String owner) {
        return directory.resolve("epoch-" + hash(owner) + ".properties");
    }

    public static String generation(Path directory, String owner) throws IOException {
        Path file = generationFile(directory, owner);
        if (!Files.exists(file, LinkOption.NOFOLLOW_LINKS)) return "0";
        String epoch = read(file).getProperty("epoch");
        if (epoch == null || !epoch.matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))
            throw new IOException("invalid session generation");
        return epoch;
    }

    /** A revocation races safely with a grant: old interactive operations cannot restore the old epoch. */
    public static void revoke(Path directory, String owner) throws IOException {
        Properties p = new Properties(); p.setProperty("epoch", UUID.randomUUID().toString());
        PrivateFiles.writePrivate(generationFile(directory, owner), p);
    }

    private static String account(String type, String name) { return hash(type + "\u0000" + name); }

    private static Path file(Path directory, String owner, String signer, String type, String name, String service) {
        return directory.resolve("selection-" + hash(owner + "\u0000" + signer + "\u0000"
                + account(type, name) + "\u0000" + service) + ".properties");
    }

    private static void idToken(GoogleAuthDelegationPolicy request) {
        if (request.clientId == null || request.serverCode || !request.service.startsWith("audience:server:client_id:"))
            throw new SecurityException("silent service unsupported");
        // A nonce or another opaque constraint cannot become a reusable account-selection grant.
        if (request.options.containsKey("token_request_options")) throw new SecurityException("silent options unsupported");
    }

    /** Called only after a successful verified Google UI flow, with its provider-approved request. */
    public static void grant(Path directory, String owner, String signer, String legacySigner,
            String type, String name, String providerKey, String epoch) throws IOException {
        GoogleAuthDelegationPolicy request = GoogleAuthDelegationPolicy.parse(providerKey);
        idToken(request);
        if (!owner.equals(request.owner) || !legacySigner.equals(request.signer) || !"com.google".equals(type)
                || name == null || name.isEmpty() || epoch == null || !epoch.equals(generation(directory, owner)))
            throw new SecurityException("session grant mismatch");
        Properties p = new Properties();
        p.setProperty("version", "1"); p.setProperty("owner", owner); p.setProperty("signer", signer);
        p.setProperty("legacySigner", legacySigner); p.setProperty("account", account(type, name));
        p.setProperty("service", request.service); p.setProperty("epoch", epoch);
        p.setProperty("include_email", request.options.getOrDefault("include_email", "0"));
        p.setProperty("include_profile", request.options.getOrDefault("include_profile", "0"));
        PrivateFiles.writePrivate(file(directory, owner, signer, type, name, request.service), p);
        // If revoke won during the atomic write, the saved old epoch is unusable.
        if (!epoch.equals(generation(directory, owner))) throw new SecurityException("session revoked");
    }

    /** Every token operation and its witness re-read this; no stale in-memory authorization cache. */
    public static boolean allows(Path directory, String owner, String signer, String legacySigner,
            String type, String name, GoogleAuthDelegationPolicy request, String epoch) throws IOException {
        idToken(request);
        if (!owner.equals(request.owner) || !legacySigner.equals(request.signer) || !"com.google".equals(type)
                || name == null || name.isEmpty() || epoch == null || !epoch.equals(generation(directory, owner))) return false;
        Properties p = read(file(directory, owner, signer, type, name, request.service));
        if (!"1".equals(p.getProperty("version")) || !owner.equals(p.getProperty("owner"))
                || !signer.equals(p.getProperty("signer")) || !legacySigner.equals(p.getProperty("legacySigner"))
                || !account(type, name).equals(p.getProperty("account"))
                || !request.service.equals(p.getProperty("service")) || !epoch.equals(p.getProperty("epoch"))) return false;
        for (String option : new String[] {"include_email", "include_profile"})
            if ("1".equals(request.options.get(option)) && !"1".equals(p.getProperty(option))) return false;
        return true;
    }
}
