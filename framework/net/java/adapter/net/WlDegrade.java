package adapter.net;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;

/**
 * Graceful degradation policy for device-credential-dependent ("non-anonymous")
 * endpoints on a compat layer that cannot obtain a server-issued device identity.
 *
 * WHY THIS EXISTS
 * ---------------
 * The anonymous cold-start path works: the recommended feed renders. But channels
 * gated by anti-abuse (the hot-list and other non-recommended channels) need a
 * server-issued device_id and/or the MSSDK X-Gorgon/X-Argus signature, neither of
 * which is obtainable here -- the signer (libmetasec_ml.so) crashes intrinsically
 * on this layer, and device_id is server-assigned and validated, so it cannot be
 * forged (see WHITEBOARD_BRIDGE.md 2.53-2.58). The server answers those endpoints
 * for an unregistered device with, e.g.,
 *
 *     {"base_resp":{"status_code":400,"status_message":"invalid user"}}
 *
 * Left as-is that can leave the channel's UI in a blank / indefinitely-loading
 * state. The purpose of this class is to turn that into a *defined, friendly*
 * outcome -- a well-formed empty result the app renders as an empty/soft-error
 * state, or a fast clean failure that triggers the app's own "network error,
 * retry" UI -- rather than a hang or a raw protocol error.
 *
 * SCOPE AND SAFETY
 * ----------------
 * This is a pure *policy + content* module: it decides whether a request should be
 * degraded and supplies the fallback body. It performs no network I/O and rewrites
 * no live stream, so it cannot itself destabilise a connection. Two decisions keep
 * it from ever touching the one working channel:
 *
 *   1. Registration gate: degradation only applies when the device is actually
 *      unregistered (device_id / install_id absent). Once a real registration
 *      exists, this class is inert and every endpoint behaves normally.
 *   2. Allow-list, not host-block: the recommended feed and the hot-list share a
 *      host (api.toutiaoapi.com), so host-level blocking is unsafe. Classification
 *      is by request *path/marker*, and the recommended feed path is explicitly
 *      exempt (isRecommendedFeed) so it is never degraded.
 *
 * The wiring that consults this class (path classification of the outbound h2
 * request, and delivery of the fallback) is described in docs/DEGRADATION.md; the
 * live-enforcement hook is gated off until it can be validated on a stable board,
 * because unvalidated h2-stream manipulation must not risk the working feed.
 */
public final class WlDegrade {

    private WlDegrade() { }

    /** Master switch: a marker file, so enforcement can be toggled without a rebuild. */
    private static final String ENABLE_MARKER = "/data/local/tmp/wl-degrade";

    /** The app's AppLog SharedPreferences file (AppLogConstants.SP_APPLOG_STATS). */
    private static final String[] APPLOG_STATS_SP = {
        "/data/app/el2/100/base/com.ss.android.article.news/shared_prefs/applog_stats.xml",
        "/data/app/el1/bundle/public/com.ss.android.article.news/shared_prefs/applog_stats.xml",
    };

