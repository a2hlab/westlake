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
        if (packageInfo == null || packageInfo.applicationInfo == null) return;
        ApplicationInfo info = packageInfo.applicationInfo;
        String basePath = info.sourceDir;
        if (basePath == null || basePath.isEmpty()) return;

        File base = new File(basePath);
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
        packageInfo.splitNames = names.clone();
        packageInfo.splitRevisionCodes = new int[count];
        System.err.println("[WESTLAKE-PM] split APKs " + info.packageName
                + " count=" + count + " paths=" + java.util.Arrays.toString(paths));
    }
}
