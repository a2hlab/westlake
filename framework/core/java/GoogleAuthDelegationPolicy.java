package adapter.core;

import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Properties;
import java.util.Set;

/** Pinned GMS cache-key syntax is input, never proof of the originating caller. */
public final class GoogleAuthDelegationPolicy {
    public final String owner, signer, service, clientId;
    public final boolean serverCode;
    public final Map<String, String> options;
    public final Set<String> scopes;

    private GoogleAuthDelegationPolicy(String key) {
        if (key == null || key.length() > 4096) throw new SecurityException("key size");
        String[] identity = key.split(":", 3);
        if (identity.length != 3 || !identity[0].matches("[A-Za-z0-9_]+(?:\\.[A-Za-z0-9_]+)+")
                || !identity[1].matches("[0-9a-fA-F]{40}")) throw new SecurityException("key identity");
        owner = identity[0]; signer = identity[1].toLowerCase(java.util.Locale.ROOT);
        String[] request = identity[2].split("\\?", -1);
        if (request.length > 2) throw new SecurityException("key query");
        service = request[0];
        options = new LinkedHashMap<>();
        if (request.length == 2) {
            for (String field : request[1].split("&", -1)) {
                String[] pair = field.split("=", -1);
                if (pair.length != 2) throw new SecurityException("option syntax");
                String name = decode(pair[0]), value = decode(pair[1]);
                if (options.put(name, value) != null) throw new SecurityException("duplicate option");
                if (name.equals("include_email") || name.equals("include_profile")) {
                    if (!value.equals("0") && !value.equals("1")) throw new SecurityException("option value");
                } else if (!name.equals("token_request_options") || value.length() > 1024) {
                    throw new SecurityException("unsupported option");
                }
            }
        }
        String scopeList;
        serverCode = service.startsWith("oauth2:server:client_id:");
        if (serverCode) {
            String[] server = service.substring(24).split(":api_scope:", -1);
            if (server.length != 2) throw new SecurityException("server scope");
            clientId = server[0]; scopeList = server[1];
        } else if (service.startsWith("audience:server:client_id:")) {
            clientId = service.substring(26); scopeList = "";
        } else if (service.startsWith("oauth2:")) {
            clientId = null; scopeList = service.substring(7);
        } else throw new SecurityException("unsupported service");
        if (clientId != null && !clientId.matches("[A-Za-z0-9_-]+\\.apps\\.googleusercontent\\.com"))
            throw new SecurityException("client id syntax");
        scopes = new HashSet<>();
        if (!scopeList.isEmpty()) for (String scope : scopeList.split(" ", -1)) {
            if (scope.isEmpty() || !scopes.add(scope)) throw new SecurityException("scope syntax");
        }
        if ((clientId == null || serverCode) && scopes.isEmpty()) throw new SecurityException("empty scopes");
    }

    private static String decode(String value) {
        try { return URLDecoder.decode(value, StandardCharsets.UTF_8.name()); }
        catch (Exception error) { throw new SecurityException("option encoding"); }
    }

    public static GoogleAuthDelegationPolicy parse(String key) { return new GoogleAuthDelegationPolicy(key); }

    public void require(Properties policy, String verifiedPackage, String currentSha256, String legacySha1) {
        if (!owner.equals(verifiedPackage) || !signer.equals(legacySha1)) throw new SecurityException("origin mismatch");
        String prefix = "delegate." + verifiedPackage + ".";
        if (!currentSha256.equals(policy.getProperty(prefix + "sha256"))) throw new SecurityException("origin not granted");
        if (clientId != null && !clientId.equals(policy.getProperty(prefix + "client_id")))
            throw new SecurityException("client id not granted");
        Set<String> allowed = new HashSet<>(Arrays.asList(policy.getProperty(prefix + "scopes", "").split(",", -1)));
        if (!allowed.containsAll(scopes)) throw new SecurityException("scope not granted");
        if ("1".equals(options.get("include_email")) && !allowed.contains("email")
                && !allowed.contains("https://www.googleapis.com/auth/userinfo.email"))
            throw new SecurityException("scope not granted");
        if ("1".equals(options.get("include_profile")) && !allowed.contains("profile")
                && !allowed.contains("https://www.googleapis.com/auth/userinfo.profile"))
            throw new SecurityException("scope not granted");
        String opaque = options.get("token_request_options");
        if (opaque != null && !opaque.equals(policy.getProperty(prefix + "token_request_options")))
            throw new SecurityException("opaque option not granted");
    }

    public void requireGrantedScopes(Properties policy, String returnedScopes) {
        if (returnedScopes == null || returnedScopes.isEmpty()) return;
        Set<String> allowed = new HashSet<>(Arrays.asList(policy.getProperty("delegate." + owner + ".scopes", "").split(",", -1)));
        for (String scope : returnedScopes.split(" ", -1)) {
            if (scope.isEmpty() || !allowed.contains(scope)) throw new SecurityException("scope not granted");
        }
    }

    public static void requireBroker(Properties policy, String owner, String currentSha256) {
        if (!"com.google.android.gms".equals(owner)
                || !currentSha256.equals(policy.getProperty("delegation.broker.sha256")))
            throw new SecurityException("broker not granted");
    }
}
