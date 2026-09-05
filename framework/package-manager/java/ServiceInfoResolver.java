package adapter.packagemanager;

import android.content.ComponentName;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.Bundle;

import java.util.Iterator;

import org.json.JSONArray;
import org.json.JSONObject;

/** Resolves Android service components from the APK manifest at the OH boundary. */
public final class ServiceInfoResolver {
    private ServiceInfoResolver() {}

    public static ServiceInfo resolve(ComponentName component, long flags) {
        if (component == null) return null;

        String packageName = component.getPackageName();
        String className = component.getClassName();
        try {
            String json = PackageManagerAdapter.getApkManifestJson(packageName);
            if (json == null || json.isEmpty()) return null;
            JSONArray services = new JSONObject(json).optJSONArray("services");
            if (services == null) return null;
            for (int i = 0; i < services.length(); i++) {
                JSONObject service = services.optJSONObject(i);
                if (service == null || !className.equals(service.optString("name", ""))) {
                    continue;
                }

                ServiceInfo info = new ServiceInfo();
                info.name = className;
                info.packageName = packageName;
                info.processName = service.optString("processName", "");
                if (info.processName.isEmpty()) info.processName = packageName;
                info.exported = service.optBoolean("exported", false);
                info.enabled = service.optBoolean("enabled", true);
                info.directBootAware = service.optBoolean("directBootAware", false);
                info.permission = nullIfEmpty(service.optString("permission", ""));
                info.applicationInfo = buildApplicationInfo(packageName);

                if ((flags & PackageManager.GET_META_DATA) != 0) {
                    JSONObject meta = service.optJSONObject("metaData");
                    if (meta != null) info.metaData = toBundle(meta);
                }
                System.err.println("[WESTLAKE-PM] getServiceInfo " + component
                        + " metadata=" + (info.metaData != null ? info.metaData.size() : 0));
                return info;
            }
            System.err.println("[WESTLAKE-PM] getServiceInfo not found: " + component);
        } catch (Throwable t) {
            System.err.println("[WESTLAKE-PM] service manifest parse failed for "
                    + component + ": " + t);
        }
        return null;
    }

    private static ApplicationInfo buildApplicationInfo(String packageName) {
        ApplicationInfo info = new ApplicationInfo();
        info.packageName = packageName;
        info.processName = packageName;
        info.dataDir = "/data/data/" + packageName;
        info.sourceDir = "/system/app/" + packageName + "/" + packageName + ".apk";
        info.publicSourceDir = info.sourceDir;
        info.enabled = true;
        return info;
    }

    private static Bundle toBundle(JSONObject meta) {
        Bundle bundle = new Bundle();
        Iterator<String> keys = meta.keys();
        while (keys.hasNext()) {
            String key = keys.next();
            Object value = meta.opt(key);
            if (value instanceof Boolean) {
                bundle.putBoolean(key, (Boolean) value);
            } else if (value instanceof Integer) {
                bundle.putInt(key, (Integer) value);
            } else if (value instanceof Long) {
                bundle.putLong(key, (Long) value);
            } else if (value instanceof Double) {
                bundle.putDouble(key, (Double) value);
            } else if (value != null && value != JSONObject.NULL) {
                bundle.putString(key, String.valueOf(value));
            }
        }
        return bundle;
    }

    private static String nullIfEmpty(String value) {
        return value == null || value.isEmpty() ? null : value;
    }
}
