package adapter.core;

import adapter.packagemanager.SourcePackageRegistry;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.util.AtomicFile;
import com.android.internal.util.XmlUtils;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Properties;

/** Reversible identity-service eligibility experiment; applied before the main GMS Application binds. */
public final class GoogleIdentityServiceConfig {
    private GoogleIdentityServiceConfig() {}

    public static void prepare(ApplicationInfo app) {
        if (!"com.google.android.gms".equals(app.packageName)
                || GoogleAuthProxy.provider()) return;
        Path journal = Path.of(app.dataDir, "westlake-identity-service-compat", "original.properties");
        boolean enabled = "1".equals(System.getenv("WESTLAKE_GOOGLE_IDENTITY_SERVICE_COMPAT"))
                && GoogleAuthDelegation.enabled();
        if (!enabled && !Files.exists(journal, LinkOption.NOFOLLOW_LINKS)) return;
        try {
            PackageInfo info = SourcePackageRegistry.packageInfo(app.packageName, 0);
            if (info == null || info.getLongVersionCode() != 244034029L)
                throw new SecurityException("unsupported GMS version");
            Path path = Path.of(app.deviceProtectedDataDir, "shared_prefs", "gms_chimera_phenotype_flags.xml");
            if (Files.isSymbolicLink(path) || Files.isSymbolicLink(path.getParent())
                    || Files.isSymbolicLink(journal) || Files.isSymbolicLink(journal.getParent()))
                throw new SecurityException("unsafe config path");
            AtomicFile file = new AtomicFile(path.toFile());
            Map<String, Object> values = new HashMap<>();
            if (file.exists()) try (FileInputStream in = file.openRead()) { values.putAll(XmlUtils.readMapXml(in)); }
            Object current = values.get(GoogleIdentityServicePolicy.KEY);
            if (current != null && !(current instanceof String)) throw new SecurityException("unsupported flag type");
            Properties saved = new Properties();
            if (Files.exists(journal)) try (FileInputStream in = new FileInputStream(journal.toFile())) { saved.load(in); }
            if (!saved.isEmpty() && (!"1".equals(saved.getProperty("version"))
                    || saved.getProperty("applied") == null || !saved.containsKey("original.present")))
                throw new SecurityException("invalid config journal");
            if (enabled) {
                String adapted = GoogleIdentityServicePolicy.adapt(GoogleIdentityServicePolicy.KEY, (String) current);
                if (adapted == null || !java.util.Arrays.asList(adapted.split(",")).containsAll(
                        java.util.Arrays.asList("212", "213")))
                    throw new SecurityException("unsupported flag value");
                if (!Objects.equals(current, adapted)) {
                    // Preserve the original across a compatibility update. A new Google value
                    // instead becomes the rollback target, so we do not restore stale configuration.
                    if (saved.isEmpty() || !Objects.equals(current, saved.getProperty("applied"))) {
                        saved.clear(); saved.setProperty("version", "1");
                        saved.setProperty("original.present", Boolean.toString(current != null));
                        if (current != null) saved.setProperty("original", (String) current);
                    }
                    saved.setProperty("applied", adapted);
                    PrivateFiles.writePrivate(journal, saved);
                    values.put(GoogleIdentityServicePolicy.KEY, adapted);
                    write(file, path, values);
                }
                System.err.println("[WL-GOOGLE-POLICY] identity_service_compat=true services=212,213 store=phenotype");
            } else {
                if (saved.isEmpty()) throw new SecurityException("empty config journal");
                // A later Google update owns its new value; do not overwrite it with the older snapshot.
                if (Objects.equals(current, saved.getProperty("applied"))) {
                    if ("true".equals(saved.getProperty("original.present"))) {
                        if (!saved.containsKey("original")) throw new SecurityException("missing original value");
                        values.put(GoogleIdentityServicePolicy.KEY, saved.getProperty("original"));
                    } else if ("false".equals(saved.getProperty("original.present"))) values.remove(GoogleIdentityServicePolicy.KEY);
                    else throw new SecurityException("invalid original presence");
                    write(file, path, values);
                }
                Files.delete(journal);
                System.err.println("[WL-GOOGLE-POLICY] identity_service_compat=false restored=true");
            }
        } catch (Exception error) {
            throw new SecurityException("Google identity compatibility preparation failed: " + error.getClass().getSimpleName());
        }
    }

    private static void write(AtomicFile file, Path path, Map<String, Object> values) throws Exception {
        Files.createDirectories(path.getParent());
        FileOutputStream out = file.startWrite();
        try { XmlUtils.writeMapXml(values, out); file.finishWrite(out); }
        catch (Exception error) { file.failWrite(out); throw error; }
        Files.setPosixFilePermissions(path, PosixFilePermissions.fromString("rw-------"));
    }
}
