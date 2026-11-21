package adapter.core;

/** Opt-in local service eligibility adaptation for the separately pinned GMS build, not certification. */
public final class GoogleIdentityServicePolicy {
    public static final String KEY = "uncertified_exceptions_whitelist";
    // Modern chooser, its identity dependency, legacy Google Sign-In and its SignIn.API dependency.
    // Gmail's Contacts client also requires People.API_1P (5) and Feedback.API (29).
    // The Identity Authorization API (219) exchanges a chooser sign-in for an authorization (WEBTOON's
    // server auth code); without it the call fails with SERVICE_INVALID after a successful sign-in.
    // MdiSync (215) is Play services' own account-details sync. Without it the sign-in UI builds the
    // credential from partial data, naming the account by its numeric ID, which the Authorization API
    // then refuses as an account the app cannot see.
    // Eligibility does not grant accounts, tokens, scopes, or an integrity verdict.
    static final java.util.List<String> REQUIRED = java.util.List.of("212", "213", "91", "44", "5", "29", "219", "215");
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
