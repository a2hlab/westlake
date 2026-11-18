import java.lang.reflect.Method;

/** Checks the native metadata entry points on ART without changing the current keyboard. */
public final class ImeMetadataLoadProbe {
    public static String query() throws Exception {
        Class<?> bridge = Class.forName("adapter.window.OhImeMetadata");
        Class.forName("adapter.window.OhImeMetadata$Native", true, bridge.getClassLoader());
        Method current = bridge.getDeclaredMethod("nativeCurrent");
        Method enabled = bridge.getDeclaredMethod("nativeList", boolean.class);
        current.setAccessible(true);
        enabled.setAccessible(true);
        String[] selection = (String[]) current.invoke(null);
        String[][] rows = (String[][]) enabled.invoke(null, true);
        if (selection != null && selection.length != 6) {
            throw new AssertionError("Unexpected keyboard metadata row");
        }
        if (rows != null) {
            for (String[] row : rows) {
                if (row == null || row.length != 6) {
                    throw new AssertionError("Unexpected enabled keyboard metadata row");
                }
            }
        }
        return "Native keyboard queries returned. Current available=" + (selection != null)
                + ", enabled rows=" + (rows == null ? "unavailable" : rows.length) + "\n";
    }
}
