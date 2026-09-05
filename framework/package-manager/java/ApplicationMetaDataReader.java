package adapter.packagemanager;

import android.content.pm.ApplicationInfo;
import android.os.Bundle;

import org.json.JSONObject;

import java.util.Iterator;

/** Converts parsed Android manifest application metadata into ApplicationInfo.metaData. */
final class ApplicationMetaDataReader {
    private ApplicationMetaDataReader() {}

    static ApplicationInfo populate(ApplicationInfo info, JSONObject manifest) {
        if (info == null || manifest == null) return info;

        JSONObject app = manifest.optJSONObject("applicationInfo");
        JSONObject values = app != null ? app.optJSONObject("metaData") : null;
        if (values == null) values = manifest.optJSONObject("appMetaData");
        if (values == null) return info;

        Bundle result = info.metaData != null ? new Bundle(info.metaData) : new Bundle();
        Iterator<String> keys = values.keys();
        while (keys.hasNext()) {
            String key = keys.next();
            Object value = values.opt(key);
            if (value == null || value == JSONObject.NULL) continue;
            if (value instanceof Boolean) {
                result.putBoolean(key, (Boolean) value);
            } else if (value instanceof Integer) {
                result.putInt(key, (Integer) value);
            } else if (value instanceof Long) {
                long longValue = (Long) value;
                if (longValue >= Integer.MIN_VALUE && longValue <= Integer.MAX_VALUE) {
                    result.putInt(key, (int) longValue);
                } else {
                    result.putLong(key, longValue);
                }
            } else if (value instanceof Number) {
                result.putFloat(key, ((Number) value).floatValue());
            } else {
                result.putString(key, String.valueOf(value));
            }
        }
        info.metaData = result;
        System.err.println("[WESTLAKE-PM] application metadata package=" + info.packageName
                + " entries=" + result.size());
        return info;
    }
}
