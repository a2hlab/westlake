package android.media;

/**
 * WESTLAKE §471 — retains Android's media-mainline manager handoff.  OH does not run the APEX
 * initializer, so create the Android manager lazily if the normal setter was never called.
 */
public class MediaFrameworkPlatformInitializer {
    private static volatile MediaServiceManager sMediaServiceManager;

    public MediaFrameworkPlatformInitializer() {}

    public static void setMediaServiceManager(MediaServiceManager mediaServiceManager) {
        sMediaServiceManager = mediaServiceManager;
    }

    public static MediaServiceManager getMediaServiceManager() {
        MediaServiceManager manager = sMediaServiceManager;
        if (manager == null) {
            synchronized (MediaFrameworkPlatformInitializer.class) {
                manager = sMediaServiceManager;
                if (manager == null) {
                    manager = new MediaServiceManager();
                    sMediaServiceManager = manager;
                    System.err.println("[WESTLAKE-471] created MediaServiceManager on demand");
                    System.err.flush();
                }
            }
        }
        return manager;
    }

    public static void registerServiceWrappers() {}
}
