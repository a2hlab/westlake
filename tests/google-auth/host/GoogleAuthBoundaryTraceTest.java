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
        requests();
        fingerprint();
        System.out.println("GOOGLE_AUTH_BOUNDARY_TRACE_TEST_PASS");
    }

    private static void requests() {
        String drive = GoogleAuthBoundaryTrace.requestService("oauth2:https://www.googleapis.com/auth/drive.readonly openid");
        require(drive.contains("scope_categories=drive_readonly,openid") && drive.contains("unknown_scope_count=0"));
        require(GoogleAuthBoundaryTrace.requestService("oauth2: https://www.googleapis.com/auth/drive.readonly")
                .contains("empty_scope_count=1"));
        require(GoogleAuthBoundaryTrace.requestService("writely").equals("request_kind=writely"));
        require(GoogleAuthBoundaryTrace.requestService("^^_account_id_^^").equals("request_kind=account_id"));
        require(GoogleAuthBoundaryTrace.requestService("https://docs.google.com/feeds")
                .equals("request_kind=docs_feed"));
        require(GoogleAuthBoundaryTrace.requestService("https://docs.google.com/feeds/")
                .equals("request_kind=docs_feed_slash"));
        require(GoogleAuthBoundaryTrace.requestService("oauth2:https://www.googleapis.com/auth/notifications")
                .contains("scope_categories=notifications unknown_scope_count=0"));
        require(GoogleAuthBoundaryTrace.requestService("oauth2:https://www.googleapis.com/auth/drive.apps.readonly")
                .contains("scope_categories=drive_apps_readonly unknown_scope_count=0"));
        require(GoogleAuthBoundaryTrace.requestService("https://docs.google.com/feeds/private@example.invalid")
                .equals("request_kind=other"));
        require(GoogleAuthBoundaryTrace.requestService(null).equals("request_kind=null"));
        require(GoogleAuthBoundaryTrace.requestService("x".repeat(4097)).equals("request_kind=oversize"));
        for (String privateValue : new String[] {"private@example.invalid", "ya29.DO_NOT_LOG", "private\nINJECT",
                "https://private.invalid/secret", "123-private.apps.googleusercontent.com"}) {
            for (String prefix : new String[] {"", "oauth2:", "audience:server:client_id:", "oauth2:server:client_id:"}) {
                String result = GoogleAuthBoundaryTrace.requestService(prefix + privateValue);
                require(!result.contains(privateValue) && !result.contains("\n"));
            }
            String result = GoogleAuthBoundaryTrace.requestExtras(java.util.Arrays.asList("clientPackageName", privateValue));
            require(result.equals("extras_categories=clientPackageName unknown_extra_count=1"));
        }
        require(GoogleAuthBoundaryTrace.requestService("oauth2:email?secret=ya29.DO_NOT_LOG")
                .equals("request_kind=oauth2 scope_categories=email unknown_scope_count=0 empty_scope_count=0"));
        require(GoogleAuthBoundaryTrace.requestExtras(null).equals("extras_categories=none unknown_extra_count=0"));
    }

    private static byte[] tokenParcel(String token) {
        byte[] text = token.getBytes(java.nio.charset.StandardCharsets.UTF_16LE);
        int fieldSize = (4 + text.length + 2 + 3) & ~3;
        java.nio.ByteBuffer data = java.nio.ByteBuffer.allocate(24 + fieldSize)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN);
        data.putInt(0xffff4f45).putInt(data.capacity() - 8);
        data.putInt(0x00040001).putInt(1); // unrelated version field
        data.putInt(0xffff0002).putInt(fieldSize).putInt(token.length()).put(text).putShort((short) 0);
        return data.array();
    }

    private static void fingerprint() {
        String token = "private.claims.signature";
        byte[] encoded = tokenParcel(token);
        String result = GoogleAuthBoundaryTrace.tokenFingerprint(encoded);
        try {
            byte[] hash = java.security.MessageDigest.getInstance("SHA-256").digest(token.getBytes("UTF-8"));
            String expected = new java.math.BigInteger(1, hash).toString(16);
            while (expected.length() < 64) expected = "0" + expected;
            require(result.equals("token_fingerprint_sha256=" + expected));
        } catch (Exception e) { throw new AssertionError(e); }
        require(!result.contains(token));
        for (int n = 0; n < encoded.length; n++) unavailable(java.util.Arrays.copyOf(encoded, n));
        unavailable(null); unavailable(new byte[65537]); unavailable(tokenParcel("private@example.invalid"));
        byte[] bad = encoded.clone(); bad[0] = 0; unavailable(bad);
        bad = encoded.clone(); bad[20] = (byte) 255; unavailable(bad); // field length overflow
        bad = encoded.clone(); bad[24] = (byte) 255; unavailable(bad); // string length overflow
        bad = encoded.clone(); bad[28 + token.length() * 2] = 1; unavailable(bad); // no terminator
        byte[] duplicate = java.util.Arrays.copyOf(encoded, encoded.length + encoded.length - 16);
        System.arraycopy(encoded, 16, duplicate, encoded.length, encoded.length - 16);
        java.nio.ByteBuffer.wrap(duplicate).order(java.nio.ByteOrder.LITTLE_ENDIAN).putInt(4, duplicate.length - 8);
        unavailable(duplicate);
    }

    private static void unavailable(byte[] encoded) {
        require(GoogleAuthBoundaryTrace.tokenFingerprint(encoded).equals("token_fingerprint_unavailable=true"));
    }
    private static void require(boolean ok) { if (!ok) throw new AssertionError(); }
}
