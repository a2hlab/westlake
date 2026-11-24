package adapter.packagemanager;

import android.content.ComponentName;
import android.content.pm.ComponentInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageParser;
import android.content.pm.pkg.FrameworkPackageUserState;
import android.os.Binder;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Proxy;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Properties;
import java.util.Set;

/**
 * The enabled state an app gives its own components at run time (PackageManager.setComponentEnabledSetting), kept as
 * Android's package manager keeps it: per component, ENABLED or DISABLED over the manifest's value and DEFAULT back to
 * the manifest; saved across launches and seen by every process of the app.
 *
 * Westlake's package manager dropped these calls and answered DEFAULT, so a component an app declares disabled and
 * enables when it needs it never resolved. Play services declares its account settings screen disabled and enables it
 * at run time; the Play Store's "Manage your Google Account" found no activity, and the uncaught
 * ActivityNotFoundException ended the Store. WorkManager enables its constraint and reschedule receivers the same way.
 *
 * Android keeps these states in package-restrictions.xml and applies them through the user state its parser reads
 * (PackageParser.isEnabled: an explicit state wins over the manifest). Here each app keeps its own, in its data
 * directory, since only the app itself may change them; the registry hands them to the same parser. An app that never
 * sets a state gets FrameworkPackageUserState.DEFAULT, as before. Not done here: Android also kills the app after a
 * change made without DONT_KILL_APP and broadcasts PACKAGE_CHANGED; Westlake keeps the app running and broadcasts
 * nothing. Application-level states (setApplicationEnabledSetting) are not kept either.
 */
final class ComponentStates {
    private static final String FILE = "westlake-component-states.properties";
    private static Properties states = new Properties();
    private static long stamp = Long.MIN_VALUE;
    private static FrameworkPackageUserState userState = FrameworkPackageUserState.DEFAULT;

    private ComponentStates() {}

    /** Resolved before any lock of this class is taken: the registry has its own lock. */
    private static File file() {
        String dir = SourcePackageRegistry.dataDir();
        return dir == null ? null : new File(dir, FILE);
    }

    private static long stampOf(File file) {
        return file == null || !file.isFile() ? 0 : file.lastModified() * 31 + file.length();
    }

    /** Reads the file again when another process of the app changed it. */
    private static void reloadIfChanged(File file) {
        long now = stampOf(file);
        if (now == stamp) return;
        Properties loaded = new Properties();
        if (now != 0) {
            try (FileInputStream in = new FileInputStream(file)) {
                loaded.load(in);
            } catch (IOException | IllegalArgumentException failure) {
                System.err.println("[WESTLAKE-PM] component states unreadable, kept the previous ones: " + failure);
                return;
            }
        }
        states = loaded;
        stamp = now;
        userState = null;
    }

    private static int stateOf(String className) {
        String value = states.getProperty(className);
        if (value == null) return PackageManager.COMPONENT_ENABLED_STATE_DEFAULT;
        try {
            return Integer.parseInt(value.trim());
        } catch (NumberFormatException bad) {
            return PackageManager.COMPONENT_ENABLED_STATE_DEFAULT;
        }
    }

    /** The user state the parser applies: explicit states over the manifest's enabled attribute. */
    static FrameworkPackageUserState userState() {
        File file = file();
        synchronized (ComponentStates.class) {
            reloadIfChanged(file);
            if (userState != null) return userState;
            Set<String> enabled = new HashSet<>();
            Set<String> disabled = new HashSet<>();
            for (String name : states.stringPropertyNames()) {
                int state = stateOf(name);
                if (state == PackageManager.COMPONENT_ENABLED_STATE_ENABLED) enabled.add(name);
                else if (state == PackageManager.COMPONENT_ENABLED_STATE_DISABLED) disabled.add(name);
            }
            if (enabled.isEmpty() && disabled.isEmpty()) {
                userState = FrameworkPackageUserState.DEFAULT;
                return userState;
            }
            final Set<String> on = Collections.unmodifiableSet(enabled);
            final Set<String> off = Collections.unmodifiableSet(disabled);
            final FrameworkPackageUserState base = FrameworkPackageUserState.DEFAULT;
            userState = (FrameworkPackageUserState) Proxy.newProxyInstance(
                    FrameworkPackageUserState.class.getClassLoader(), new Class<?>[] {FrameworkPackageUserState.class},
                    (proxy, method, arguments) -> {
                        switch (method.getName()) {
                            case "getEnabledComponents":
                                return on;
                            case "getDisabledComponents":
                                return off;
                            case "isComponentEnabled":
                                return on.contains((String) arguments[0]);
                            case "isComponentDisabled":
                                return off.contains((String) arguments[0]);
                            case "hashCode":
                                return System.identityHashCode(proxy);
                            case "equals":
                                return proxy == arguments[0];
                            case "toString":
                                return "ComponentStates(" + on.size() + " enabled, " + off.size() + " disabled)";
                            default:
                                try {
                                    return method.invoke(base, arguments);
                                } catch (InvocationTargetException thrown) {
                                    throw thrown.getCause();
                                }
                        }
                    });
            return userState;
        }
    }

