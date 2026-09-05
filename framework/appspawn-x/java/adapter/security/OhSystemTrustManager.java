/*
 * Copyright (c) 2026 Westlake contributors.
 *
 * OpenHarmony-backed X.509 trust manager for the Android compatibility
 * runtime.  Android normally gets this implementation from Conscrypt's APEX.
 * The OH runtime intentionally omits Conscrypt JNI, so use the platform's
 * existing CA bundle with the standard Java PKIX validator instead.
 */
package adapter.security;

import java.io.BufferedInputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.net.Socket;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.cert.CertPath;
import java.security.cert.CertPathValidator;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.PKIXCertPathValidatorResult;
import java.security.cert.PKIXParameters;
import java.security.cert.TrustAnchor;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import javax.net.ssl.SSLEngine;
import javax.net.ssl.X509ExtendedTrustManager;

/** A PKIX trust manager whose default anchors are the host platform roots. */
public final class OhSystemTrustManager extends X509ExtendedTrustManager {
    private static final String OH_CA_BUNDLE = "/etc/ssl/certs/cacert.pem";

    private final Set<TrustAnchor> trustAnchors;
    private final X509Certificate[] acceptedIssuers;

    public OhSystemTrustManager(KeyStore suppliedStore)
            throws GeneralSecurityException, IOException {
        LinkedHashSet<X509Certificate> roots = new LinkedHashSet<>();
        if (suppliedStore != null) {
            loadKeyStore(suppliedStore, roots);
        } else {
            loadAndroidSystemDirectory(roots);
            if (roots.isEmpty()) {
                loadPemBundle(new File(OH_CA_BUNDLE), roots);
            }
        }
        if (roots.isEmpty() && suppliedStore == null) {
            throw new KeyStoreException("No platform CA certificates are available");
        }

        LinkedHashSet<TrustAnchor> anchors = new LinkedHashSet<>();
        for (X509Certificate root : roots) {
            anchors.add(new TrustAnchor(root, null));
        }
        trustAnchors = Collections.unmodifiableSet(anchors);
        acceptedIssuers = roots.toArray(new X509Certificate[0]);
    }

    private static void loadKeyStore(KeyStore store, Set<X509Certificate> roots)
            throws KeyStoreException {
        Enumeration<String> aliases = store.aliases();
        while (aliases.hasMoreElements()) {
            Certificate cert = store.getCertificate(aliases.nextElement());
            if (cert instanceof X509Certificate) {
                roots.add((X509Certificate) cert);
            }
        }
    }

    private static void loadAndroidSystemDirectory(Set<X509Certificate> roots)
            throws GeneralSecurityException, IOException {
        String androidRoot = System.getenv("ANDROID_ROOT");
        if (androidRoot == null || androidRoot.isEmpty()) {
            return;
        }
        File directory = new File(androidRoot + "/etc/security/cacerts");
        File[] files = directory.listFiles();
        if (files == null) {
            return;
        }
        CertificateFactory factory = CertificateFactory.getInstance("X.509");
        for (File file : files) {
            if (!file.isFile() || file.length() == 0) {
                continue;
            }
            try (BufferedInputStream input =
                         new BufferedInputStream(new FileInputStream(file))) {
                Certificate cert = factory.generateCertificate(input);
                if (cert instanceof X509Certificate) {
                    roots.add((X509Certificate) cert);
                }
            } catch (CertificateException ignored) {
                // Android treats malformed files in the system CA directory as absent.
            }
        }
    }

    private static void loadPemBundle(File bundle, Set<X509Certificate> roots)
            throws GeneralSecurityException, IOException {
        if (!bundle.isFile()) {
            return;
        }
        CertificateFactory factory = CertificateFactory.getInstance("X.509");
        // Android's BouncyCastle CertificateFactory does not reliably parse a
        // concatenated PEM bundle with generateCertificates(): on this runtime
        // it consumes past a certificate boundary and reports "no header
        // found".  Split the standard platform bundle into complete PEM
        // records and feed them through the same single-certificate path used
        // for Android's hashed CA directory.
        byte[] contents;
        try (BufferedInputStream input =
                     new BufferedInputStream(new FileInputStream(bundle));
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
            contents = output.toByteArray();
        }

        String beginMarker = "-----BEGIN CERTIFICATE-----";
        String endMarker = "-----END CERTIFICATE-----";
        int cursor = 0;
        while (true) {
            int begin = findAscii(contents, beginMarker, cursor);
            if (begin < 0) {
                break;
            }
            int end = findAscii(contents, endMarker, begin + beginMarker.length());
            if (end < 0) {
                throw new CertificateException("Unterminated certificate in " + bundle);
            }
            int endExclusive = end + endMarker.length();
            try (ByteArrayInputStream input =
                         new ByteArrayInputStream(contents, begin, endExclusive - begin)) {
                Certificate cert = factory.generateCertificate(input);
                if (cert instanceof X509Certificate) {
                    roots.add((X509Certificate) cert);
                }
            }
            cursor = endExclusive;
        }
    }

