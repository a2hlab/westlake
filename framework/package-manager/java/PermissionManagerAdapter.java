package adapter.packagemanager;

import android.app.ActivityThread;
import android.app.Application;
import android.content.AttributionSourceState;
import android.content.pm.*;
import android.content.pm.permission.SplitPermissionInfoParcelable;
import android.permission.IPermissionManager;
import android.permission.IOnPermissionsChangeListener;
import java.util.Collections;
import java.util.List;

/** Android permission queries over the current OH application token.
 * This facade never grants permissions or treats a missing OH permission as a
 * grant. Privileged mutations require a real permission-controller workflow.
 */
public final class PermissionManagerAdapter extends IPermissionManager.Stub {
    // Explicit OH enforcement avoids the generated constructor's dependency
    // on ActivityThread's system Context during service bootstrap.
    private PermissionManagerAdapter() { super(OHPermissionEnforcer.getInstance()); }
    private static final class Holder {
        static final PermissionManagerAdapter INSTANCE = new PermissionManagerAdapter();
    }
    public static PermissionManagerAdapter getInstance() { return Holder.INSTANCE; }
    /** Permission results must not use the launcher's generic integer-zero default. */
    public static Integer proxyPermissionResult(java.lang.reflect.Method method, Object[] args) {
        if ("checkPermission".equals(method.getName()) && args != null && args.length == 3
                && args[1] instanceof Integer && args[2] instanceof Integer) {
            return trace("am.checkPermission", (String) args[0], (Integer) args[2],
                    checkProcessPermission((String) args[0], (Integer) args[1], (Integer) args[2]));
        }
        if ("checkUidPermission".equals(method.getName()) && args != null && args.length == 2)
            return trace("am.checkUidPermission", (String) args[0], (Integer) args[1],
                    checkProcessPermission((String) args[0], -1, (Integer) args[1]));
        // Android 15's Context.checkPermission asks this one. Unhandled, it fell to the stub's int default, 0:
        // granted, for every permission and uid. Kept until one rule replaces all three answers (see rule).
        if ("checkPermissionForDevice".equals(method.getName()) && args != null && args.length == 4
                && args[2] instanceof Integer) {
            return trace("am.checkPermissionForDevice", (String) args[0], (Integer) args[2],
                    PackageManager.PERMISSION_GRANTED);
        }
        return null;
    }

    /**
     * What one rule, Android's protection levels, would answer whatever API asked (ADR-0004 decision 1), and why:
     * "granted: <why>" or "denied: <why>". The uid's package (this process's own, or the one Westlake gave a
     * client's uid) must request or define the permission. Then a normal permission, and a signature permission
     * whose declarer shares the package's signer, is granted; a dangerous one is what OH granted the host app,
     * the user's runtime grant, which every Westlake process shares; a platform-signature, internal or foreign
     * signature permission is denied, as for any app Android installs; and so is one no package this process
     * knows defines. Today the activity manager's checkPermission denies every other uid, its
     * checkPermissionForDevice grants everything, and the package manager answers by request alone: trace()
     * logs where they differ from this.
     */
    static String rule(String permission, int uid) {
        if (permission == null) return "denied: no permission named";
        // No Westlake app is platform-signed: a platform signature or internal permission is denied whoever asks,
        // before the application or its package is readable (ContextImpl asks for STATUS_BAR_SERVICE that early).
        String platform = adapter.core.PermissionLevels.platformDenial(permission);
        if (platform != null) return "denied: " + platform;
        Application app = ActivityThread.currentApplication();
        PackageManager manager = app != null ? app.getPackageManager() : null;
        // A check made outside a binder call names the kernel uid, which every Westlake process shares; in this
        // process it means the process itself.
        boolean own = uid == android.os.Process.myUid() || uid == android.system.Os.getuid();
        String pkg = own ? (app != null ? app.getPackageName() : null) : RemotePackages.packageForUid(uid);
        if (pkg == null || manager == null) return "unknown: no package for the uid";
        Boolean requested = requestsOrDefines(manager, pkg, permission, own);
        if (requested == null) return "unknown: " + pkg + " unreadable";
        if (!requested) return "denied: not requested by " + pkg;
        if (POST_NOTIFICATIONS.equals(permission)) {
            return postNotifications() == PackageManager.PERMISSION_GRANTED
                    ? "granted: notification switch on" : "denied: notification switch off";
        }
        String level = adapter.core.PermissionLevels.classify(manager, permission, pkg);
        switch (level) {
            case "normal":
                return "granted: normal";
            case "signature":
                return "granted: signature shared";
            case "dangerous":
                return Native.granted(PermissionMapper.mapToOH(permission))
                        ? "granted: dangerous, granted on OH" : "denied: dangerous, not granted on OH";
            default:
                return "denied: " + level;
        }
    }

