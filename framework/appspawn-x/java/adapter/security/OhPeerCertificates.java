package adapter.security;

import java.io.ByteArrayInputStream;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import javax.net.ssl.SSLPeerUnverifiedException;

/** Parse the authenticated peer's concatenated DER without hiding parse failures. */
public final class OhPeerCertificates {
    private OhPeerCertificates() {}
    public static Certificate[] decode(byte[] der) throws SSLPeerUnverifiedException {
        try {
            if (der == null || der.length == 0 || der.length > 4 * 1024 * 1024)
                throw new IllegalArgumentException("Missing or oversized peer chain");
            CertificateFactory factory = CertificateFactory.getInstance("X.509", "BC");
            ByteArrayInputStream input = new ByteArrayInputStream(der);
            ArrayList<Certificate> chain = new ArrayList<>();
            while (input.available() > 0) {
                int previous = input.available();
                Certificate cert = factory.generateCertificate(input);
                if (!(cert instanceof X509Certificate) || cert.getPublicKey() == null
                        || input.available() >= previous || chain.size() >= 64)
                    throw new IllegalArgumentException("Invalid peer chain");
                chain.add(cert);
            }
            return chain.toArray(new Certificate[chain.size()]);
        } catch (Exception failure) {
            SSLPeerUnverifiedException error = new SSLPeerUnverifiedException("Cannot decode peer chain");
            error.initCause(failure);
            throw error;
        }
    }
}
