package adapter.core;

import android.app.Activity;
import android.app.ActivityThread;
import android.app.servertransaction.ClientTransaction;
import android.app.servertransaction.ConfigurationChangeItem;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.os.Handler;
import android.os.IBinder;
import android.os.LocaleList;
import android.os.Looper;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;

/**
 * Per-app languages for the app in this process: LocaleManager.setApplicationLocales and getApplicationLocales (API 33+),
 * which AppCompat's in-app language setting calls. Android keeps an app's languages in the system, applies them to the
 * app's configuration ahead of the system's languages from then on and across restarts, and relaunches the app's
 * activities that do not handle a locale change.
 *
 * The locale service here accepted a set and dropped it, and reported no languages back, so a language an app chose
 * never applied. offerToday sets its language, compares, and restarts its MainActivity (NEW_TASK | CLEAR_TASK) until
 * the configuration has that language, which it never got: thousands of restarts and no frame, on the D600 and on the
 * phones. Any app with a language picker had a setting that did nothing.
 *
 * The languages are kept in the app's data directory (westlake_app_locales, language tags), so a restart keeps them, as
 * on Android. Only the app's own package is answered: another package's languages are not this process's to read or set.
 */
public final class AppLocales {
    private AppLocales() {}

    private static final String TAG = "[WL-LOCALES] ";
    private static final String FILE = "westlake_app_locales";

    private static String sPackage;
    private static File sFile;
    private static LocaleList sLocales;   // null until read

    /** The app's package and data directory, from bindApplication, before its configuration is built. */
    public static synchronized void init(String packageName, String dataDir) {
        sPackage = packageName;
        sFile = dataDir != null ? new File(dataDir, FILE) : null;
        sLocales = null;
    }

    private static synchronized LocaleList stored() {
        if (sLocales == null) {
            LocaleList read = LocaleList.getEmptyLocaleList();
            try {
                if (sFile != null && sFile.isFile()) {
                    String tags = new String(Files.readAllBytes(sFile.toPath()), StandardCharsets.UTF_8).trim();
                    if (!tags.isEmpty()) read = LocaleList.forLanguageTags(tags);
                }
            } catch (Throwable t) {
                System.err.println(TAG + "the app's languages could not be read: " + t);
            }
            sLocales = read;
        }
        return sLocales;
    }

    /** getApplicationLocales: the app's own languages, none for another package. */
    public static LocaleList get(String packageName) {
        String own;
        synchronized (AppLocales.class) {
            own = sPackage;
        }
        if (packageName == null || !packageName.equals(own)) return LocaleList.getEmptyLocaleList();
        return stored();
    }

    /** setApplicationLocales for the app's own package: kept, then applied to the running app on its main thread. */
    public static void set(String packageName, LocaleList locales) {
        LocaleList next = locales != null ? locales : LocaleList.getEmptyLocaleList();
        synchronized (AppLocales.class) {
            if (packageName == null || !packageName.equals(sPackage)) {
                System.err.println(TAG + "languages for " + packageName + " not set: not this app's package");
                return;
            }
            if (next.equals(stored())) return;   // no change: Android relaunches nothing either
            sLocales = next;
            write(next);
        }
        System.err.println(TAG + "the app's languages are now [" + next.toLanguageTags() + "]");
        new Handler(Looper.getMainLooper()).post(AppLocales::apply);
    }

    private static void write(LocaleList locales) {
        if (sFile == null) return;
        try {
            if (locales.isEmpty()) {
                Files.deleteIfExists(sFile.toPath());
                return;
            }
            File temporary = new File(sFile.getPath() + ".new");
            try (FileOutputStream out = new FileOutputStream(temporary)) {
                out.write(locales.toLanguageTags().getBytes(StandardCharsets.UTF_8));
                out.getFD().sync();
            }
            if (!temporary.renameTo(sFile)) throw new java.io.IOException("rename failed");
        } catch (Throwable t) {
            System.err.println(TAG + "the app's languages could not be kept: " + t);
        }
    }

    /** The app's languages over a configuration's (the system's), as Android's per-app override puts them. */
    public static Configuration overlay(Configuration config) {
        if (config == null) return null;
        LocaleList app;
        synchronized (AppLocales.class) {
            app = sPackage != null ? stored() : LocaleList.getEmptyLocaleList();
        }
        if (!app.isEmpty()) {
            setLocales(config, app);
            System.err.println(TAG + "the app's languages over the system's: [" + app.toLanguageTags() + "]");
        }
        return config;
    }

    /**
     * Configuration.setLocales also derives the layout direction through ICU, which the bind-time configuration avoids
     * on OH (it sets the fields directly). Here the full call is tried first, then the fields alone.
     */
    private static void setLocales(Configuration config, LocaleList locales) {
        try {
            config.setLocales(locales);
            return;
        } catch (Throwable icu) {
            // the fields below
        }
        try {
            java.lang.reflect.Field list = Configuration.class.getDeclaredField("mLocaleList");
            list.setAccessible(true);
            list.set(config, locales);
            java.lang.reflect.Field primary = Configuration.class.getDeclaredField("locale");
            primary.setAccessible(true);
            primary.set(config, locales.get(0));
        } catch (Throwable t) {
            System.err.println(TAG + "the configuration's languages could not be set: " + t);
        }
    }

    /**
     * Applies the app's languages to the running app: a configuration change for the process (what ATMS sends), then a
     * relaunch of each of the app's activities that does not handle a locale change (ATMS's relaunch; recreate here).
     */
    private static void apply() {
        try {
            ActivityThread thread = ActivityThread.currentActivityThread();
            if (thread == null) return;
            Configuration config = new Configuration(thread.getConfiguration());
            LocaleList app = stored();
            setLocales(config, app.isEmpty() ? Resources.getSystem().getConfiguration().getLocales() : app);
            config.seq = thread.getConfiguration().seq + 1;
            ClientTransaction transaction = ClientTransaction.obtain(thread.getApplicationThread());
            transaction.addTransactionItem(ConfigurationChangeItem.obtain(config, 0));
            thread.executeTransaction(transaction);
            int relaunched = 0;
            for (IBinder token : adapter.activity.ActivityClientControllerAdapter.taskTokens()) {
                Activity activity = thread.getActivity(token);
                if (activity == null || activity.isFinishing() || activity.isDestroyed()) continue;
                int handled = 0;
                try {
                    ActivityInfo info = activity.getPackageManager().getActivityInfo(activity.getComponentName(), 0);
                    handled = info.configChanges;
                } catch (Throwable t) {
                    // not found: relaunched, as an activity that declares nothing is
                }
                if ((handled & ActivityInfo.CONFIG_LOCALE) == 0) {
                    activity.recreate();
                    relaunched++;
                }
            }
            Locale primary = config.getLocales().isEmpty() ? null : config.getLocales().get(0);
            System.err.println(TAG + "applied: configuration language " + primary + ", " + relaunched
                    + " activities relaunched");
        } catch (Throwable t) {
            System.err.println(TAG + "the languages could not be applied to the running app: " + t);
        }
    }
}
