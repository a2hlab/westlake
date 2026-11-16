package adapter.packagemanager;

import java.io.File;

/**
 * An app's device-protected storage, kept apart from its credential-protected storage as Android keeps
 * them (/data/user_de/<user>/<package> and /data/user/<user>/<package>, two directories installd creates).
 * Westlake gave both the one staged data directory, so code that clears one of them cleared the other:
 * Play services writes its Chimera module config into credential storage and then deletes the config files
 * left in device-protected storage, which was the same file. It deleted the config it had just written,
 * every time, so no module was registered and every API a module provides answered SERVICE_INVALID.
 *
 * The launcher stages only the credential directory, and an app cannot create a directory beside it in
 * /data/data, so device-protected storage is a directory inside it. Apps reach it through
 * Context.createDeviceProtectedStorageContext, never by its path.
 */
public final class DataDirs {
    /** The name of the device-protected directory inside an app's staged data directory. */
    public static final String DEVICE_PROTECTED = ".device_protected";

    private DataDirs() {}

    /** The device-protected data directory for an app whose credential-protected one is dataDir. */
    public static String deviceProtected(String dataDir) {
        return dataDir == null || dataDir.isEmpty() ? dataDir : dataDir + "/" + DEVICE_PROTECTED;
    }

    /**
     * Creates the device-protected directory if it is missing: ContextImpl creates the directories under a data
     * directory (files, app_<name>, shared_prefs), never the data directory itself, which installd made.
     */
    public static void ensure(String deviceProtectedDir) {
        if (deviceProtectedDir == null || deviceProtectedDir.isEmpty()) return;
        try {
            File dir = new File(deviceProtectedDir);
            if (!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory()) {
                System.err.println("[WESTLAKE-DATA] device-protected storage not creatable: " + deviceProtectedDir);
            }
        } catch (RuntimeException e) {
            System.err.println("[WESTLAKE-DATA] device-protected storage check failed: " + e);
        }
    }
}
