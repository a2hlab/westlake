// Presence-only Conscrypt marker for okhttp's Android platform selection.
//
// Westlake's TLS implementation is the real JSSE path over libssl_openssl.z.so; there is no
// Conscrypt implementation to expose.  Okhttp only resolves this type and then probes an object
// for a field of this type, so class presence is sufficient and construction must stay explicit.
package com.android.org.conscrypt;

public class SSLParametersImpl {
    private SSLParametersImpl() {
        throw new UnsupportedOperationException(
                "westlake §676: presence-only stub for okhttp platform detection; not a real "
                        + "Conscrypt SSLParametersImpl. TLS goes through JSSE (§441).");
    }
}
