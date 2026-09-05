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
    // IPermissionManager.Stub constructs a PermissionEnforcer using the
    // ActivityThread system context. Proxy permission checks can occur before
    // that context exists, so defer Binder construction until service lookup.
    private static final class Holder {
        static final PermissionManagerAdapter INSTANCE = new PermissionManagerAdapter();
    }
    public static PermissionManagerAdapter getInstance() { return Holder.INSTANCE; }
    /** Permission results must not use the launcher's generic integer-zero default. */
    public static Integer proxyPermissionResult(java.lang.reflect.Method method, Object[] args) {
        if ("checkPermission".equals(method.getName()) && args != null && args.length == 3
                && args[1] instanceof Integer && args[2] instanceof Integer) {
            return checkProcessPermission((String) args[0], (Integer) args[1], (Integer) args[2]);
        }
        if ("checkUidPermission".equals(method.getName()) && args != null && args.length == 2)
            return checkProcessPermission((String) args[0], -1, (Integer) args[1]);
        return null;
    }
    public static int checkProcessPermission(String permission, int pid, int uid) {
        if (permission == null || uid != android.os.Process.myUid()
                || (pid != -1 && pid != android.os.Process.myPid())) return PackageManager.PERMISSION_DENIED;
        return Native.granted(PermissionMapper.mapToOH(permission))
                ? PackageManager.PERMISSION_GRANTED : PackageManager.PERMISSION_DENIED;
    }
    private static final class Native {
        static { System.load(System.getenv("WESTLAKE_PERMISSION_HELPER_PATH")); }
        static native int flags(String permission);
        static native boolean granted(String permission);
    }
    private static void own(String pkg, int user) {
        Application app = ActivityThread.currentApplication();
        if (app == null || !app.getPackageName().equals(pkg) || app.getUserId() != user)
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
    public void registerAttributionSource(AttributionSourceState source) { throw controllerRequired(); }
    public void addOnPermissionsChangeListener(IOnPermissionsChangeListener listener) { throw new UnsupportedOperationException("OH permission-change subscription unavailable"); }
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
}
