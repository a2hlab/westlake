package adapter.packagemanager;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.parsing.ApkLite;
import android.content.pm.parsing.ApkLiteParseUtils;
import android.content.pm.parsing.result.ParseResult;
import android.content.pm.parsing.result.ParseTypeImpl;
import android.util.SparseArray;

import java.io.File;
import java.util.Arrays;
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
            TreeMap<String, Split> explicit = new TreeMap<>();
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
                if (splitName.isEmpty()) continue;
                Split split = Split.read(splitName, candidate);
                explicit.put(split.name, split);
            }
            if (!explicit.isEmpty()) {
                apply(packageInfo, info, explicit, "declared");
                return;
            }
        }

        File parent = base.getParentFile();
        File[] siblings = parent != null ? parent.listFiles() : null;
        if (siblings == null || siblings.length == 0) return;

        TreeMap<String, Split> splits = new TreeMap<>();
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
            if (splitName.isEmpty()) continue;
            Split split = Split.read(splitName, candidate);
            splits.put(split.name, split);
        }

        apply(packageInfo, info, splits, "scanned");
    }

    /**
     * A split as its own manifest declares it: its name (the manifest's split attribute), whether it
     * is a feature split, the split it uses and, for a config split, the split it configures.
     * Android takes all of these from the split manifests, and the staged file name need not carry
     * the name: Chrome's and Brave's bundles name their code split "config.chrome.apk", and named
     * "config.chrome" it was taken for a config split of the base, so Chrome's isolated split
     * loading gave its code no class loader (ClassNotFoundException making SplitChromeApplication).
     * A split whose manifest cannot be read keeps the file name, and its kind comes from Android's
     * naming convention.
     */
    private static final class Split {
        final String name;
        final String path;
        final boolean feature;
        final String usesSplit;
        final String configFor;

        Split(String name, String path, boolean feature, String usesSplit, String configFor) {
            this.name = name;
            this.path = path;
            this.feature = feature;
            this.usesSplit = usesSplit;
            this.configFor = configFor;
        }

        static Split read(String fileName, File file) {
            try {
                ParseResult<ApkLite> result =
                        ApkLiteParseUtils.parseApkLite(ParseTypeImpl.forDefaultParsing().reset(), file, 0);
                if (result.isSuccess()) {
                    ApkLite lite = result.getResult();
                    String name = lite.getSplitName();
                    if (name != null && !name.isEmpty()) {
                        String usesSplit = lite.getUsesSplitName();
                        String configFor = lite.getConfigForSplit();
                        return new Split(name, file.getAbsolutePath(), lite.isFeatureSplit(),
                                usesSplit == null || usesSplit.isEmpty() ? null : usesSplit,
                                configFor == null || configFor.isEmpty() ? null : configFor);
                    }
                }
            } catch (Throwable unreadable) {
                // Fall back to the name and the convention.
            }
            int config = fileName.indexOf(".config.");
            if (fileName.startsWith("config.")) {
                return new Split(fileName, file.getAbsolutePath(), false, null, null);
            }
            if (config > 0) {
                return new Split(fileName, file.getAbsolutePath(), false, null, fileName.substring(0, config));
            }
            return new Split(fileName, file.getAbsolutePath(), true, null, null);
        }
    }

    /**
     * As SplitDependencyLoader.createDependenciesFromPackage makes it, keyed by split index plus one
     * (splits in name order), with the base at 0: the base depends on nothing; a feature split on
     * the split it uses, else the base; and a config split is listed after the dependency of the
     * split it configures, else the base's. LoadedApk walks this whenever the app asks for isolated
     * split loading (Chrome, Play services), and a null array throws while its context is made. A
     * dependency on a split that is not here falls back to the base, where Android would refuse.
     */
    private static SparseArray<int[]> dependencies(Split[] splits) {
        String[] names = new String[splits.length];
        for (int i = 0; i < splits.length; i++) names[i] = splits[i].name;
        SparseArray<int[]> dependencies = new SparseArray<>();
        dependencies.put(0, new int[] {-1});
        for (int i = 0; i < splits.length; i++) {
            if (!splits[i].feature) continue;
            int target = 0;
            if (splits[i].usesSplit != null) {
                int found = Arrays.binarySearch(names, splits[i].usesSplit);
                if (found >= 0 && splits[found].feature) target = found + 1;
            }
            dependencies.put(i + 1, new int[] {target});
        }
        for (int i = 0; i < splits.length; i++) {
            if (splits[i].feature) continue;
            int target = 0;
            if (splits[i].configFor != null) {
                int found = Arrays.binarySearch(names, splits[i].configFor);
                if (found >= 0 && splits[found].feature) target = found + 1;
            }
            int[] current = dependencies.get(target);
            int[] grown = Arrays.copyOf(current, current.length + 1);
            grown[current.length] = i + 1;
            dependencies.put(target, grown);
        }
        return dependencies;
    }

    private static void apply(PackageInfo packageInfo, ApplicationInfo info,
            TreeMap<String, Split> splits, String source) {
        int count = splits.size();
        Split[] ordered = splits.values().toArray(new Split[0]);
        String[] names = new String[count];
        String[] paths = new String[count];
        for (int index = 0; index < count; index++) {
            names[index] = ordered[index].name;
            paths[index] = ordered[index].path;
        }

        info.splitNames = names;
        info.splitSourceDirs = paths;
        info.splitPublicSourceDirs = paths.clone();
        info.splitDependencies = dependencies(ordered);
        // One class loader name per split, null where the split's manifest names none (Chrome's and
        // Brave's name none). LoadedApk reads it by split index when isolated split loading makes a
        // split's class loader, and with no array Chrome's SplitChromeApplication failed there:
        // "Attempt to read from null array" in SplitDependencyLoaderImpl.constructSplit. Names the
        // split manifests gave (SourcePackageRegistry parses them in this same order) are kept.
        if (info.splitClassLoaderNames == null || info.splitClassLoaderNames.length != count) {
            info.splitClassLoaderNames = new String[count];
        }
        if (packageInfo != null) {
            packageInfo.splitNames = names.clone();
            packageInfo.splitRevisionCodes = new int[count];
        }
        System.err.println("[WESTLAKE-PM] split APKs " + info.packageName
                + " source=" + source + " count=" + count
                + " names=" + Arrays.toString(names)
                + " paths=" + Arrays.toString(paths));
    }
}