    /** Whether pkg requests or defines permission: this process's own package, or another one Westlake knows. */
    /** Whether pkg requests or defines permission; null when this process cannot read its own package. */
    private static Boolean requestsOrDefines(PackageManager manager, String pkg, String permission, boolean own) {
        if (!own) return RemotePackages.holdsPermission(pkg, permission);
        try {
            PackageInfo info = manager.getPackageInfo(pkg, PackageManager.GET_PERMISSIONS);
            if (info.requestedPermissions != null) {
                for (String requested : info.requestedPermissions) {
                    if (permission.equals(requested)) return true;
                }
            }
            if (info.permissions != null) {
                for (PermissionInfo defined : info.permissions) {
                    if (defined != null && permission.equals(defined.name)) return true;
                }
            }
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            return null;   // not a package this process can read: no answer, rather than "not requested"
        }
        return false;
    }

    private static final java.util.Set<String> TRACED = java.util.concurrent.ConcurrentHashMap.newKeySet();
    // The rule may ask another process about a package, so it never runs on the checking thread.
    private static final java.util.concurrent.ExecutorService TRACER =
            java.util.concurrent.Executors.newSingleThreadExecutor(r -> {
                Thread t = new Thread(r, "permission-check-trace");
                t.setDaemon(true);
                return t;
            });

    /**
     * Logs a check once per API, permission and uid, with the rule's answer where it differs ("[WL-PERM-CHECK]"),
     * and returns the answer unchanged. The rule is worked out on its own thread.
     */
    /**
     * WESTLAKE_PERMISSION_LEVELS=enforce: phase A of ADR-0004 decision 1. A check gets the rule's answer where the
     * rule reaches one Android gives whatever the user does: normal, signature, not requested, platform or foreign
     * signature, internal, undefined. A dangerous permission and the notification switch keep today's answer until
     * a runtime request reaches OH's dialog; so does a check the rule cannot answer ("unknown"). Unset, checks are
     * only traced.
     */
    private static final boolean ENFORCE = "enforce".equals(System.getenv("WESTLAKE_PERMISSION_LEVELS"));
    /** The rule's reason per permission and uid, once known; non-dangerous answers do not change while a process runs. */
    private static final java.util.Map<String, String> RULES = new java.util.concurrent.ConcurrentHashMap<>();

    /** The answer phase A gives, or null to keep today's. Worked out on the checking thread, then cached. */
    private static Integer enforced(String permission, int uid) {
        String key = permission + " " + uid;
        String rule = RULES.get(key);
        if (rule == null) {
            try {
                rule = rule(permission, uid);
            } catch (RuntimeException | LinkageError e) {
                return null;
            }
            if (rule.startsWith("unknown")) return null;   // perhaps readable later: not cached
            RULES.put(key, rule);
        }
        if (rule.contains("dangerous") || rule.contains("notification switch")) return null;
        if (rule.startsWith("granted:")) return PackageManager.PERMISSION_GRANTED;
        if (rule.startsWith("denied:")) return PackageManager.PERMISSION_DENIED;
        return null;
    }

    static int trace(String api, String permission, int uid, int today) {
        Integer phaseA = ENFORCE && permission != null ? enforced(permission, uid) : null;
        int answer = phaseA != null ? phaseA : today;
        if (permission != null && TRACED.add(api + " " + permission + " " + uid)) {
            Throwable caller = new Throwable();   // where the check came from, printed where the rule differs
            TRACER.execute(() -> {
                awaitApplication(uid);
                String given = answer == PackageManager.PERMISSION_GRANTED ? "granted" : "denied";
                String rule;
                try {
                    rule = rule(permission, uid);
                } catch (RuntimeException | LinkageError e) {
                    rule = given + ": rule failed, " + e.getClass().getSimpleName();
                }
                String was = today == PackageManager.PERMISSION_GRANTED ? "granted" : "denied";
                System.err.println("[WL-PERM-CHECK] " + api + " " + permission + " uid " + uid + ": " + given
                        + (was.equals(given) ? "" : " (enforced; was " + was + ")")
                        + (rule.startsWith(given + ":") ? "" : " (one rule: " + rule + ")" + callers(caller)));
            });
        }
        return answer;
    }

