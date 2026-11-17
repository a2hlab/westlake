package adapter.core;

/**
 * Widens Play services' own uncertified-device exception list (opt-in, pinned GMS build). The added services
 * pass Play services' uncertified-device check although Google still treats the device as uncertified: a local
 * workaround of Google's device policy, not certification. The project owner approved it on 2026-11-17 for a
 * fixed set of services (docs/google-auth-broker.md); adding any other service needs the owner's sign-off.
 */
public final class GoogleIdentityServicePolicy {
    public static final String KEY = "uncertified_exceptions_whitelist";
    // Default service exceptions in the tested 244034029 build; the value lives in Phenotype preferences.
    private static final String DEFAULT = "151,130,40,25,257,185,51,218";
    private GoogleIdentityServicePolicy() {}

    public static String adapt(String key, String value) {
        if (!KEY.equals(key)) return value;
        String existing = value == null ? DEFAULT : value;
        if (existing.length() > 4096 || (!existing.isEmpty() && !existing.matches("[0-9]+(?:,[0-9]+)*")))
            return value;
        // 212 supplies the chooser; 213 is its internal identity service after account selection.
        for (String required : new String[] { "212", "213" }) {
            if (!java.util.Arrays.asList(existing.split(",")).contains(required))
                existing = existing.isEmpty() ? required : existing + "," + required;
        }
        return existing;
    }
}
