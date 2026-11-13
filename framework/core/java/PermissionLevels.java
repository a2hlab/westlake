package adapter.core;

import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageParser;
import android.content.pm.PermissionInfo;
import android.content.pm.Signature;

import java.io.File;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * What Android's protection levels would answer when an app asks for a permission held by another app's
 * caller (ADR-0004 decision 1). Westlake answers that a caller holds every permission its package
 * requests, whatever the level, and the requests come from what the client says. Android grants a
 * normal or dangerous permission as requested, a signature permission only to a package signed like
 * the one that declares it, and an internal one by role or installer rules Westlake does not have.
 *
 * Until the levels are enforced, this only logs ([WL-PERM]), once for each package and permission, a
 * request the levels would deny or a permission whose definition this process does not know, so that a
 * round shows what enforcing them would change. It works off the binder thread that asked.
 */
final class PermissionLevels {
    private static final String TAG = "[WL-PERM] ";
    /** Packages whose declared permissions a Play host or an app is likely to meet besides its own. */
    private static final String[] PLAY_PACKAGES = {
            "com.google.android.gms", "com.google.android.gsf", "com.android.vending"};
    private static final Set<String> sSeen = new HashSet<>();
    private static final ExecutorService sWorker = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "wl-permission-levels");
        thread.setDaemon(true);
        return thread;
    });
    /** The platform's permissions, from framework-res; read once. */
    private static Map<String, PermissionInfo> sPlatform;

    private PermissionLevels() {}

    /** Logs, once per package and permission, a requested permission the levels would deny. */
    static void requested(PackageManager manager, String permission, String requester, String self) {
        if (manager == null || permission == null || requester == null) return;
        synchronized (sSeen) {
            if (!sSeen.add(requester + " " + permission)) return;
        }
        sWorker.execute(() -> {
            String verdict;
            try {
                verdict = verdict(manager, permission, requester, self);
            } catch (RuntimeException | LinkageError e) {
                verdict = "unknown: " + e;
            }
            if (!"grant".equals(verdict)) {
                System.err.println(TAG + permission + " for " + requester + ": " + verdict
                        + "; Westlake grants it as requested");
            }
        });
    }

    private static String verdict(PackageManager manager, String permission, String requester, String self) {
        PermissionInfo info = platform().get(permission);
        if (info == null) info = declared(manager, permission, requester, self);
        if (info == null) return "undefined here";
        int flags = info.getProtectionFlags();
        switch (info.getProtection()) {
            case PermissionInfo.PROTECTION_NORMAL:
            case PermissionInfo.PROTECTION_DANGEROUS:
                return "grant";
            case PermissionInfo.PROTECTION_SIGNATURE:
                if ("android".equals(info.packageName)) {
                    return "would deny (platform signature" + flagText(flags) + ")";
                }
                return sameSigner(manager, info.packageName, requester)
                        ? "grant"
                        : "would deny (signature of " + info.packageName + flagText(flags) + ")";
            case PermissionInfo.PROTECTION_INTERNAL:
                return "would deny (internal" + flagText(flags) + ")";
            default:
                return "undefined level " + info.getProtection();
        }
    }

    private static String flagText(int flags) {
        StringBuilder text = new StringBuilder();
        if ((flags & PermissionInfo.PROTECTION_FLAG_PRIVILEGED) != 0) text.append("|privileged");
        if ((flags & PermissionInfo.PROTECTION_FLAG_DEVELOPMENT) != 0) text.append("|development");
        if ((flags & PermissionInfo.PROTECTION_FLAG_APPOP) != 0) text.append("|appop");
        if ((flags & PermissionInfo.PROTECTION_FLAG_INSTALLER) != 0) text.append("|installer");
        if ((flags & PermissionInfo.PROTECTION_FLAG_ROLE) != 0) text.append("|role");
        return text.toString();
    }

    /** The permission as a package this process knows declares it: the requester, itself or a Play package. */
    private static PermissionInfo declared(PackageManager manager, String permission, String requester, String self) {
        Set<String> candidates = new HashSet<>(Arrays.asList(PLAY_PACKAGES));
        candidates.add(requester);
        if (self != null) candidates.add(self);
        for (String candidate : candidates) {
            try {
                PackageInfo info = manager.getPackageInfo(candidate, PackageManager.GET_PERMISSIONS);
                if (info == null || info.permissions == null) continue;
                for (PermissionInfo declared : info.permissions) {
                    if (declared != null && permission.equals(declared.name)) return declared;
                }
            } catch (PackageManager.NameNotFoundException | RuntimeException e) {
                // not a package this process knows
            }
        }
        return null;
    }

    /** Whether the two packages share a current signer, as far as this process knows them. */
    private static boolean sameSigner(PackageManager manager, String declarer, String requester) {
        Signature[] a = signers(manager, declarer);
        Signature[] b = signers(manager, requester);
        if (a == null || b == null) return false;
        Set<Signature> set = new HashSet<>(Arrays.asList(a));
        for (Signature signature : b) {
            if (set.contains(signature)) return true;
        }
        return false;
    }

    private static Signature[] signers(PackageManager manager, String packageName) {
        try {
            PackageInfo info = manager.getPackageInfo(packageName, PackageManager.GET_SIGNING_CERTIFICATES);
            return info == null || info.signingInfo == null ? null : info.signingInfo.getApkContentsSigners();
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            return null;
        }
    }

    private static synchronized Map<String, PermissionInfo> platform() {
        if (sPlatform != null) return sPlatform;
        Map<String, PermissionInfo> permissions = new HashMap<>();
        String path = System.getenv("WESTLAKE_FRAMEWORK_RES_PATH");
        try {
            if (path != null) {
                PackageParser.Package parsed = new PackageParser().parsePackage(new File(path), 0);
                for (PackageParser.Permission permission : parsed.permissions) {
                    if (permission != null && permission.info != null) {
                        permissions.put(permission.info.name, permission.info);
                    }
                }
            }
            System.err.println(TAG + permissions.size() + " platform permissions read from framework-res");
        } catch (PackageParser.PackageParserException | RuntimeException e) {
            System.err.println(TAG + "platform permissions unread: " + e);
        }
        sPlatform = permissions;
        return permissions;
    }
}
