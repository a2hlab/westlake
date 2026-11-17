package adapter.core;

/** Value-free classifications of the pinned GMS auth/cache boundary, never authorization. */
public final class GoogleAuthBoundaryTrace {
    private GoogleAuthBoundaryTrace() {}

    /** Fixed public categories only; never print a service, scope, client ID or option value. */
    public static String requestService(String service) {
        if (service == null) return "request_kind=null";
        if (service.length() > 4096) return "request_kind=oversize";
        if (service.startsWith("audience:server:client_id:")) return "request_kind=id_token";
        if (service.startsWith("oauth2:server:client_id:")) return "request_kind=server_code";
        if (!service.startsWith("oauth2:")) {
            if ("^^_account_id_^^".equals(service)) return "request_kind=account_id";
            for (String known : new String[] {"ac2dm", "androidmarket", "writely"})
                if (known.equals(service)) return "request_kind=" + known;
            if ("https://docs.google.com/feeds".equals(service)) return "request_kind=docs_feed";
            if ("https://docs.google.com/feeds/".equals(service)) return "request_kind=docs_feed_slash";
            return "request_kind=other";
        }
        String body = service.substring(7).split("\\?", 2)[0];
        java.util.Set<String> labels = new java.util.TreeSet<>();
        int unknown = 0, empty = 0;
        for (String scope : body.split(" ", -1)) {
            if (scope.isEmpty()) { empty++; continue; }
            String label = null;
            for (String known : new String[] {"openid", "email", "profile"})
                if (known.equals(scope)) label = known;
            for (String known : new String[] {"drive", "drive.readonly", "drive.file", "drive.appdata",
                    "drive.metadata.readonly", "espresso", "photos.image.readonly", "cloud_search.query",
                    "workspace_ui", "userinfo.email", "userinfo.profile", "user.birthday.read",
                    "youtube", "youtube.readonly", "gmail.readonly", "gmail.full_access", "gmail.ads",
                    "gmail.locker.read", "gmail.publisher_first_party", "taskassist.readonly", "reminders",
                    "calendar.readonly", "chat", "notifications", "subscriptions",
                    "supportcontent", "apps.growth.promotion.read", "apps.signup", "activity", "cloudprint",
                    "discussions", "discussions.readonly", "docs", "drive.activity", "drive.activity.readonly",
                    "drive.apps", "drive.apps.readonly", "drive.categories.readonly", "drive.meet.readonly",
                    "drive.metadata", "drive.photos.readonly", "drive.resource", "drive.scripts", "games",
                    "games_lite", "memento", "peopleapi.readonly", "peopleapi.readwrite", "spreadsheets", "vouchers"})
                if (("https://www.googleapis.com/auth/" + known).equals(scope)) label = known.replace('.', '_');
            if ("https://docs.google.com/feeds".equals(scope)) label = "docs_feed";
            if ("https://docs.google.com/feeds/".equals(scope)) label = "docs_feed_slash";
            if ("https://mail.google.com/".equals(scope)) label = "mail_full";
            if (label == null) unknown++; else labels.add(label);
        }
        return "request_kind=oauth2 scope_categories=" + (labels.isEmpty() ? "none" : String.join(",", labels))
                + " unknown_scope_count=" + unknown + " empty_scope_count=" + empty;
    }

    public static String requestExtras(java.util.Collection<String> keys) {
        if (keys == null) return "extras_categories=none unknown_extra_count=0";
        java.util.Set<String> labels = new java.util.TreeSet<>();
        int unknown = 0;
        for (String key : keys) {
            String label = null;
            for (String known : new String[] {"androidPackageName", "clientPackageName",
                    "service_connection_start_time_millis", "handle_notification", "suppressProgressScreen",
                    "callerUid", "callerPid", "isCalledFromAccountManager", "request_visible_actions",
                    "forceRefresh", "TokenData", "oauth2_client_id", "scope", "authAccount"})
                if (known.equals(key)) label = known;
            if (label == null) unknown++; else labels.add(label);
        }
        return "extras_categories=" + (labels.isEmpty() ? "none" : String.join(",", labels))
                + " unknown_extra_count=" + unknown;
    }

    /** Private, opt-in test correlation only; never a token, claim, or authorization decision.
     * The inspected FotMob TokenData serializer uses SafeParcelable object 20293, string field 2.
     * Unknown/malformed encodings fail closed. Callers must enforce the private-log guards.
     */
    public static String tokenFingerprint(byte[] encoded) {
        final String unavailable = "token_fingerprint_unavailable=true";
        if (encoded == null || encoded.length < 8 || encoded.length > 65536) return unavailable;
        try {
            java.nio.ByteBuffer data = java.nio.ByteBuffer.wrap(encoded).order(java.nio.ByteOrder.LITTLE_ENDIAN);
            int header = data.getInt();
            if ((header & 65535) != 20293) return unavailable;
            int size = fieldSize(data, header);
            if (size != data.remaining()) return unavailable;
            String token = null;
            while (data.hasRemaining()) {
                header = data.getInt();
                size = fieldSize(data, header);
                if (size < 0 || size > data.remaining()) return unavailable;
                int end = data.position() + size;
                if ((header & 65535) == 2) {
                    if (token != null || size < 8) return unavailable;
                    int chars = data.getInt();
                    if (chars <= 0 || chars > 16384 || ((4 + (chars + 1) * 2 + 3) & ~3) != size)
                        return unavailable;
                    byte[] text = new byte[chars * 2]; data.get(text);
                    if (data.getShort() != 0) return unavailable;
                    token = new String(text, java.nio.charset.StandardCharsets.UTF_16LE);
                    if (!token.matches("[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+")) return unavailable;
                }
                data.position(end);
            }
            if (token == null) return unavailable;
            byte[] digest = java.security.MessageDigest.getInstance("SHA-256")
                    .digest(token.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            StringBuilder hex = new StringBuilder();
            for (byte b : digest) hex.append(Character.forDigit((b >>> 4) & 15, 16)).append(Character.forDigit(b & 15, 16));
            return "token_fingerprint_sha256=" + hex;
        } catch (Exception invalid) { return unavailable; }
    }

    private static int fieldSize(java.nio.ByteBuffer data, int header) {
        return (header >>> 16) == 65535 ? data.getInt() : header >>> 16;
    }

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
                : "com.android.vending".equals(fields[0]) ? "store"
                : "com.google.android.apps.docs".equals(fields[0]) ? "drive"
                : "com.google.android.gm".equals(fields[0]) ? "gmail"
                : "com.google.android.youtube".equals(fields[0]) ? "youtube" : "other";
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
                + options(fields[2]) + " " + requestService(fields[2]);
    }
}
