package adapter.core;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.math.BigInteger;
import java.security.InvalidAlgorithmParameterException;
import java.security.Key;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.KeyPairGeneratorSpi;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.KeyStoreSpi;
import java.security.PrivateKey;
import java.security.Provider;
import java.security.SecureRandom;
import java.security.Security;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.ECGenParameterSpec;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.RSAKeyGenParameterSpec;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.function.Supplier;

import javax.crypto.KeyGeneratorSpi;
import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;
import javax.security.auth.x500.X500Principal;

/**
 * Android's "AndroidKeyStore" JCA provider, answered in process with software keys.
 *
 * <p>Android's zygote installs android.security.keystore2.AndroidKeyStoreProvider, whose keys live
 * in the keystore2 system service. Direct launch has neither the zygote step nor keystore2, and
 * apps open the provider by name at startup (expo-secure-store, androidx.security, biometric
 * crypto, device-attestation SDKs): without it {@code KeyStore.getInstance("AndroidKeyStore")}
 * throws "AndroidKeyStore not found".
 *
 * <p>This supplies the same names over ordinary software keys, kept per app in its no_backup
 * directory: the KeyStore (aliases, entries without a password, delete), KeyGenerator (AES, HMAC)
 * and KeyPairGenerator (EC, RSA) configured by KeyGenParameterSpec. The keys work with the default
 * Cipher, Mac and Signature providers. They are not hardware-backed, purposes and user
 * authentication are not enforced, and a key pair's certificate is self-signed with no
 * attestation: OH HUKS is the backend that would give them that.
 */
public final class SoftwareAndroidKeyStore extends Provider {
    public static final String NAME = "AndroidKeyStore";
    private static final String TAG = "[WL-KEYSTORE] ";
    private static final Map<String, Entry> ENTRIES = new LinkedHashMap<>();
    private static File sFile;
    private static boolean sLoaded;

    /** Installs the provider once per process; keys persist under {@code directory}. */
    public static synchronized void install(File directory) {
        if (Security.getProvider(NAME) != null) {
            return;
        }
        sFile = directory == null ? null : new File(directory, "westlake-android-keystore");
        Security.addProvider(new SoftwareAndroidKeyStore());
        System.err.println(TAG + "installed software " + NAME + " provider, keys in " + sFile);
    }

    private SoftwareAndroidKeyStore() {
        super(NAME, 1.0, "Westlake software AndroidKeyStore (keys in app data, not hardware-backed)");
        service("KeyStore", NAME, Store.class, Store::new);
        for (String algorithm : new String[] {KeyProperties.KEY_ALGORITHM_AES, KeyProperties.KEY_ALGORITHM_HMAC_SHA1,
                KeyProperties.KEY_ALGORITHM_HMAC_SHA224, KeyProperties.KEY_ALGORITHM_HMAC_SHA256,
                KeyProperties.KEY_ALGORITHM_HMAC_SHA384, KeyProperties.KEY_ALGORITHM_HMAC_SHA512}) {
            service("KeyGenerator", algorithm, SecretKeyGenerator.class, () -> new SecretKeyGenerator(algorithm));
        }
        for (String algorithm : new String[] {KeyProperties.KEY_ALGORITHM_EC, KeyProperties.KEY_ALGORITHM_RSA}) {
            service("KeyPairGenerator", algorithm, PairGenerator.class, () -> new PairGenerator(algorithm));
        }
    }

    private void service(String type, String algorithm, Class<?> implementation, Supplier<Object> factory) {
        putService(new Service(this, type, algorithm, implementation.getName(), null, null) {
            @Override
            public Object newInstance(Object parameter) {
                return factory.get();
            }
        });
    }

    // ---- the per-app store ----------------------------------------------------------------------

    private static final class Entry {
        final SecretKey secret;
        final PrivateKey privateKey;
        final Certificate[] chain;   // the key pair's chain, or the single trusted certificate
        final Date created;

        Entry(SecretKey secret, PrivateKey privateKey, Certificate[] chain, Date created) {
            this.secret = secret;
            this.privateKey = privateKey;
            this.chain = chain;
            this.created = created;
        }
    }

    private static synchronized Entry get(String alias) {
        load();
        return alias == null ? null : ENTRIES.get(alias);
    }

    private static synchronized void put(String alias, Entry entry) {
        load();
        ENTRIES.put(alias, entry);
        save();
    }

    private static synchronized boolean remove(String alias) {
        load();
        boolean removed = ENTRIES.remove(alias) != null;
        if (removed) {
            save();
        }
        return removed;
    }

    private static synchronized ArrayList<String> aliases() {
        load();
        return new ArrayList<>(ENTRIES.keySet());
    }

