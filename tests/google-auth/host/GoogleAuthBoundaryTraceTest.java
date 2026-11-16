import adapter.core.GoogleAuthBoundaryTrace;

public final class GoogleAuthBoundaryTraceTest {
    public static void main(String[] args) {
        String signer = "0123456789012345678901234567890123456789";
        String prefix = "org.westlake.googlebootstrap.probe:" + signer + ":";
        String probe = GoogleAuthBoundaryTrace.describe("peekAuthToken", prefix + "ac2dm");
        require(probe.contains("owner=probe") && probe.contains("scope_kind=ac2dm"));
        require(GoogleAuthBoundaryTrace.describe("getUserData", "EXP:" + prefix + "ac2dm").contains("key_kind=expiry"));
        for (String secret : new String[] {"private@example.invalid", "ya29.DO_NOT_LOG", "unknown-private-key\nINJECT"}) {
            for (String method : new String[] {"getUserData", "peekAuthToken", "getPassword"}) {
                String result = GoogleAuthBoundaryTrace.describe(method, secret);
                require(!result.contains(secret) && !result.contains("\n"));
            }
            String result = GoogleAuthBoundaryTrace.describe("peekAuthToken",
                    "com.spotify.music:" + signer + ":oauth2:server:client_id:" + secret + ":api_scope:email?private=" + secret);
            require(result.contains("owner=spotify") && result.contains("scope_kind=oauth2_server_client"));
            require(!result.contains(secret) && !result.contains(signer));
        }
        require(GoogleAuthBoundaryTrace.describe("getUserData", null).equals("key_kind=null"));
        String options = GoogleAuthBoundaryTrace.describe("peekAuthToken", prefix
                + "audience:server:client_id:123.apps.googleusercontent.com?include_email=0&include_profile=1&token_request_options=private-value");
        require(options.contains("include_email=0") && options.contains("include_profile=1")
                && options.contains("opaque_options=true") && !options.contains("private-value"));
        require(GoogleAuthBoundaryTrace.responseError("BadAuthentication").equals("BadAuthentication"));
        require(GoogleAuthBoundaryTrace.responseError("private@example.invalid\nINJECT").equals("other"));
        require(GoogleAuthBoundaryTrace.responseError(null).equals("absent"));
        System.out.println("GOOGLE_AUTH_BOUNDARY_TRACE_TEST_PASS");
    }
    private static void require(boolean ok) { if (!ok) throw new AssertionError(); }
}
