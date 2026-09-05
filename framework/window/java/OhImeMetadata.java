package adapter.window;

import android.content.pm.ApplicationInfo;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.view.inputmethod.InputMethodInfo;
import android.view.inputmethod.InputMethodSubtype;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Real OH keyboard identity exposed as Android-facing metadata, not an APK. */
public final class OhImeMetadata {
    private OhImeMetadata() {}
    private static final class Native {
        static { System.load(System.getenv("WESTLAKE_IME_METADATA_PATH")); }
        static void load() {}
    }
    private static native String[] nativeCurrent();
    private static native String[][] nativeList(boolean enabled);
    private static native boolean nativePicker();
    private static native void nativeSelection(String text, int start, int end);
    private static InputMethodInfo info(String[] row) {
        if (row == null || row.length != 6 || row[0].isEmpty() || row[1].isEmpty()) return null;
        ResolveInfo resolve = new ResolveInfo();
        resolve.serviceInfo = new ServiceInfo();
        resolve.serviceInfo.packageName = row[0]; resolve.serviceInfo.name = row[1];
        resolve.serviceInfo.permission = "android.permission.BIND_INPUT_METHOD";
        resolve.serviceInfo.enabled = true; resolve.serviceInfo.exported = true;
        resolve.serviceInfo.nonLocalizedLabel = row[2].isEmpty() ? row[0] : row[2];
        resolve.serviceInfo.applicationInfo = new ApplicationInfo();
        resolve.serviceInfo.applicationInfo.packageName = row[0];
        resolve.serviceInfo.applicationInfo.enabled = true;
        resolve.serviceInfo.applicationInfo.uid = -1; // Not an installed Android APK/UID.
        List<InputMethodSubtype> subtypes = new ArrayList<>();
        if (!row[3].isEmpty()) subtypes.add(subtype(row));
        return new InputMethodInfo(resolve, false, null, subtypes, 0, false);
    }
    private static InputMethodSubtype subtype(String[] row) {
        return new InputMethodSubtype.InputMethodSubtypeBuilder()
                .setSubtypeId((row[0] + "/" + row[3]).hashCode())
                .setSubtypeLocale(row[4]).setLanguageTag(row[4].replace('_','-'))
                .setSubtypeMode("keyboard").setIsAsciiCapable("en".equals(row[5]))
                .build();
    }
    public static InputMethodInfo current() {
        try { Native.load(); return info(nativeCurrent()); }
        catch (RuntimeException | LinkageError error) { System.err.println("[OH_IME_META] unavailable " + error); return null; }
    }
    public static List<InputMethodInfo> list(boolean enabled) {
        try {
            Native.load(); String[][] rows = nativeList(enabled);
            if (rows == null) return Collections.emptyList();
            java.util.LinkedHashMap<String, InputMethodInfo> methods = new java.util.LinkedHashMap<>();
            java.util.Map<String, List<InputMethodSubtype>> subs = new java.util.HashMap<>();
            for (String[] row : rows) {
                InputMethodInfo item = info(row);
                if (item == null) continue;
                if (!methods.containsKey(item.getId())) {
                    methods.put(item.getId(), item); subs.put(item.getId(), new ArrayList<InputMethodSubtype>());
                }
                if (!row[3].isEmpty()) {
                    InputMethodSubtype subtype = subtype(row);
                    if (!subs.get(item.getId()).contains(subtype)) subs.get(item.getId()).add(subtype);
                }
            }
            List<InputMethodInfo> result = new ArrayList<>();
            for (InputMethodInfo item : methods.values()) {
                ResolveInfo resolve = new ResolveInfo(); resolve.serviceInfo = item.getServiceInfo();
                result.add(new InputMethodInfo(resolve, false, null, subs.get(item.getId()), 0, false));
            }
            return result;
        } catch (RuntimeException | LinkageError error) { System.err.println("[OH_IME_META] list unavailable " + error); return Collections.emptyList(); }
    }
    public static void picker() {
        try { Native.load(); if (!nativePicker()) System.err.println("[OH_IME_META] picker request rejected by OH"); }
        catch (RuntimeException | LinkageError error) { System.err.println("[OH_IME_META] picker unavailable " + error); }
    }
    static void selection(String text, int start, int end) {
        try { Native.load(); nativeSelection(text, start, end); }
        catch (RuntimeException | LinkageError error) { System.err.println("[OH_IME_META] selection unavailable " + error); }
    }
}
