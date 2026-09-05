/*
 * WlTlsBootstrap.java
 *
 * Stands up a real JSSE provider and hands it to WlTlsGate.
 *
 * Split of responsibility
 * -----------------------
 * WlTlsGate is the framework part: pure source, and it never learns where a
 * provider came from. This class is the deployment part: it loads one out of a
 * jar that the operator puts on the device. The jar itself is NOT in this
 * repository -- see the note at the bottom -- and nothing here hard-codes a
 * board layout beyond a documented, overridable search list.
 *
 * Why a separate jar at all
 * -------------------------
 * The adapter has no Conscrypt (libconscrypt_jni.so does not exist on OH), and
 * AppSpawnXInit prunes both Conscrypt entries from Security.props. A pure-Java
 * JSSE is therefore the only thing that can complete a handshake in this
 * process today. The eventual answer is an in-tree Conscrypt replacement with
 * its own JNI, at which point this class stops being the interesting supplier;
 * WlTlsGate does not need to change when that happens.
 *
 * Contract with the jar
 * ---------------------
 * The jar must contain a class named by BOOTSTRAP_CLASS with three static
 * methods:
 *
 *     String        install()        -- build the provider; returns a status line
 *     Provider      jsseProvider()   -- the provider install() built
 *     X509TrustManager trustManager() -- a usable trust manager, or null
 *
 * and optionally
 *
 *     String selfTest(String host, int port, String url)
 *
 * Timing
 * ------
 * install() is deliberately off the calling thread. Loading several thousand
 * classes and standing up a provider costs seconds even on a desktop JVM, and
 * this board runs -Xint, so doing it inline would add that straight onto
 * process attach. The app's own network stack does not come up until well into
 * its init chain, tens of seconds later, so the background load comfortably
 * wins the race -- and callers that arrive early block in WlTlsGate rather than
 * receiving a broken context.
 *
 * The gate is released in a finally block whatever happens. A supplier that
 * fails must still settle the gate, or every caller waits out the full timeout
 * for an answer that is never coming.
 */
package adapter.security;

import java.io.File;
import java.security.Provider;
import java.util.Arrays;

import javax.net.ssl.X509TrustManager;

public final class WlTlsBootstrap {

    private static final String TAG = "[WL-TLS]";

    /** Entry point this bootstrap expects to find inside the jar. */
    private static final String BOOTSTRAP_CLASS = "westlake.tls.TlsBootstrap";

    /**
     * Where the JSSE jar may have been deployed, in order of preference.
     *
     * Deployment configuration, not a build input: override with the system
     * property westlake.tls.jar to point somewhere else entirely.
     */
    private static final String JAR_PROPERTY = "westlake.tls.jar";
    private static final String[] JAR_CANDIDATES = {
        "/data/local/tmp/wl-tls.jar",
        "/system/android/framework/wl-tls.jar",
    };

    /** Scratch directories for the dex cache, best first. */
    private static final String[] DEX_CACHE_CANDIDATES = {
        "/data/local/tmp/wl-dexcache",
        null,                                   // filled in from java.io.tmpdir
    };

    /** Opt-in end-to-end proof, so a normal run never pays for it. */
    private static final String SELF_TEST_MARKER = "/data/local/tmp/wl-tls-selftest";

    private static volatile boolean sStarted;

    private WlTlsBootstrap() { }

    /**
     * Take over the shim provider now, and load the real one in the background.
     *
     * Idempotent. Safe to call on the attach path: the synchronous half is only
     * map writes.
     */
    public static void start() {
        if (sStarted) {
            return;
        }
        sStarted = true;

        // First and inline: until this has run, any app code asking for TLS gets
        // the adapter's broken shim and caches the result forever.
        WlTlsGate.hijackShimProvider();

        Thread thread = new Thread(WlTlsBootstrap::load, "wl-tls-bootstrap");
        thread.setDaemon(true);
        thread.start();
        System.err.println(TAG + " bootstrap thread armed");
    }

    private static void load() {
        long start = System.currentTimeMillis();
        Provider provider = null;
        X509TrustManager trustManager = null;
        try {
            String jar = findJar();
            if (jar == null) {
                System.err.println(TAG + " no JSSE jar found; looked at "
                        + Arrays.toString(JAR_CANDIDATES)
                        + " and -D" + JAR_PROPERTY);
                return;
            }
            ClassLoader parent = WlTlsBootstrap.class.getClassLoader();
            // Reflective because dalvik.system.DexClassLoader does not exist on a
            // host JVM, and this file is compiled by host javac as part of the
            // framework. The device runtime always has it.
            Class<?> loaderClass = Class.forName("dalvik.system.DexClassLoader");
            ClassLoader loader = (ClassLoader) loaderClass
                    .getConstructor(String.class, String.class, String.class, ClassLoader.class)
                    .newInstance(jar, dexCacheDir(), null, parent);

            Class<?> bootstrap = loader.loadClass(BOOTSTRAP_CLASS);
            String status = (String) bootstrap.getMethod("install").invoke(null);
            provider = (Provider) bootstrap.getMethod("jsseProvider").invoke(null);
            trustManager = (X509TrustManager) bootstrap.getMethod("trustManager").invoke(null);
            System.err.println(TAG + " install: " + status
                    + " loadMs=" + (System.currentTimeMillis() - start));
            maybeSelfTest(bootstrap);
        } catch (Throwable t) {
            System.err.println(TAG + " bootstrap failed after "
                    + (System.currentTimeMillis() - start) + "ms: " + t);
            t.printStackTrace();
        } finally {
            // Always settle the gate: a caller blocked in awaitProvider() must
            // get a clear failure rather than hang for the full timeout.
            WlTlsGate.publish(provider, trustManager);
        }
    }

    private static String findJar() {
        String override = System.getProperty(JAR_PROPERTY);
        if (override != null && isReadableFile(override)) {
            return override;
        }
        for (String candidate : JAR_CANDIDATES) {
            if (isReadableFile(candidate)) {
                return candidate;
            }
        }
        return null;
    }

    private static boolean isReadableFile(String path) {
        File file = new File(path);
        return file.isFile() && file.canRead();
    }

    /** A writable scratch dir for the dex cache, or null to let ART decide. */
    private static String dexCacheDir() {
        for (String candidate : DEX_CACHE_CANDIDATES) {
            String path = candidate != null ? candidate : System.getProperty("java.io.tmpdir");
            if (path == null) {
                continue;
            }
            File dir = new File(path);
            if (dir.isDirectory() ? dir.canWrite() : dir.mkdirs()) {
                return dir.getAbsolutePath();
            }
        }
        return null;
    }

    private static void maybeSelfTest(Class<?> bootstrap) {
        if (!new File(SELF_TEST_MARKER).isFile()) {
            return;
        }
        try {
            String result = (String) bootstrap
                    .getMethod("selfTest", String.class, int.class, String.class)
                    .invoke(null, "example.com", 443, "https://example.com/");
            System.err.println(TAG + " selftest: " + result);
        } catch (Throwable t) {
            System.err.println(TAG + " selftest failed: " + t);
        }
    }
}