    private static void load() {
        if (sLoaded) {
            return;
        }
        sLoaded = true;
        if (sFile == null || !sFile.isFile()) {
            return;
        }
        try (DataInputStream in = new DataInputStream(new FileInputStream(sFile))) {
            if (in.readInt() != 1) {
                return;
            }
            CertificateFactory x509 = CertificateFactory.getInstance("X.509");
            for (int count = in.readInt(); count > 0; count--) {
                String alias = in.readUTF();
                int kind = in.readByte();
                Date created = new Date(in.readLong());
                String algorithm = in.readUTF();
                byte[] encoded = bytes(in);
                Certificate[] chain = new Certificate[in.readInt()];
                for (int i = 0; i < chain.length; i++) {
                    chain[i] = x509.generateCertificate(new ByteArrayInputStream(bytes(in)));
                }
                if (kind == 1) {
                    ENTRIES.put(alias, new Entry(new SecretKeySpec(encoded, algorithm), null, null, created));
                } else if (kind == 2) {
                    PrivateKey key = KeyFactory.getInstance(algorithm).generatePrivate(new PKCS8EncodedKeySpec(encoded));
                    ENTRIES.put(alias, new Entry(null, key, chain, created));
                } else {
                    ENTRIES.put(alias, new Entry(null, null, chain, created));
                }
            }
        } catch (Exception e) {
            System.err.println(TAG + "could not read " + sFile + ": " + e);
        }
    }

    private static void save() {
        if (sFile == null) {
            return;
        }
        File temp = new File(sFile.getPath() + ".tmp");
        try {
            sFile.getParentFile().mkdirs();
            try (DataOutputStream out = new DataOutputStream(new FileOutputStream(temp))) {
                out.writeInt(1);
                out.writeInt(ENTRIES.size());
                for (Map.Entry<String, Entry> item : ENTRIES.entrySet()) {
                    Entry entry = item.getValue();
                    Key key = entry.secret != null ? entry.secret : entry.privateKey;
                    out.writeUTF(item.getKey());
                    out.writeByte(entry.secret != null ? 1 : entry.privateKey != null ? 2 : 3);
                    out.writeLong(entry.created.getTime());
                    out.writeUTF(key != null ? key.getAlgorithm() : "");
                    writeBytes(out, key != null ? key.getEncoded() : new byte[0]);
                    Certificate[] chain = entry.chain != null ? entry.chain : new Certificate[0];
                    out.writeInt(chain.length);
                    for (Certificate certificate : chain) {
                        writeBytes(out, certificate.getEncoded());
                    }
                }
            }
            if (!temp.renameTo(sFile)) {
                throw new IOException("rename failed");
            }
        } catch (Exception e) {
            System.err.println(TAG + "could not write " + sFile + ": " + e);
            temp.delete();
        }
    }

    private static byte[] bytes(DataInputStream in) throws IOException {
        byte[] value = new byte[in.readInt()];
        in.readFully(value);
        return value;
    }

    private static void writeBytes(DataOutputStream out, byte[] value) throws IOException {
        out.writeInt(value.length);
        out.write(value);
    }

    /** KeyStore("AndroidKeyStore"): entries are read without a password, as on Android. */
    public static final class Store extends KeyStoreSpi {
        @Override
        public Key engineGetKey(String alias, char[] password) {
            Entry entry = get(alias);
            return entry == null ? null : entry.secret != null ? entry.secret : entry.privateKey;
        }

        @Override
        public Certificate[] engineGetCertificateChain(String alias) {
            Entry entry = get(alias);
            return entry == null || entry.privateKey == null ? null : entry.chain.clone();
        }

        @Override
        public Certificate engineGetCertificate(String alias) {
            Entry entry = get(alias);
            return entry == null || entry.chain == null || entry.chain.length == 0 ? null : entry.chain[0];
        }

        @Override
        public Date engineGetCreationDate(String alias) {
            Entry entry = get(alias);
            return entry == null ? null : new Date(entry.created.getTime());
        }

        @Override
        public void engineSetKeyEntry(String alias, Key key, char[] password, Certificate[] chain) throws KeyStoreException {
            if (key instanceof SecretKey) {
                put(alias, new Entry((SecretKey) key, null, null, new Date()));
            } else if (key instanceof PrivateKey && chain != null && chain.length > 0) {
                put(alias, new Entry(null, (PrivateKey) key, chain.clone(), new Date()));
            } else {
                throw new KeyStoreException("unsupported key entry for " + alias);
            }
        }

        @Override
        public void engineSetKeyEntry(String alias, byte[] key, Certificate[] chain) throws KeyStoreException {
            throw new KeyStoreException("Operation not supported because key encoding is unknown");
        }