    /** Enforcement is opt-in via the marker file (default: policy-only, no action). */
    public static boolean enforcementEnabled() {
        try {
            return new File(ENABLE_MARKER).isFile();
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * True when the device has a server-issued identity. When this is true the
     * whole module is inert -- there is nothing to degrade.
     *
     * device_id / install_id are read from the applog_stats SP, which is where the
     * device_register response persists them (never SQLite; see 2.53). A value is
     * "present" only if non-empty and not the all-zeros / "unknown" placeholder,
     * matching the app's own NetUtil.isBadId contract.
     */
    public static boolean isDeviceRegistered() {
        String did = readSpString("device_id");
        String iid = readSpString("install_id");
        return !isBadId(did) && !isBadId(iid);
    }

    /** Mirrors com.bytedance.common.utility.NetUtil.isBadId (client-side check). */
    static boolean isBadId(String s) {
        if (s == null || s.length() == 0) return true;
        if (s.equalsIgnoreCase("unknown") || s.equalsIgnoreCase("none")) return true;
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) != '0') return false;
        }
        return true;   // all zeros
    }

    /**
     * Whether a request to (host, path) is a device-credential-dependent channel
     * that should be degraded when the device is unregistered.
     *
     * Conservative by construction: returns true only for paths positively
     * identified as non-anonymous channel content, and never for the recommended
     * feed. An unknown path returns false (fail-open: behave normally), so a
     * misclassification can only ever *under*-degrade, never break a working path.
     */
    public static boolean shouldDegrade(String host, String path) {
        if (host == null || path == null) return false;
        if (isRecommendedFeed(path)) return false;          // never touch the working feed
        if (!isDeviceRegistered() && isNonAnonymousChannel(path)) return true;
        return false;
    }

    /**
     * The recommended feed -- the anonymous channel that already works. Explicitly
     * exempt so it is never degraded even though it shares a host with the rest.
     * Recommended is query_category=__all__ on the feed endpoint.
     */
    static boolean isRecommendedFeed(String path) {
        if (!path.startsWith("/api/news/feed/")) return false;
        return path.contains("category=__all__") || !path.contains("category=");
    }

    /**
     * Positive markers of a non-anonymous channel. These are the channels shown as
     * "网络异常" in screenshot #21.
     *
     * NOTE: the exact hot-list path/params are not yet captured -- the app never
     * issued the request in any observed run (it gates on device readiness) and the
     * board UI could not be driven to the tab. The markers below are the documented
     * ByteDance endpoints for these channels; they MUST be reconciled against a
     * real on-board hot-list capture before enforcement is enabled (see
     * docs/DEGRADATION.md, "Unknowns to close on a stable board").
     */
    static boolean isNonAnonymousChannel(String path) {
        // hot-list / hot board
        if (path.startsWith("/hotboard/")) return true;
        if (path.contains("/hot_board")) return true;
        if (path.startsWith("/api/news/feed/") && path.contains("category=")
                && !path.contains("category=__all__")) {
            return true;   // any non-recommended feed channel
        }
        return false;
    }

    /** Classifies a request for fallback selection. */
    public enum Kind { FEED_LIST, GENERIC }

    public static Kind classify(String path) {
        if (path != null && path.startsWith("/api/news/feed/")) return Kind.FEED_LIST;
        return Kind.GENERIC;
    }

    /**
     * A well-formed, friendly fallback body.
     *
     * FEED_LIST: an empty-but-valid feed page. The app renders it as an empty
     *   channel ("暂无更多内容") rather than a blank screen: data present, zero
     *   items, has_more false, a real message field.
     * GENERIC: a success-shaped envelope with an empty data object.
     *
     * The exact schema the app's parser requires is not fully pinned without a real
     * response sample; these use the fields common to the TT feed/base_resp
     * envelopes and are the starting point to validate on a stable board.
     */
    public static String fallbackJson(Kind kind) {
        long now = System.currentTimeMillis() / 1000L;
        if (kind == Kind.FEED_LIST) {
            return "{\"data\":[],\"has_more\":false,\"message\":\"success\","
                 + "\"tips\":{\"display_info\":\"暂无更多内容\"},"
                 + "\"feed_flag\":0,\"login_status\":0,"
                 + "\"base_resp\":{\"status_code\":0,\"status_message\":\"success\"},"
                 + "\"now\":" + now + "}";
        }
        return "{\"data\":{},\"message\":\"success\","
             + "\"base_resp\":{\"status_code\":0,\"status_message\":\"success\"}}";
    }

    /**
     * A complete HTTP/1.1 friendly response, for any injection point operating at
     * the HTTP/1.1 layer. (The app negotiates h2 for these hosts, so the primary
     * delivery path is an h2 single-stream response synthesised from fallbackJson;
     * that builder and its wiring are specified in docs/DEGRADATION.md and gated
     * off pending on-board validation. This h1 form is provided for completeness
     * and for any endpoint that falls back to HTTP/1.1.)
     */
    public static byte[] http11Response(Kind kind) {
        byte[] body;
        try {
            body = fallbackJson(kind).getBytes("UTF-8");
        } catch (Exception e) {
            body = fallbackJson(kind).getBytes();
        }
        String head = "HTTP/1.1 200 OK\r\n"
                + "content-type: application/json; charset=utf-8\r\n"
                + "content-length: " + body.length + "\r\n"
                + "x-wl-degrade: 1\r\n"
                + "connection: close\r\n\r\n";
        byte[] h;
        try {
            h = head.getBytes("US-ASCII");
        } catch (Exception e) {
            h = head.getBytes();
        }
        byte[] out = new byte[h.length + body.length];
        System.arraycopy(h, 0, out, 0, h.length);
        System.arraycopy(body, 0, out, h.length, body.length);
        return out;
    }

    // ---------------------------------------------------------------------------

    /** Minimal SharedPreferences XML reader: <string name="KEY">VALUE</string>. */
    private static String readSpString(String key) {
        for (int i = 0; i < APPLOG_STATS_SP.length; i++) {
            String v = readSpString(APPLOG_STATS_SP[i], key);
            if (v != null) return v;
        }
        return null;
    }

    private static String readSpString(String path, String key) {
        File f = new File(path);
        if (!f.isFile() || !f.canRead()) return null;
        InputStream in = null;
        try {
            in = new FileInputStream(f);
            byte[] buf = new byte[(int) Math.min(f.length(), 1 << 20)];
            int n = 0, r;
            while (n < buf.length && (r = in.read(buf, n, buf.length - n)) > 0) n += r;
            String xml = new String(buf, 0, n, "UTF-8");
            String open = "<string name=\"" + key + "\">";
            int a = xml.indexOf(open);
            if (a < 0) return null;
            a += open.length();
            int b = xml.indexOf("</string>", a);
            if (b < 0) return null;
            return xml.substring(a, b);
        } catch (Throwable t) {
            return null;
        } finally {
            if (in != null) try { in.close(); } catch (Throwable ignored) { }
        }
    }
}
