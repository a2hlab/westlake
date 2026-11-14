package adapter.core;

import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PermissionInfo;
import android.content.pm.Signature;
import android.content.res.ApkAssets;
import android.content.res.XmlResourceParser;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

import java.io.IOException;
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
public final class PermissionLevels {
    private static final String TAG = "[WL-PERM] ";
    private static final String ANDROID_NAMESPACE = "http://schemas.android.com/apk/res/android";
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

    /**
     * The protection class Android would apply to permission for requester: "normal", "dangerous",
     * "signature" (granted: the requester shares the declarer's signer), "deny <why>" (platform signature,
     * another package's signature, internal) or "undefined" (no definition this process knows).
     */
    public static String classify(PackageManager manager, String permission, String requester) {
        PermissionInfo info = platform().get(permission);
        if (info == null) info = declared(manager, permission, requester, null);
        if (info == null) return "undefined";
        int flags = info.getProtectionFlags();
        switch (info.getProtection()) {
            case PermissionInfo.PROTECTION_NORMAL:
                return "normal";
            case PermissionInfo.PROTECTION_DANGEROUS:
                return "dangerous";
            case PermissionInfo.PROTECTION_SIGNATURE:
                if ("android".equals(info.packageName)) return "deny platform signature" + flagText(flags);
                return sameSigner(manager, info.packageName, requester)
                        ? "signature" : "deny signature of " + info.packageName + flagText(flags);
            case PermissionInfo.PROTECTION_INTERNAL:
                return "deny internal" + flagText(flags);
            default:
                return "undefined level " + info.getProtection();
        }
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
            if (path != null) readDeclared(path, "android", permissions);
            System.err.println(TAG + permissions.size() + " platform permissions read from framework-res");
        } catch (IOException | XmlPullParserException | RuntimeException e) {
            System.err.println(TAG + "platform permissions unread: " + e);
        }
        sPlatform = permissions;
        return permissions;
    }

    /**
     * The permissions an APK's manifest declares, read straight from its binary XML. PackageParser cannot
     * read framework-res: its manifest asks for SDK 35, and Westlake reports 34, so the parser refuses it
     * before reaching a permission.
     */
    private static void readDeclared(String apk, String packageName, Map<String, PermissionInfo> out)
            throws IOException, XmlPullParserException {
        ApkAssets assets = ApkAssets.loadFromPath(apk);
        try (XmlResourceParser parser = assets.openXml("AndroidManifest.xml")) {
            for (int type = parser.next(); type != XmlPullParser.END_DOCUMENT; type = parser.next()) {
                if (type != XmlPullParser.START_TAG || !"permission".equals(parser.getName())) continue;
                String name = parser.getAttributeValue(ANDROID_NAMESPACE, "name");
                if (name == null) continue;
                PermissionInfo info = new PermissionInfo();
                info.name = name;
                info.packageName = packageName;
                info.group = parser.getAttributeValue(ANDROID_NAMESPACE, "permissionGroup");
                info.protectionLevel = PermissionInfo.fixProtectionLevel(parser.getAttributeIntValue(
                        ANDROID_NAMESPACE, "protectionLevel", PermissionInfo.PROTECTION_NORMAL));
                out.put(name, info);
            }
        } finally {
            assets.close();
        }
    }
}