        @Override
        public void engineSetCertificateEntry(String alias, Certificate certificate) {
            put(alias, new Entry(null, null, new Certificate[] {certificate}, new Date()));
        }

        @Override
        public void engineDeleteEntry(String alias) {
            remove(alias);
        }

        @Override
        public Enumeration<String> engineAliases() {
            return Collections.enumeration(aliases());
        }

        @Override
        public boolean engineContainsAlias(String alias) {
            return get(alias) != null;
        }

        @Override
        public int engineSize() {
            return aliases().size();
        }

        @Override
        public boolean engineIsKeyEntry(String alias) {
            Entry entry = get(alias);
            return entry != null && (entry.secret != null || entry.privateKey != null);
        }

        @Override
        public boolean engineIsCertificateEntry(String alias) {
            Entry entry = get(alias);
            return entry != null && entry.secret == null && entry.privateKey == null;
        }

        @Override
        public String engineGetCertificateAlias(Certificate certificate) {
            for (String alias : aliases()) {
                Entry entry = get(alias);
                if (entry != null && entry.chain != null && entry.chain.length > 0 && entry.chain[0].equals(certificate)) {
                    return alias;
                }
            }
            return null;
        }

        @Override
        public KeyStore.Entry engineGetEntry(String alias, KeyStore.ProtectionParameter protection) {
            Entry entry = get(alias);
            if (entry == null) {
                return null;
            }
            if (entry.secret != null) {
                return new KeyStore.SecretKeyEntry(entry.secret);
            }
            if (entry.privateKey != null) {
                return new KeyStore.PrivateKeyEntry(entry.privateKey, entry.chain.clone());
            }
            return new KeyStore.TrustedCertificateEntry(entry.chain[0]);
        }

        @Override
        public void engineSetEntry(String alias, KeyStore.Entry entry, KeyStore.ProtectionParameter protection)
                throws KeyStoreException {
            if (entry instanceof KeyStore.SecretKeyEntry) {
                engineSetKeyEntry(alias, ((KeyStore.SecretKeyEntry) entry).getSecretKey(), null, null);
            } else if (entry instanceof KeyStore.PrivateKeyEntry) {
                KeyStore.PrivateKeyEntry pair = (KeyStore.PrivateKeyEntry) entry;
                engineSetKeyEntry(alias, pair.getPrivateKey(), null, pair.getCertificateChain());
            } else if (entry instanceof KeyStore.TrustedCertificateEntry) {
                engineSetCertificateEntry(alias, ((KeyStore.TrustedCertificateEntry) entry).getTrustedCertificate());
            } else {
                throw new KeyStoreException("unsupported entry " + entry);
            }
        }

        @Override
        public void engineStore(OutputStream stream, char[] password) {
            throw new UnsupportedOperationException("Can not serialize AndroidKeyStore to OutputStream");
        }

        @Override
        public void engineLoad(InputStream stream, char[] password) {
            if (stream != null) {
                throw new IllegalArgumentException("InputStream not supported");
            }
            synchronized (SoftwareAndroidKeyStore.class) {
                load();
            }
        }

        @Override
        public void engineLoad(KeyStore.LoadStoreParameter parameter) {
            engineLoad((InputStream) null, null);
        }
    }

    // ---- key generation ---------------------------------------------------------------------------

    private static KeyGenParameterSpec requireSpec(AlgorithmParameterSpec params) throws InvalidAlgorithmParameterException {
        if (!(params instanceof KeyGenParameterSpec)) {
            throw new InvalidAlgorithmParameterException("Unsupported params class: "
                    + (params == null ? "null" : params.getClass().getName()) + ". Supported: "
                    + KeyGenParameterSpec.class.getName());
        }
        return (KeyGenParameterSpec) params;
    }

    /** KeyGenerator(AES | HmacSHA*, "AndroidKeyStore"). */
    public static final class SecretKeyGenerator extends KeyGeneratorSpi {
        private final String mAlgorithm;
        private KeyGenParameterSpec mSpec;
        private SecureRandom mRandom;

        SecretKeyGenerator(String algorithm) {
            mAlgorithm = algorithm;
        }

        @Override
        protected void engineInit(SecureRandom random) {
            throw new UnsupportedOperationException("Cannot initialize without a "
                    + KeyGenParameterSpec.class.getName() + " parameter");
        }

        @Override
        protected void engineInit(int keySize, SecureRandom random) {
            engineInit(random);
        }

        @Override
        protected void engineInit(AlgorithmParameterSpec params, SecureRandom random)
                throws InvalidAlgorithmParameterException {
            mSpec = requireSpec(params);
            mRandom = random != null ? random : new SecureRandom();
        }

