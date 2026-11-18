import java.lang.reflect.Method;

/** Read-only ART check for the origin shown by the in-app web-link dialog. */
public final class WebLinkOriginProbe {
    public static String query() throws Exception {
        Method origin = Class.forName("adapter.activity.WebLinkDialog").getDeclaredMethod("webOrigin", String.class);
        origin.setAccessible(true);
        check(origin, "HTTPS://EXAMPLE.COM:443/login?sample=private#fragment", "https://example.com");
        check(origin, "http://example.com:80/page", "http://example.com");
        check(origin, "https://example.com:8443/page", "https://example.com:8443");
        check(origin, "http://[2001:db8::1]:8080/", "http://[2001:db8::1]:8080");
        check(origin, "https://name@example.com/", null);
        check(origin, "https://example.com:65536/", null);
        check(origin, "https://example.com:invalid/", null);
        check(origin, "https:///missing-host", null);
        check(origin, "about:blank", null);
        check(origin, "/relative", null);
        check(origin, null, null);
        return "Web link origin checks passed.\n";
    }

    private static void check(Method method, String url, String expected) throws Exception {
        String actual = (String) method.invoke(null, (Object) url);
        if (expected == null ? actual != null : !expected.equals(actual)) {
            throw new AssertionError("Unexpected web origin");
        }
    }
}
