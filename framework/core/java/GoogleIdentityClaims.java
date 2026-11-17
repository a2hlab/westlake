package adapter.core;

import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

/** Opt-in adaptation of the interactive GIS identity contract; never a scope grant. */
public final class GoogleIdentityClaims {
    private GoogleIdentityClaims() {}

    public static String request(String key, boolean enabled, String host, String activity) {
        if (!enabled || !"com.google.android.gms".equals(host)
                || !"com.google.android.gms.auth.api.credentials.assistedsignin.ui.GoogleSignInActivity".equals(activity)) return key;
        GoogleAuthDelegationPolicy parsed = GoogleAuthDelegationPolicy.parse(key);
        if (parsed.clientId == null || parsed.serverCode) return key;
        // GIS returns basic identity data. The cache-mediated path can lose those
        // claims. Request them from Google, without manufacturing a JWT or copying
        // account data into it. The provider must still authorize both claim scopes,
        // the exact client ID, caller signatures and live account-selection witness.
        Map<String, String> options = new LinkedHashMap<>(parsed.options);
        options.put("include_email", "1");
        options.put("include_profile", "1");
        int query = key.indexOf('?');
        StringBuilder result = new StringBuilder(query < 0 ? key : key.substring(0, query));
        for (Map.Entry<String, String> option : options.entrySet()) {
            result.append(result.indexOf("?") < 0 ? '?' : '&');
            result.append(URLEncoder.encode(option.getKey(), StandardCharsets.UTF_8));
            result.append('=').append(URLEncoder.encode(option.getValue(), StandardCharsets.UTF_8));
        }
        return result.toString();
    }
}
