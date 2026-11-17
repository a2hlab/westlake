package adapter.core;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Properties;
import java.util.Set;

/** Explicit operator consent for native account OAuth; never a fabricated Google Sign-In result. */
public final class GoogleNativeAuthConsent {
    public static final String SOURCE = "operator-native-account-v1";
    public static final long MAX_LIFETIME_MILLIS = 24 * 60 * 60 * 1000L;
    private GoogleNativeAuthConsent() {}

    public static boolean reusable(GoogleAuthDelegationPolicy request) {
        return request.clientId == null && !request.serverCode && request.options.isEmpty();
    }

    public static String accountHash(String type, String name) {
        if (!"com.google".equals(type) || name == null || name.isEmpty() || name.length() > 320
                || name.chars().anyMatch(Character::isISOControl))
            throw new SecurityException("native account invalid");
        try {
            byte[] bytes = MessageDigest.getInstance("SHA-256")
                    .digest((type + "\u0000" + name).getBytes(StandardCharsets.UTF_8));
            StringBuilder out = new StringBuilder();
            for (byte value : bytes) out.append(String.format("%02x", value & 255));
            return out.toString();
        } catch (java.security.NoSuchAlgorithmException error) {
            throw new IllegalStateException("account digest unavailable");
        }
    }

    private static long timestamp(Properties policy, String key) {
        String value = policy.getProperty(key);
        if (value == null || !value.matches("[1-9][0-9]{0,18}"))
            throw new SecurityException("native consent time invalid");
        try { return Long.parseLong(value); }
        catch (NumberFormatException error) { throw new SecurityException("native consent time invalid"); }
    }

    /** Called before and after provider I/O; package and both signer digests come from the live APK. */
    public static Set<String> require(Properties policy, String owner, String signer, String legacySigner,
            String type, String name, GoogleAuthDelegationPolicy request, String epoch, long now) {
        request.require(policy, owner, signer, legacySigner);
        if (!reusable(request)) throw new SecurityException("native service unsupported");
        String prefix = "delegate." + owner + ".native.";
        if (!SOURCE.equals(policy.getProperty(prefix + "source")))
            throw new SecurityException("native consent absent");
        if (!accountHash(type, name).equals(policy.getProperty(prefix + "account_sha256")))
            throw new SecurityException("native account not granted");
        if (epoch == null || !epoch.matches("0|[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")
                || !epoch.equals(policy.getProperty(prefix + "epoch")))
            throw new SecurityException("native consent revoked");
        long issued = timestamp(policy, prefix + "issued_at_ms");
        long expires = timestamp(policy, prefix + "expires_at_ms");
        if (expires <= issued || expires - issued > MAX_LIFETIME_MILLIS || now < issued || now >= expires)
            throw new SecurityException("native consent expired");
        String raw = policy.getProperty(prefix + "scopes", "");
        if (raw.isEmpty() || raw.length() > GoogleAuthDelegationPolicy.MAX_KEY_LENGTH)
            throw new SecurityException("native scope not granted");
        String[] values = raw.split(",", -1);
        Set<String> scopes = new HashSet<>(Arrays.asList(values));
        if (scopes.size() != values.length || scopes.size() > 128)
            throw new SecurityException("native scope not granted");
        for (String scope : scopes) if (!scope.matches("[\\x21-\\x7e]+"))
            throw new SecurityException("native scope not granted");
        if (!scopes.containsAll(request.scopes)) throw new SecurityException("native scope not granted");
        return java.util.Collections.unmodifiableSet(scopes);
    }

    public static void requireReturnedScopes(Set<String> consented, String returned) {
        if (returned == null || returned.isEmpty()) return;
        for (String scope : returned.split(" ", -1))
            if (scope.isEmpty() || !consented.contains(scope))
                throw new SecurityException("native scope not granted");
    }
}
