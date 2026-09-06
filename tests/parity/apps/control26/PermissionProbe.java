package org.westlake.gapcontrol;
import android.app.Activity;
import android.content.pm.PackageManager;

final class PermissionProbe {
    static void run(Activity activity) {
        try {
            Object service = activity.getSystemService("permission");
            if(service==null)throw new AssertionError("permission facade missing");
            boolean rationale=activity.shouldShowRequestPermissionRationale("org.westlake.permission.UNKNOWN");
            if(rationale)throw new AssertionError("unknown permission has rationale");
            if(activity.checkSelfPermission("org.westlake.permission.UNKNOWN")==PackageManager.PERMISSION_GRANTED)
                throw new AssertionError("unknown permission incorrectly granted by existing check path");
            if(activity.checkSelfPermission("android.permission.INTERNET")!=PackageManager.PERMISSION_GRANTED)
                throw new AssertionError("real OH Internet grant not mapped");
            if(activity.shouldShowRequestPermissionRationale("android.permission.INTERNET"))
                throw new AssertionError("granted Internet permission has rationale");
            int flags=(Integer)PackageManager.class.getMethod("getPermissionFlags",String.class,String.class,android.os.UserHandle.class)
                .invoke(activity.getPackageManager(),"android.permission.INTERNET",activity.getPackageName(),android.os.Process.myUserHandle());
            System.err.println("[GAP_PERMISSION] internet_flags="+flags);
            System.err.println("[GAP_PERMISSION] PASS facade-and-unknown-denial");
        } catch(Throwable error) { System.err.println("[GAP_PERMISSION] FAIL "+error);error.printStackTrace(); }
    }
}
