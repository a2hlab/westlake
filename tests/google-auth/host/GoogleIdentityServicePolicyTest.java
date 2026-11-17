package adapter.core;

public final class GoogleIdentityServicePolicyTest {
    private static int checks;
    private static void eq(String expected, String actual) {
        checks++;
        if (!java.util.Objects.equals(expected, actual)) throw new AssertionError("check " + checks);
    }
    public static void main(String[] args) {
        String key = GoogleIdentityServicePolicy.KEY;
        eq("151,130,40,25,257,185,51,218,212,213,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, null));
        eq("212,213,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, ""));
        eq("51,212,213,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, "51"));
        eq("51,212,213,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, "51,212"));
        eq("51,213,212,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, "51,213"));
        eq("51,212,213,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, "51,212,213"));
        eq("91,51,212,213,44,5,29", GoogleIdentityServicePolicy.adapt(key, "91,51"));
        eq("51,212,213,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, "51,212,213,91"));
        eq("8,91,212,213,44,5,29", GoogleIdentityServicePolicy.adapt(key, "8,91"));
        eq("44,91,212,213,5,29", GoogleIdentityServicePolicy.adapt(key, "44,91"));
        eq("212,213,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, "212,213,91,44"));
        eq("5,29,212,213,91,44", GoogleIdentityServicePolicy.adapt(key, "5,29"));
        eq("212,213,91,44,5,29", GoogleIdentityServicePolicy.adapt(key, "212,213,91,44,5,29"));
        eq("29,5,212,213,91,44", GoogleIdentityServicePolicy.adapt(key, "29,5,212,213,91,44"));
        eq(" 51", GoogleIdentityServicePolicy.adapt(key, " 51"));
        eq("1".repeat(4097), GoogleIdentityServicePolicy.adapt(key, "1".repeat(4097)));
        eq("bad", GoogleIdentityServicePolicy.adapt(key, "bad"));
        eq("51,,8", GoogleIdentityServicePolicy.adapt(key, "51,,8"));
        eq("3", GoogleIdentityServicePolicy.adapt("uncertified_status", "3"));
        eq(null, GoogleIdentityServicePolicy.adapt("uncertified_status", null));
        eq("123", GoogleIdentityServicePolicy.adapt("uncertified_status_expiration_time_ms", "123"));
        eq("true", GoogleIdentityServicePolicy.adapt("enable_uncertified_device_check", "true"));
        eq("googlecertificates", GoogleIdentityServicePolicy.adapt("uncertified_feature_exceptions_whitelist", "googlecertificates"));
        System.out.println("GOOGLE_IDENTITY_SERVICE_POLICY_PASS " + checks);
    }
}
