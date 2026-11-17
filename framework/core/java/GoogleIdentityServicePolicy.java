package adapter.core;

/** Opt-in local service eligibility adaptation for the separately pinned GMS build, not certification. */
public final class GoogleIdentityServicePolicy {
    public static final String KEY = "uncertified_exceptions_whitelist";
    // Modern chooser, its identity dependency, and legacy Google Sign-In.
    // Eligibility does not grant accounts, tokens, scopes, or an integrity verdict.
    static final java.util.List<String> REQUIRED = java.util.List.of("212", "213", "91");
    // Default service exceptions in the tested 244034029 build; the value lives in Phenotype preferences.
    private static final String DEFAULT = "151,130,40,25,257,185,51,218";
    private GoogleIdentityServicePolicy() {}

    public static String adapt(String key, String value) {
        if (!KEY.equals(key)) return value;
        String existing = value == null ? DEFAULT : value;
        if (existing.length() > 4096 || (!existing.isEmpty() && !existing.matches("[0-9]+(?:,[0-9]+)*")))
            return value;
        for (String required : REQUIRED) {
            if (!java.util.Arrays.asList(existing.split(",")).contains(required))
                existing = existing.isEmpty() ? required : existing + "," + required;
        }
        return existing;
    }
}