    private static int findAscii(byte[] contents, String marker, int start) {
        int lastStart = contents.length - marker.length();
        for (int offset = start; offset <= lastStart; offset++) {
            int index = 0;
            while (index < marker.length()
                    && (contents[offset + index] & 0xff) == marker.charAt(index)) {
                index++;
            }
            if (index == marker.length()) {
                return offset;
            }
        }
        return -1;
    }

    private List<X509Certificate> validate(X509Certificate[] chain, String authType)
            throws CertificateException {
        if (chain == null || chain.length == 0) {
            throw new CertificateException("Server certificate chain is empty");
        }
        if (authType == null || authType.isEmpty()) {
            throw new CertificateException("Authentication type is empty");
        }

        try {
            ArrayList<X509Certificate> pathCertificates = new ArrayList<>();
            Collections.addAll(pathCertificates, chain);

            // A PKIX CertPath excludes its TrustAnchor.  Servers occasionally
            // include the root, so remove it when it exactly matches one of ours.
            if (!pathCertificates.isEmpty()
                    && findMatchingAnchor(pathCertificates.get(pathCertificates.size() - 1))
                    != null) {
                pathCertificates.remove(pathCertificates.size() - 1);
            }
            if (pathCertificates.isEmpty()) {
                chain[0].checkValidity();
                return Collections.singletonList(chain[0]);
            }

            CertificateFactory factory = CertificateFactory.getInstance("X.509");
            CertPath path = factory.generateCertPath(pathCertificates);
            PKIXParameters parameters = new PKIXParameters(trustAnchors);
            // Match Android's default offline behavior. Revocation is handled
            // by network-stack policy rather than requiring CRLs here.
            parameters.setRevocationEnabled(false);
            PKIXCertPathValidatorResult result = (PKIXCertPathValidatorResult)
                    CertPathValidator.getInstance("PKIX").validate(path, parameters);

            ArrayList<X509Certificate> verified = new ArrayList<>(pathCertificates);
            X509Certificate anchor = result.getTrustAnchor().getTrustedCert();
            if (anchor != null
                    && (verified.isEmpty() || !anchor.equals(verified.get(verified.size() - 1)))) {
                verified.add(anchor);
            }
            return verified;
        } catch (GeneralSecurityException e) {
            throw new CertificateException("PKIX certificate validation failed", e);
        }
    }

    private TrustAnchor findMatchingAnchor(X509Certificate certificate) {
        for (TrustAnchor anchor : trustAnchors) {
            X509Certificate trusted = anchor.getTrustedCert();
            if (certificate.equals(trusted)) {
                return anchor;
            }
        }
        return null;
    }

    @Override
    public void checkClientTrusted(X509Certificate[] chain, String authType)
            throws CertificateException {
        validate(chain, authType);
    }

    @Override
    public void checkServerTrusted(X509Certificate[] chain, String authType)
            throws CertificateException {
        validate(chain, authType);
    }

    @Override
    public void checkClientTrusted(X509Certificate[] chain, String authType, Socket socket)
            throws CertificateException {
        validate(chain, authType);
    }

    @Override
    public void checkServerTrusted(X509Certificate[] chain, String authType, Socket socket)
            throws CertificateException {
        validate(chain, authType);
    }

    @Override
    public void checkClientTrusted(X509Certificate[] chain, String authType, SSLEngine engine)
            throws CertificateException {
        validate(chain, authType);
    }

    @Override
    public void checkServerTrusted(X509Certificate[] chain, String authType, SSLEngine engine)
            throws CertificateException {
        validate(chain, authType);
    }

    /** Host-aware Android extension used by Chromium's X509Util. */
    public List<X509Certificate> checkServerTrusted(
            X509Certificate[] chain, String authType, String host)
            throws CertificateException {
        // Chromium performs endpoint identity verification separately.  This
        // method deliberately performs only PKIX chain validation, like
        // Conscrypt's corresponding trust-manager entry point.
        return validate(chain, authType);
    }

    public boolean isSameTrustConfiguration(String firstHost, String secondHost) {
        return true;
    }

    @Override
    public X509Certificate[] getAcceptedIssuers() {
        return acceptedIssuers.clone();
    }
}
