package adapter.packagemanager;

import android.content.ComponentName;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.os.Bundle;

import java.util.Iterator;
import java.util.List;

import org.json.JSONArray;
import org.json.JSONObject;

/** Resolves Android provider components from the APK manifest at the OH boundary. */
public final class ProviderInfoResolver {
    private ProviderInfoResolver() {}

    public static ProviderInfo resolve(ComponentName component, long flags) {
        if (component == null) return null;

        String packageName = component.getPackageName();
        String className = component.getClassName();
        List<ProviderInfo> providers =
                PackageManagerAdapter.getProvidersForPackage(packageName);
        for (ProviderInfo info : providers) {
            if (info == null || !className.equals(info.name)) continue;
            if ((flags & PackageManager.GET_META_DATA) != 0) {
                populateMetaData(info, packageName, className);
            }
            System.err.println("[WESTLAKE-PM] getProviderInfo " + component
                    + " metadata=" + (info.metaData != null ? info.metaData.size() : 0));
            return info;
        }
        System.err.println("[WESTLAKE-PM] getProviderInfo not found: " + component);
        return null;
    }

    private static void populateMetaData(
            ProviderInfo info, String packageName, String className) {
        try {
            String json = PackageManagerAdapter.getApkManifestJson(packageName);
            if (json == null || json.isEmpty()) return;
            JSONArray providers = new JSONObject(json).optJSONArray("providers");
            if (providers == null) return;
            for (int i = 0; i < providers.length(); i++) {
                JSONObject provider = providers.optJSONObject(i);
                if (provider == null || !className.equals(provider.optString("name", ""))) {
                    continue;
                }
                JSONObject meta = provider.optJSONObject("metaData");
                if (meta == null) return;
                Bundle bundle = new Bundle();
                Iterator<String> keys = meta.keys();
                while (keys.hasNext()) {
                    String key = keys.next();
                    bundle.putString(key, meta.optString(key, ""));
                }
                info.metaData = bundle;
                return;
            }
        } catch (Throwable t) {
            System.err.println("[WESTLAKE-PM] provider metadata parse failed for "
                    + packageName + "/" + className + ": " + t);
        }
    }
}
