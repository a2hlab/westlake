package adapter.packagemanager;

import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.ProviderInfo;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Packages that run in another Westlake process (ADR-0001). There are two uses:
 *
 * In an app, Play services' packages are answered by the Play services process, through a Source
 * that PlayServicesChannel sets.
 *
 * In the Play services process, the apps connected to it are recorded here, each with the virtual
 * uid that Binder.getCallingUid answers while serving it. Every Westlake process shares one OH uid,
 * so this is what lets Play services tell its callers apart.
 *
 * An app has the same virtual uid in every Westlake process, and keeps it from one launch to the next, as
 * an app keeps its uid on Android from install on: the uids are in one registry that the spawners keep where
 * only root can enter, which every process asks through PackageUidTable; a process whose spawner predates it
 * uses the older table in the host app's files directory, which holds every launch's runtime
 * (WESTLAKE_RUNTIME_REAL_ROOT is one of them).
 * Each process numbered its clients itself before, in the order they connected, so an app had one uid in
 * Play services, another in the Play Store and another in each of the store's other processes (ADR-0002),
 * and a uid that one of them passed to another named a different app there.
 */
public final class RemotePackages {
    /** Answers about the packages in the process at the other end of a channel. */
    public interface Source {
        boolean owns(String packageName);
        PackageInfo packageInfo(String packageName, long flags);
        ApplicationInfo applicationInfo(String packageName, long flags);
        ServiceInfo serviceInfo(ComponentName component, long flags);
        ProviderInfo providerInfo(ComponentName component, long flags);
        ProviderInfo resolveProvider(String authority, long flags);
        List<ResolveInfo> queryServices(Intent intent, String type, long flags);
    }

    /** Appended to the Westlake user's base, above any uid an installed OH app is given. */
    private static final int FIRST_APP_ID = 10200;
    private static final int PER_USER_RANGE = 100000;

    /** The uid table's path under the host app's files directory. */
    private static final String UID_TABLE = "westlake-system/package-uids";

    private static volatile Source sSource;
    private static PackageUidTable sTable = uidTable();
    private static final Map<String, PackageInfo> sClients = new HashMap<>();
    private static final Map<String, Integer> sUids = new HashMap<>();
    private static final Map<Integer, String> sPackages = new HashMap<>();

    private RemotePackages() {}

    public static void setSource(Source source) {
        sSource = source;
    }

    /**
     * Records an app connected to this process and returns its virtual uid, which stays the same
     * for a package however often it reconnects.
     */
    public static synchronized int registerClient(PackageInfo info, int ownUid) {
        Integer uid = sUids.get(info.packageName);
        if (uid == null) {
            int user = ownUid / PER_USER_RANGE;
            uid = user * PER_USER_RANGE + appId(info.packageName);
            sUids.put(info.packageName, uid);
            sPackages.put(uid, info.packageName);
        }
        info.applicationInfo.uid = uid;
        sClients.put(info.packageName, info);
        return uid;
    }

    /** The table every Westlake process shares; none outside a Westlake launch. */
    private static PackageUidTable uidTable() {
        String runtime = System.getenv("WESTLAKE_RUNTIME_REAL_ROOT");
        File files = runtime == null || runtime.isEmpty() ? null : new File(runtime).getParentFile();
        return files == null ? null : new PackageUidTable(new File(files, UID_TABLE), FIRST_APP_ID);
    }

    /**
     * A new client's app id: the table's, or, if this process can't use the table, the next after its own
     * clients', as before the table; then the app's uid holds only in this process.
     */
    private static int appId(String packageName) {
        if (sTable != null) {
            try {
                return sTable.appId(packageName);
            } catch (IOException | RuntimeException e) {
                System.err.println("[WL-PLAY] uid table unusable; this process numbers its clients itself: " + e);
                sTable = null;
            }
        }
        int next = FIRST_APP_ID;
        for (int uid : sUids.values()) next = Math.max(next, uid % PER_USER_RANGE + 1);
        return next;
    }

    private static synchronized PackageInfo client(String packageName) {
        return sClients.get(packageName);
    }

    public static boolean contains(String packageName) {
        if (packageName == null) return false;
        if (client(packageName) != null) return true;
        Source source = sSource;
        return source != null && source.owns(packageName);
    }

    public static PackageInfo packageInfo(String packageName, long flags) {
        PackageInfo known = client(packageName);
        if (known != null) return known;
        Source source = sSource;
        return source == null ? null : source.packageInfo(packageName, flags);
    }

    public static ApplicationInfo applicationInfo(String packageName, long flags) {
        PackageInfo known = client(packageName);
        if (known != null) return known.applicationInfo;
        Source source = sSource;
        return source == null ? null : source.applicationInfo(packageName, flags);
    }

    public static ServiceInfo serviceInfo(ComponentName component, long flags) {
        Source source = sSource;
        return source == null ? null : source.serviceInfo(component, flags);
    }

    public static ProviderInfo providerInfo(ComponentName component, long flags) {
        Source source = sSource;
        return source == null ? null : source.providerInfo(component, flags);
    }

    public static ProviderInfo resolveProvider(String authority, long flags) {
        Source source = sSource;
        return source == null ? null : source.resolveProvider(authority, flags);
    }

    public static List<ResolveInfo> queryServices(Intent intent, String type, long flags) {
        Source source = sSource;
        if (source == null) return Collections.emptyList();
        List<ResolveInfo> found = source.queryServices(intent, type, flags);
        return found == null ? Collections.emptyList() : new ArrayList<>(found);
    }

    /**
     * Whether a package in another process holds a permission, going by what its manifest requests
     * or defines. A package uses what it defines (Play services' c2dm SEND, which Firebase checks
     * before it will use it). This process can't see what that process was really granted.
     */
    public static boolean holdsPermission(String packageName, String permission) {
        if (!contains(packageName)) return false;
        PackageInfo info = packageInfo(packageName, android.content.pm.PackageManager.GET_PERMISSIONS);
        if (info == null) return false;
        if (info.requestedPermissions != null) {
            for (String requested : info.requestedPermissions) {
                if (permission.equals(requested)) return true;
            }
        }
        if (info.permissions != null) {
            for (android.content.pm.PermissionInfo defined : info.permissions) {
                if (permission.equals(defined.name)) return true;
            }
        }
        return false;
    }

    /** The virtual uid of a connected app, or -1. */
    public static synchronized int uidOf(String packageName) {
        Integer uid = sUids.get(packageName);
        return uid == null ? -1 : uid;
    }

    /** The connected app with this virtual uid, or null. */
    public static synchronized String packageForUid(int uid) {
        return sPackages.get(uid);
    }

    /**
     * The uid an app has in every Westlake process, whether or not it has connected to this one: a PendingIntent
     * can reach this process from an app that never did, passed on by another of this app's processes. Its id
     * comes from the shared table, as registerClient's does; this app's own package has this process's uid, as
     * its other processes do. -1 for no package.
     */
    public static synchronized int uidForPackage(String packageName, int ownUid, String ownPackage) {
        if (packageName == null) return -1;
        if (packageName.equals(ownPackage)) return ownUid;
        Integer known = sUids.get(packageName);
        if (known != null) return known;
        return (ownUid / PER_USER_RANGE) * PER_USER_RANGE + appId(packageName);
    }
}
