package adapter.packagemanager;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;

import java.io.File;
import java.util.Map;
import java.util.TreeMap;

/** Populates installed APK split paths from the adapter's per-package directory. */
public final class SplitApkResolver {
    private SplitApkResolver() {}

    public static void populate(PackageInfo packageInfo) {
        if (packageInfo == null) return;
        populate(packageInfo, packageInfo.applicationInfo);
    }

    /**
     * Direct launch builds an ApplicationInfo without a surrounding PackageInfo, so the
     * per-package split fields have no carrier. Resolve against the ApplicationInfo alone.
     */
    public static void populate(ApplicationInfo info) {
        populate(null, info);
    }

    private static void populate(PackageInfo packageInfo, ApplicationInfo info) {
        if (info == null) return;
        String basePath = info.sourceDir;
        if (basePath == null || basePath.isEmpty()) return;

        File base = new File(basePath);

        // The launcher knows exactly which splits belong to this app and exports them, which
        // is authoritative where the sibling scan below can only guess. Direct-launch staging
        // puts every app's APKs in one shared directory, and staged names carry the app key as
        // a prefix ("mcdonalds.split_config.arm64_v8a.apk"), so neither the "split_" convention
        // nor a package-named parent identifies them.
        String declared = System.getenv("ASX_SPLIT_APK_PATHS");
        if (declared != null && !declared.isEmpty()) {
            TreeMap<String, String> explicit = new TreeMap<>();
            String basePrefix = base.getName().endsWith(".apk")
                    ? base.getName().substring(0, base.getName().length() - 4) + "."
                    : null;
            for (String path : declared.split(":")) {
                if (path.isEmpty()) continue;
                File candidate = new File(path);
                if (!candidate.isFile()) {
                    System.err.println("[WESTLAKE-PM] declared split missing: " + path);
                    continue;
                }
                String name = candidate.getName();
                if (!name.endsWith(".apk")) continue;
                String splitName = name.substring(0, name.length() - 4);
                // Recover the real split name from the staged "<app>.<split>.apk" form.
                if (basePrefix != null && splitName.startsWith(basePrefix)) {
                    splitName = splitName.substring(basePrefix.length());
                }
                // Android installs a split as split_<name>.apk, and SDKs find theirs by that file
                // name: Facebook's SoLoader looks for a split path ending split_config.<abi>.apk
                // and otherwise reads native libraries from the base APK, which has none.
                if (splitName.startsWith("split_")) {
                    splitName = splitName.substring("split_".length());
                }
                if (!splitName.isEmpty()) explicit.put(splitName, candidate.getAbsolutePath());
            }
            if (!explicit.isEmpty()) {
                apply(packageInfo, info, explicit, "declared");
                return;
            }
        }

        File parent = base.getParentFile();
        File[] siblings = parent != null ? parent.listFiles() : null;
        if (siblings == null || siblings.length == 0) return;

        TreeMap<String, String> splits = new TreeMap<>();
        String baseAbsolute = base.getAbsolutePath();
        String baseName = base.getName();
        String parentPath = parent.getAbsolutePath();
        // A normal Android install directory contains one base.apk and only
        // that package's splits. Direct-launch staging directories are shared
        // by many unrelated test APKs, so siblinghood alone is not evidence of
        // split membership there. In a shared directory accept only standard
        // split filenames; in a package-owned install directory retain the
        // complete sibling scan for vendor-specific split names.
        boolean packageOwnedDirectory = "base.apk".equals(baseName)
                || (info.packageName != null && !info.packageName.isEmpty()
                    && parentPath.contains(info.packageName));
        for (File candidate : siblings) {
            if (candidate == null || !candidate.isFile()) continue;
            String name = candidate.getName();
            if (!name.endsWith(".apk")
                    || candidate.getAbsolutePath().equals(baseAbsolute)) {
                continue;
            }
            boolean conventionalSplit = name.startsWith("split_")
                    || name.startsWith("split-")
                    || name.startsWith("config.");
            if (!packageOwnedDirectory && !conventionalSplit) continue;
            String splitName = name.substring(0, name.length() - 4);
            if (!splitName.isEmpty()) splits.put(splitName, candidate.getAbsolutePath());
        }

        apply(packageInfo, info, splits, "scanned");
    }

    private static void apply(PackageInfo packageInfo, ApplicationInfo info,
            TreeMap<String, String> splits, String source) {
        int count = splits.size();
        String[] names = new String[count];
        String[] paths = new String[count];
        int index = 0;
        for (Map.Entry<String, String> entry : splits.entrySet()) {
            names[index] = entry.getKey();
            paths[index] = entry.getValue();
            index++;
        }

        info.splitNames = names;
        info.splitSourceDirs = paths;
        info.splitPublicSourceDirs = paths.clone();
        if (packageInfo != null) {
            packageInfo.splitNames = names.clone();
            packageInfo.splitRevisionCodes = new int[count];
        }
        System.err.println("[WESTLAKE-PM] split APKs " + info.packageName
                + " source=" + source + " count=" + count
                + " paths=" + java.util.Arrays.toString(paths));
    }
}
