package adapter.core;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * The fixed answers of in-process system services, in one list: service, method, value. A service
 * method whose answer needs no code belongs here, with the reason beside it; a method that keeps
 * state or computes its answer stays in LocalServiceBinders. Anything not listed there or here
 * answers the type default.
 *
 * Values: true, false, a whole number, or "empty" for an empty list. A value that does not fit the
 * method's return type is not answered: it is reported and the method falls through to its default.
 * The idea comes from GrapheneOS's gmscompat_config, which keeps its stubs as a table.
 */
final class ServiceAnswers {
    static final Object NOT_LISTED = new Object();

    private static final String[][] TABLE = {
        // uimode: an ordinary phone, not in car mode. expo-device reads the type while a React
        // Native bundle loads.
        {"uimode", "getCurrentModeType", "1"},               // UI_MODE_TYPE_NORMAL
        {"uimode", "getNightModeCustomType", "-1"},          // MODE_NIGHT_CUSTOM_TYPE_UNKNOWN
        {"uimode", "getAttentionModeThemeOverlay", "1000"},  // MODE_ATTENTION_THEME_OVERLAY_OFF
        {"uimode", "getProjectingPackages", "empty"},

        // power: interactive, without power saving. Wake locks are accepted.
        {"power", "isInteractive", "true"},
        {"power", "isDisplayInteractive", "true"},
        {"power", "isWakeLockLevelSupported", "true"},

        // audio
        {"audio", "requestAudioFocus", "1"},                 // AUDIOFOCUS_REQUEST_GRANTED
        {"audio", "requestAudioFocusForTest", "1"},
        {"audio", "abandonAudioFocus", "1"},
        {"audio", "abandonAudioFocusForTest", "1"},
        {"audio", "getUiSoundsStreamType", "2"},             // STREAM_RING: STREAM_SYSTEM's alias on phones

        // device_policy: a personal phone nobody manages. Everything else answers the default
        // (camera allowed, no password requirement, no lock-task packages); CameraX casts the
        // manager non-null before it lists cameras. Storage encryption answers the default,
        // unsupported, since the board sets no fscrypt policy.
        {"device_policy", "isDeviceProvisioned", "true"},
        {"device_policy", "isUsbDataSignalingEnabled", "true"},

        // usagestats: without usage access every query answers empty, and the caller, running in
        // the foreground, is in the ACTIVE bucket. linphone asks for its bucket while it starts.
        {"usagestats", "getAppStandbyBucket", "10"},         // STANDBY_BUCKET_ACTIVE

        // account: no account database and no authenticator, so "signed in nowhere". Wikipedia
        // never null-checks AccountManager, so the binder has to exist though it owns nothing.
        {"account", "addAccountExplicitly", "false"},
        {"account", "removeAccountExplicitly", "false"},
        {"account", "someUserHasAccount", "false"},
        {"account", "accountAuthenticated", "false"},
    };

    private static final Map<String, String> VALUES = new HashMap<>();
    private static final Set<String> sReported = new HashSet<>();

    static {
        for (String[] row : TABLE) VALUES.put(row[0] + "." + row[1], row[2]);
    }

    private ServiceAnswers() {}

    /** The listed answer for a call, or NOT_LISTED. */
    static Object lookup(String service, String method, Class<?> returnType) {
        String key = service + "." + method;
        String value = VALUES.get(key);
        if (value == null) return NOT_LISTED;
        Object answer = convert(value, returnType);
        if (answer == NOT_LISTED) {
            synchronized (sReported) {
                if (sReported.add(key)) {
                    System.err.println("[WESTLAKE-LOCAL-SERVICE] table value " + value + " does not fit "
                            + key + " returning " + returnType);
                }
            }
        }
        return answer;
    }

    private static Object convert(String value, Class<?> type) {
        try {
            switch (value) {
                case "true":
                case "false":
                    return type == boolean.class ? (Object) Boolean.valueOf(value) : NOT_LISTED;
                case "empty":
                    return type == java.util.List.class ? new ArrayList<>() : NOT_LISTED;
                default:
                    if (type == int.class) return Integer.valueOf(value);
                    if (type == long.class) return Long.valueOf(value);
                    return NOT_LISTED;
            }
        } catch (NumberFormatException e) {
            return NOT_LISTED;
        }
    }
}
