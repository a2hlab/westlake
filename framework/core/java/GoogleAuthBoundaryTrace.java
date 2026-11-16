package adapter.core;

/** Value-free classifications of the pinned GMS auth/cache boundary, never authorization. */
public final class GoogleAuthBoundaryTrace {
    private GoogleAuthBoundaryTrace() {}

    public static String responseError(String error) {
        if (error == null) return "absent";
        for (String known : new String[] {"OK", "BadAuthentication", "NeedsBrowser", "NetworkError",
                "AccountNotPresent", "NeedPermission", "NeedsPermission", "ServiceDisabled", "UserCancel",
                "INVALID_CLIENT", "UNREGISTERED_ON_API_CONSOLE", "DeviceManagementRequired", "BadRequest"})
            if (known.equals(error)) return known;
        return "other";
    }

    private static String options(String service) {
        int query = service.indexOf('?');
        if (query < 0) return " include_email=absent include_profile=absent opaque_options=false other_options=false";
        String email = "absent", profile = "absent";
        boolean opaque = false, other = false;
        for (String part : service.substring(query + 1).split("&", -1)) {
            String[] pair = part.split("=", 2);
            try {
                String name = java.net.URLDecoder.decode(pair[0], "UTF-8");
                String value = pair.length == 2 ? java.net.URLDecoder.decode(pair[1], "UTF-8") : "";
                String bit = "0".equals(value) || "1".equals(value) ? value : "other";
                if ("include_email".equals(name)) email = bit;
                else if ("include_profile".equals(name)) profile = bit;
                else if ("token_request_options".equals(name)) opaque = true;
                else other = true;
            } catch (Exception invalid) { other = true; }
        }
        return " include_email=" + email + " include_profile=" + profile
                + " opaque_options=" + opaque + " other_options=" + other;
    }

    public static String describe(String method, String key) {
        if ("getPassword".equals(method)) return "key_kind=none";
        if (key == null) return "key_kind=null";
        if ("getUserData".equals(method)) {
            for (String known : new String[] {"GoogleUserId", "GoogleUserIdRefreshTime", "hasPassword",
                    "hasUsername", "services", "oauthAccessToken", "identifier", "firstName", "lastName",
                    "lstBindingKeyAlias", "DeviceManagementStatus", "capabilities_version",
                    "com.google.android.gms.auth.confirm.CredentialsState"}) {
                if (known.equals(key)) return "key_kind=metadata metadata=" + known;
            }
            if (key.startsWith("EXP:")) return "key_kind=expiry " + cache(key.substring(4));
            if (key.startsWith("SCOPES:")) return "key_kind=scopes " + cache(key.substring(7));
            return "key_kind=other_metadata";
        }
        if ("peekAuthToken".equals(method)) return "key_kind=token " + cache(key);
        return "key_kind=unclassified";
    }

    private static String cache(String key) {
        // tku/tns in the inspected GMS build use package:signer:scope[?options].
        // Output only fixed labels and booleans. Unknown strings and OAuth client IDs stay private.
        if (key.startsWith("hub:")) key = key.substring(4);
        String[] fields = key.split(":", 3);
        if (fields.length != 3) return "cache_shape=unrecognized";
        String owner = "org.westlake.googlebootstrap.probe".equals(fields[0]) ? "probe"
                : "com.spotify.music".equals(fields[0]) ? "spotify"
                : "com.google.android.gms".equals(fields[0]) ? "gms"
                : "com.android.vending".equals(fields[0]) ? "store" : "other";
        String scope = fields[2].split("\\?", 2)[0];
        String kind = "ac2dm".equals(scope) ? "ac2dm"
                : "androidmarket".equals(scope) ? "androidmarket"
                : scope.startsWith("oauth2:server:client_id:") ? "oauth2_server_client"
                : scope.startsWith("oauth2:") ? "oauth2"
                : scope.startsWith("audience:server:client_id:") ? "audience_server_client"
                : scope.startsWith("weblogin:") ? "weblogin" : "other";
        return "cache_shape=package_signer_scope owner=" + owner
                + " signer_sha1_shape=" + fields[1].matches("[0-9a-fA-F]{40}")
                + " scope_kind=" + kind + " options_present=" + (fields[2].indexOf('?') >= 0)
                + options(fields[2]);
    }
}