    /**
     * A check this process makes about itself can come before its application exists (ContextImpl asks for
     * STATUS_BAR_SERVICE during start-up), when the rule cannot name the package yet: wait for it, up to 10 s.
     */
    private static void awaitApplication(int uid) {
        if (uid != android.os.Process.myUid() && uid != android.system.Os.getuid()) return;
        for (int i = 0; i < 100 && ActivityThread.currentApplication() == null; i++) {
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return;
            }
        }
    }

    /** The frames that asked, past this class and reflection: " from A.m:1 < B.n:2 ...", at most 10. */
    private static String callers(Throwable caller) {
        StringBuilder out = new StringBuilder();
        int shown = 0;
        for (StackTraceElement frame : caller.getStackTrace()) {
            String cls = frame.getClassName();
            if (cls.equals(PermissionManagerAdapter.class.getName()) || cls.startsWith("java.lang.reflect.")
                    || cls.startsWith("$Proxy") || cls.startsWith("jdk.internal.")) {
                continue;
            }
            out.append(shown == 0 ? " from " : " < ").append(cls.substring(cls.lastIndexOf('.') + 1))
                    .append('.').append(frame.getMethodName()).append(':').append(frame.getLineNumber());
            if (++shown == 10) break;
        }
        return out.toString();
    }
    public static int checkProcessPermission(String permission, int pid, int uid) {
        if (permission == null || uid != android.os.Process.myUid()
                || (pid != -1 && pid != android.os.Process.myPid())) return PackageManager.PERMISSION_DENIED;
        if (POST_NOTIFICATIONS.equals(permission)) return postNotifications();
        return Native.granted(PermissionMapper.mapToOH(permission))
                ? PackageManager.PERMISSION_GRANTED : PackageManager.PERMISSION_DENIED;
    }

    private static final String POST_NOTIFICATIONS = "android.permission.POST_NOTIFICATIONS";

    /**
     * POST_NOTIFICATIONS is no OH permission: OH keeps a per-application notification switch that
     * the user turns on (adapter.core.NotificationSwitch). Its mapping to NOTIFICATION_CONTROLLER,
     * a system permission, read as denied for every app whatever the user had allowed.
     */
    public static int postNotifications() {
        return adapter.core.NotificationSwitch.enabled()
                ? PackageManager.PERMISSION_GRANTED : PackageManager.PERMISSION_DENIED;
    }
    private static final class Native {
        static { System.load(System.getenv("WESTLAKE_PERMISSION_HELPER_PATH")); }
        static native int flags(String permission);
        static native boolean granted(String permission);
    }
    private static boolean isOwn(String pkg, int user) {
        Application app = ActivityThread.currentApplication();
        return app != null && app.getPackageName().equals(pkg) && app.getUserId() == user;
    }
    private static void own(String pkg, int user) {
        if (!isOwn(pkg, user))
            throw new SecurityException("Permission query is not for the current OH application");
    }
    private static SecurityException controllerRequired() {
        return new SecurityException("Permission changes require the OH permission controller");
    }
    public int getPermissionFlags(String pkg, String permission, int user) {
        own(pkg, user);
        int flags = Native.flags(PermissionMapper.mapToOH(permission));
        if (flags < 0) return 0; // Unknown/unrequested permission has no recorded user decision.
        int result = flags & 3; // USER_SET and USER_FIXED have the same bit values.
        if ((flags & 4) != 0) result |= PackageManager.FLAG_PERMISSION_SYSTEM_FIXED;
        if ((flags & (32 | 128)) != 0) result |= PackageManager.FLAG_PERMISSION_POLICY_FIXED;
        if ((flags & 8) != 0) result |= PackageManager.FLAG_PERMISSION_GRANTED_BY_DEFAULT;
        if ((flags & 64) != 0) result |= PackageManager.FLAG_PERMISSION_ONE_TIME;
        return result;
    }
    public boolean shouldShowRequestPermissionRationale(String pkg, String permission, int user) {
        own(pkg, user);
        String mapped = PermissionMapper.mapToOH(permission);
        if (Native.granted(mapped)) return false;
        int flags = getPermissionFlags(pkg, permission, user);
        return (flags & PackageManager.FLAG_PERMISSION_USER_SET) != 0
                && (flags & (PackageManager.FLAG_PERMISSION_USER_FIXED
                | PackageManager.FLAG_PERMISSION_SYSTEM_FIXED | PackageManager.FLAG_PERMISSION_POLICY_FIXED)) == 0;
    }
    public boolean isPermissionRevokedByPolicy(String pkg, String permission, int user) {
        return (getPermissionFlags(pkg, permission, user) & PackageManager.FLAG_PERMISSION_POLICY_FIXED) != 0;
    }
    // No Android-defined custom permission groups or split permissions are
    // registered by this boundary. Do not fabricate platform metadata.
    public ParceledListSlice getAllPermissionGroups(int flags) { return new ParceledListSlice(Collections.emptyList()); }
    public PermissionGroupInfo getPermissionGroupInfo(String name, int flags) { return null; }
    public PermissionInfo getPermissionInfo(String name, String pkg, int flags) { return null; }
    public ParceledListSlice queryPermissionsByGroup(String group, int flags) { return new ParceledListSlice(Collections.emptyList()); }
    public List<SplitPermissionInfoParcelable> getSplitPermissions() { return Collections.emptyList(); }
    public List<String> getAllowlistedRestrictedPermissions(String pkg, int flags, int user) { own(pkg,user); return Collections.emptyList(); }
    public List<String> getAutoRevokeExemptionGrantedPackages(int user) { return Collections.emptyList(); }
    public List<String> getAutoRevokeExemptionRequestedPackages(int user) { return Collections.emptyList(); }
    public boolean isAutoRevokeExempted(String pkg, int user) { own(pkg,user); return false; }
    public boolean isRegisteredAttributionSource(AttributionSourceState source) { return false; }
    public android.os.IBinder registerAttributionSource(AttributionSourceState source) { throw controllerRequired(); }
    // Android never refuses a listener. OH offers no subscription, so this one never hears a change.
    public void addOnPermissionsChangeListener(IOnPermissionsChangeListener listener) {}
    public void removeOnPermissionsChangeListener(IOnPermissionsChangeListener listener) {}
    public boolean addPermission(PermissionInfo permission, boolean async) { throw controllerRequired(); }
    public void removePermission(String name) { throw controllerRequired(); }
    public void grantRuntimePermission(String pkg, String permission, int user) { throw controllerRequired(); }
    public void revokeRuntimePermission(String pkg, String permission, int user, String reason) { throw controllerRequired(); }
    public void revokePostNotificationPermissionWithoutKillForTest(String pkg, int user) { throw controllerRequired(); }
    public void updatePermissionFlags(String pkg, String permission, int mask, int values, boolean check, int user) { throw controllerRequired(); }
    public void updatePermissionFlagsForAllApps(int mask, int values, int user) { throw controllerRequired(); }
    public boolean addAllowlistedRestrictedPermission(String pkg, String permission, int flags, int user) { throw controllerRequired(); }
    public boolean removeAllowlistedRestrictedPermission(String pkg, String permission, int flags, int user) { throw controllerRequired(); }
    public boolean setAutoRevokeExempted(String pkg, boolean exempt, int user) { throw controllerRequired(); }
    public void startOneTimePermissionSession(String pkg, int user, long timeout, long killedDelay) { throw controllerRequired(); }
    public void stopOneTimePermissionSession(String pkg, int user) { throw controllerRequired(); }

    // Android 15 service boundary. Unsupported OH capabilities fail explicitly.
    @Override
    public int getRegisteredAttributionSourceCount(int uid) throws android.os.RemoteException {
        return 0;
    }

    @Override
    public int checkPermission(java.lang.String packageName, java.lang.String permissionName, java.lang.String persistentDeviceId, int userId) throws android.os.RemoteException {
        // Android answers for any package and never throws here: another package, which this
        // process cannot see installed, holds nothing (Messenger's Firebase checked one and died on
        // the SecurityException).
        if (RemotePackages.contains(packageName)) {
            return RemotePackages.holdsPermission(packageName, permissionName)
                    ? PackageManager.PERMISSION_GRANTED : PackageManager.PERMISSION_DENIED;
        }
        if (!isOwn(packageName, userId)) return PackageManager.PERMISSION_DENIED;
        if (!android.companion.virtual.VirtualDeviceManager.PERSISTENT_DEVICE_ID_DEFAULT.equals(persistentDeviceId)) return PackageManager.PERMISSION_DENIED;
        return checkProcessPermission(permissionName, -1, android.os.Process.myUid());
    }

    @Override
    public int checkUidPermission(int uid, java.lang.String permissionName, int deviceId) throws android.os.RemoteException {
        String remote = RemotePackages.packageForUid(uid);
        if (remote != null) {
            return trace("pm.checkUidPermission", permissionName, uid, RemotePackages.holdsPermission(remote, permissionName)
                    ? PackageManager.PERMISSION_GRANTED : PackageManager.PERMISSION_DENIED);
        }
        return trace("pm.checkUidPermission", permissionName, uid,
                deviceId == 0 ? checkProcessPermission(permissionName, -1, uid) : PackageManager.PERMISSION_DENIED);
    }

    @Override
    public int getPermissionFlags(java.lang.String packageName, java.lang.String permissionName, java.lang.String persistentDeviceId, int userId) throws android.os.RemoteException {
        requireDefaultDevice(persistentDeviceId);
        return getPermissionFlags(packageName, permissionName, userId);
    }

    @Override
    public void updatePermissionFlags(java.lang.String packageName, java.lang.String permissionName, int flagMask, int flagValues, boolean checkAdjustPolicyFlagPermission, java.lang.String persistentDeviceId, int userId) throws android.os.RemoteException {
        throw controllerRequired();
    }

    @Override
    public void grantRuntimePermission(java.lang.String packageName, java.lang.String permissionName, java.lang.String persistentDeviceId, int userId) throws android.os.RemoteException {
        throw controllerRequired();
    }

    @Override
    public void revokeRuntimePermission(java.lang.String packageName, java.lang.String permissionName, java.lang.String persistentDeviceId, int userId, java.lang.String reason) throws android.os.RemoteException {
        throw controllerRequired();
    }

    @Override
    public boolean shouldShowRequestPermissionRationale(java.lang.String packageName, java.lang.String permissionName, int deviceId, int userId) throws android.os.RemoteException {
        if (deviceId != 0) throw new SecurityException("Unknown OH virtual device");
        return shouldShowRequestPermissionRationale(packageName, permissionName, userId);
    }

    @Override
    public boolean isPermissionRevokedByPolicy(java.lang.String packageName, java.lang.String permissionName, int deviceId, int userId) throws android.os.RemoteException {
        if (deviceId != 0) throw new SecurityException("Unknown OH virtual device");
        return isPermissionRevokedByPolicy(packageName, permissionName, userId);
    }

    @Override
    public void startOneTimePermissionSession(java.lang.String packageName, int deviceId, int userId, long timeout, long revokeAfterKilledDelay) throws android.os.RemoteException {
        throw controllerRequired();
    }

    @Override
    public java.util.Map<java.lang.String,android.permission.PermissionManager.PermissionState> getAllPermissionStates(java.lang.String packageName, java.lang.String persistentDeviceId, int userId) throws android.os.RemoteException {
        own(packageName, userId);
        requireDefaultDevice(persistentDeviceId);
        java.util.Map<String, android.permission.PermissionManager.PermissionState> states = new java.util.HashMap<>();
        PackageInfo info = PackageManagerAdapter.getInstance().getPackageInfo(packageName, PackageManager.GET_PERMISSIONS, userId);
        if (info != null && info.requestedPermissions != null) {
            for (String permission : info.requestedPermissions) {
                states.put(permission, new android.permission.PermissionManager.PermissionState(checkProcessPermission(permission, -1, android.os.Process.myUid()) == PackageManager.PERMISSION_GRANTED, getPermissionFlags(packageName, permission, userId)));
            }
        }
        return states;
    }
    private static void requireDefaultDevice(String device) {
        if (!android.companion.virtual.VirtualDeviceManager.PERSISTENT_DEVICE_ID_DEFAULT.equals(device))
            throw new SecurityException("Unknown OH virtual device");
    }
}
