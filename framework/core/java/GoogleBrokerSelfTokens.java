package adapter.core;

import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Experiment (user-approved 2026-11-20): Play services' own short-lived tokens for its account management.
 *
 * Play services asks its token cache (AccountManager.peekAuthToken) for its own tokens before it reaches for
 * the account's reusable credential, which Westlake always withholds. Without them it cannot read the
 * account's details or state, and falls back to partial data: an app's Sign in with Google credential then
 * names the account by its numeric ID instead of its email, and the Authorization API refuses that account.
 *
 * Exactly two requests, four scopes in all, are answered, and only through Play services' own client grant
 * at the broker (client.com.google.android.gms.scopes): never the reusable credential, never another
 * package's token, never other scopes or options. Off unless WESTLAKE_GOOGLE_BROKER_SELF_TOKENS=1.
 */
public final class GoogleBrokerSelfTokens {
    public static final String FLAG = "WESTLAKE_GOOGLE_BROKER_SELF_TOKENS";
    static final String GMS = "com.google.android.gms";
    /** The approved requests, each exactly as the broker's client grant lists it. */
    public static final List<List<String>> ALLOWED = List.of(
            List.of("https://www.googleapis.com/auth/lams"),
            List.of("https://www.googleapis.com/auth/userinfo.email",
                    "https://www.googleapis.com/auth/account.capabilities",
                    "https://www.googleapis.com/auth/account.service_flags"));
    /** The broker's client path reports no expiry: answer a token for half of a Google access token's hour. */
    static final long LIFETIME_S = 1800;
    static final int MAX_CACHED = 8;

    /**
     * The service to ask the broker for, when key (package:signer-sha1:service, Play services' cache key) is
     * one of Play services' own approved requests under its own signer; null otherwise.
     */
    public static String allowedService(String key, String ownSignerSha1) {
        if (key == null || ownSignerSha1 == null) return null;
        GoogleAuthDelegationPolicy parsed;
        try {
            parsed = GoogleAuthDelegationPolicy.parse(key);
        } catch (SecurityException unsupported) {
            return null;
        }
        if (!GMS.equals(parsed.owner) || !parsed.signer.equals(ownSignerSha1.toLowerCase(Locale.ROOT))) return null;
        if (!parsed.options.isEmpty() || parsed.clientId != null || parsed.serverCode
                || !parsed.service.startsWith("oauth2:")) return null;
        for (List<String> allowed : ALLOWED) {
            if (parsed.scopes.equals(new HashSet<>(allowed))) return "oauth2:" + String.join(" ", allowed);
        }
        return null;
    }

    /** Which approved request a service is, for value-free events. */
    public static String label(String service) {
        return service != null && service.endsWith("/lams") ? "lams" : "account_state";
    }

    private static final class Entry {
        final String token, scopes;
        final long expiry;
        Entry(String token, String scopes, long expiry) { this.token = token; this.scopes = scopes; this.expiry = expiry; }
    }

    /** Account name and service -> the token answered for them. Held in memory only, never logged. */
    private final Map<String, Entry> cache = new LinkedHashMap<>();

    private static String slot(String account, String service) { return account + '\n' + service; }

    private Entry live(String account, String service, long now) {
        Entry entry = cache.get(slot(account, service));
        if (entry != null && entry.expiry <= now) {
            cache.remove(slot(account, service));
            return null;
        }
        return entry;
    }

    public synchronized String cachedToken(String account, String service, long now) {
        Entry entry = live(account, service, now);
        return entry == null ? null : entry.token;
    }

    /** The expiry Play services reads beside a token (EXP:), in seconds; null when none is held. */
    public synchronized String expiry(String account, String service, long now) {
        Entry entry = live(account, service, now);
        return entry == null ? null : Long.toString(entry.expiry);
    }

    /** The granted scopes Play services reads beside a token (SCOPES:); null when none is held. */
    public synchronized String scopes(String account, String service, long now) {
        Entry entry = live(account, service, now);
        return entry == null ? null : entry.scopes;
    }

    public synchronized void put(String account, String service, String token, long now) {
        if (account == null || service == null || token == null || token.isEmpty()) return;
        cache.remove(slot(account, service));
        while (cache.size() >= MAX_CACHED) {
            Iterator<String> oldest = cache.keySet().iterator();
            oldest.next();
            oldest.remove();
        }
        cache.put(slot(account, service), new Entry(token, service.substring("oauth2:".length()), now + LIFETIME_S));
    }

    /** Play services invalidated token: it is not answered again. */
    public synchronized boolean invalidate(String token) {
        if (token == null || token.isEmpty()) return false;
        boolean removed = false;
        for (Iterator<Entry> entries = cache.values().iterator(); entries.hasNext(); ) {
            if (token.equals(entries.next().token)) {
                entries.remove();
                removed = true;
            }
        }
        return removed;
    }

    synchronized int size() { return cache.size(); }
}
