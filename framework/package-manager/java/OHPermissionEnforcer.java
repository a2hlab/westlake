package adapter.packagemanager;

import android.content.AttributionSource;
import android.content.pm.PackageManager;
import android.os.PermissionEnforcer;
import android.permission.PermissionCheckerManager;

/** AIDL permission checks at the OH token boundary, independent of Context startup. */
public final class OHPermissionEnforcer extends PermissionEnforcer {
    private static final OHPermissionEnforcer INSTANCE = new OHPermissionEnforcer();

    private OHPermissionEnforcer() { super(); }

    public static OHPermissionEnforcer getInstance() { return INSTANCE; }

    @Override
    protected int checkPermission(String permission, int pid, int uid) {
        return PermissionManagerAdapter.checkProcessPermission(permission, pid, uid)
                == PackageManager.PERMISSION_GRANTED ? PermissionCheckerManager.PERMISSION_GRANTED
                : PermissionCheckerManager.PERMISSION_HARD_DENIED;
    }

    @Override
    protected int checkPermission(String permission, AttributionSource source) {
        // The OH helper checks the current process token. It cannot authorize
        // another process or an attribution chain using that token.
        if (source == null || source.getNext() != null) {
            return PermissionCheckerManager.PERMISSION_HARD_DENIED;
        }
        return checkPermission(permission, source.getPid(), source.getUid());
    }
}