        @Override
        protected SecretKey engineGenerateKey() {
            if (mSpec == null) {
                throw new IllegalStateException("Not initialized");
            }
            int bits = mSpec.getKeySize();
            if (bits <= 0) {
                bits = KeyProperties.KEY_ALGORITHM_AES.equalsIgnoreCase(mAlgorithm) ? 128 : digestBits(mAlgorithm);
            }
            byte[] material = new byte[(bits + 7) / 8];
            mRandom.nextBytes(material);
            SecretKey key = new SecretKeySpec(material, mAlgorithm);
            put(mSpec.getKeystoreAlias(), new Entry(key, null, null, new Date()));
            return key;
        }

        private static int digestBits(String hmac) {
            String digest = hmac.substring("Hmac".length()).toUpperCase();
            switch (digest) {
                case "SHA1": return 160;
                case "SHA224": return 224;
                case "SHA384": return 384;
                case "SHA512": return 512;
                default: return 256;
            }
        }
    }

    /** KeyPairGenerator(EC | RSA, "AndroidKeyStore"): the pair plus a self-signed certificate. */
    public static final class PairGenerator extends KeyPairGeneratorSpi {
        private final String mAlgorithm;
        private KeyGenParameterSpec mSpec;
        private SecureRandom mRandom;

        PairGenerator(String algorithm) {
            mAlgorithm = algorithm;
        }

        @Override
        public void initialize(int keySize, SecureRandom random) {
            throw new IllegalArgumentException(KeyGenParameterSpec.class.getName() + " required to initialize this KeyPairGenerator");
        }

        @Override
        public void initialize(AlgorithmParameterSpec params, SecureRandom random) throws InvalidAlgorithmParameterException {
            mSpec = requireSpec(params);
            mRandom = random != null ? random : new SecureRandom();
        }

        @Override
        public KeyPair generateKeyPair() {
            if (mSpec == null) {
                throw new IllegalStateException("Not initialized");
            }
            try {
                KeyPairGenerator generator = KeyPairGenerator.getInstance(mAlgorithm);
                AlgorithmParameterSpec shape = mSpec.getAlgorithmParameterSpec();
                boolean ec = KeyProperties.KEY_ALGORITHM_EC.equalsIgnoreCase(mAlgorithm);
                int bits = mSpec.getKeySize() > 0 ? mSpec.getKeySize() : ec ? 256 : 2048;
                if (shape instanceof ECGenParameterSpec || shape instanceof RSAKeyGenParameterSpec) {
                    generator.initialize(shape, mRandom);
                } else if (ec) {
                    generator.initialize(new ECGenParameterSpec(bits == 384 ? "secp384r1" : bits == 521 ? "secp521r1"
                            : bits == 224 ? "secp224r1" : "secp256r1"), mRandom);
                } else {
                    generator.initialize(new RSAKeyGenParameterSpec(bits, RSAKeyGenParameterSpec.F4), mRandom);
                }
                KeyPair pair = generator.generateKeyPair();
                X509Certificate certificate = selfSigned(pair, ec);
                put(mSpec.getKeystoreAlias(), new Entry(null, pair.getPrivate(), new Certificate[] {certificate}, new Date()));
                return pair;
            } catch (Exception e) {
                throw new java.security.ProviderException("Failed to generate key pair", e);
            }
        }

        @SuppressWarnings("deprecation")
        private X509Certificate selfSigned(KeyPair pair, boolean ec) throws Exception {
            com.android.internal.org.bouncycastle.x509.X509V3CertificateGenerator builder =
                    new com.android.internal.org.bouncycastle.x509.X509V3CertificateGenerator();
            X500Principal subject = mSpec.getCertificateSubject() != null
                    ? mSpec.getCertificateSubject() : new X500Principal("CN=Android Keystore Key");
            BigInteger serial = mSpec.getCertificateSerialNumber() != null ? mSpec.getCertificateSerialNumber() : BigInteger.ONE;
            builder.setSerialNumber(serial);
            builder.setSubjectDN(subject);
            builder.setIssuerDN(subject);
            builder.setNotBefore(mSpec.getCertificateNotBefore() != null ? mSpec.getCertificateNotBefore() : new Date(0L));
            builder.setNotAfter(mSpec.getCertificateNotAfter() != null ? mSpec.getCertificateNotAfter()
                    : new Date(2461449600000L));   // 2048-01-01, Android's default
            builder.setPublicKey(pair.getPublic());
            builder.setSignatureAlgorithm(ec ? "SHA256withECDSA" : "SHA256withRSA");
            return builder.generate(pair.getPrivate());
        }
    }
}