    /** Whether one of the app's own components is enabled, as the parser decides it; another app's: its manifest. */
    static boolean isEnabled(ComponentInfo info) {
        if (info == null) return false;
        boolean packageEnabled = info.applicationInfo == null || info.applicationInfo.enabled;
        if (!info.packageName.equals(SourcePackageRegistry.packageName())) return packageEnabled && info.enabled;
        return PackageParser.isEnabled(userState(), packageEnabled, info.enabled, info.name, 0);
    }

    /** PackageManager.getComponentEnabledSetting for the app's own component; an unknown class has no state: DEFAULT. */
    static int get(ComponentName component) {
        File file = file();
        synchronized (ComponentStates.class) {
            reloadIfChanged(file);
            return stateOf(component.getClassName());
        }
    }

    /**
     * PackageManager.setComponentEnabledSetting(s): checked as PackageManagerService checks it (state values,
     * duplicates, the DONT_KILL_APP conflict, the caller's own package, the class exists), then ENABLED and DISABLED
     * recorded, DEFAULT cleared, and the user-level states, which Android does not take for a component, logged.
     */
    static void apply(List<PackageManager.ComponentEnabledSetting> settings) {
        String own = SourcePackageRegistry.packageName();
        Set<ComponentName> seen = new HashSet<>();
        Integer killFlag = null;
        for (PackageManager.ComponentEnabledSetting setting : settings) {
            int state = setting.getEnabledState();
            if (state < PackageManager.COMPONENT_ENABLED_STATE_DEFAULT
                    || state > PackageManager.COMPONENT_ENABLED_STATE_DISABLED_UNTIL_USED) {
                throw new IllegalArgumentException("Invalid new component state: " + state);
            }
        }
        for (PackageManager.ComponentEnabledSetting setting : settings) {
            if (!setting.isComponent()) continue;
            if (settings.size() > 1) {
                if (!seen.add(setting.getComponentName()))
                    throw new IllegalArgumentException("The component " + setting.getComponentName() + " is duplicated");
                int flag = setting.getEnabledFlags() & PackageManager.DONT_KILL_APP;
                if (killFlag == null) killFlag = flag;
                else if (killFlag != flag)
                    throw new IllegalArgumentException("A conflict of the DONT_KILL_APP flag between components in"
                            + " the package " + setting.getPackageName());
            }
            if (own == null || !own.equals(setting.getPackageName())) {
                // Only the app may change its components: changing another's needs
                // CHANGE_COMPONENT_ENABLED_STATE, a signature permission no app holds here.
                throw new SecurityException("Attempt to change component state; pid=" + Binder.getCallingPid()
                        + ", uid=" + Binder.getCallingUid() + ", component=" + setting.getComponentName());
            }
            if (!SourcePackageRegistry.hasComponent(setting.getClassName()))
                throw new IllegalArgumentException("Component class " + setting.getClassName()
                        + " does not exist in " + own);
        }
        File file = file();
        if (file == null) throw new IllegalStateException("component states before the package registered");
        int changed = 0;
        int ignored = 0;
        synchronized (ComponentStates.class) {
            reloadIfChanged(file);
            Properties next = new Properties();
            next.putAll(states);
            for (PackageManager.ComponentEnabledSetting setting : settings) {
                if (!setting.isComponent()) {
                    ignored++;
                    continue;
                }
                String name = setting.getClassName();
                String before = next.getProperty(name);
                switch (setting.getEnabledState()) {
                    case PackageManager.COMPONENT_ENABLED_STATE_ENABLED:
                    case PackageManager.COMPONENT_ENABLED_STATE_DISABLED:
                        next.setProperty(name, Integer.toString(setting.getEnabledState()));
                        break;
                    case PackageManager.COMPONENT_ENABLED_STATE_DEFAULT:
                        next.remove(name);
                        break;
                    default:
                        System.err.println("[WESTLAKE-PM] setComponentEnabledSetting: component " + own + "/" + name
                                + " requested an invalid new component state: " + setting.getEnabledState());
                        continue;
                }
                if (!java.util.Objects.equals(before, next.getProperty(name))) changed++;
            }
            if (changed > 0) {
                save(next, file);
                states = next;
                stamp = stampOf(file);
                userState = null;
            }
        }
        if (changed > 0 || ignored > 0) {
            System.err.println("[WESTLAKE-PM] component states: " + changed + " changed"
                    + (ignored > 0 ? ", " + ignored + " application-level settings not kept" : "")
                    + "; " + states.size() + " explicit in " + own);
        }
    }

    /** A new file renamed over the old one, so another process never reads half a file. */
    private static void save(Properties values, File file) {
        File temp = new File(file.getParentFile(), FILE + ".tmp" + android.os.Process.myPid());
        try (FileOutputStream out = new FileOutputStream(temp)) {
            values.store(out, null);
            out.getFD().sync();
        } catch (IOException failure) {
            temp.delete();
            throw new IllegalStateException("component states not saved", failure);
        }
        if (!temp.renameTo(file)) {
            temp.delete();
            throw new IllegalStateException("component states not saved: rename failed");
        }
    }
}
