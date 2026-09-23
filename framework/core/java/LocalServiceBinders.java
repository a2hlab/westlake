package adapter.core;

import android.content.ClipData;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * System services with no OH service to route to, answered in process with what an idle,
 * ordinary phone reports.
 *
 * Without a binder, SystemServiceRegistry's fetcher throws ServiceNotFoundException and
 * getSystemService returns null. Java callers often check; Kotlin's {@code as UiModeManager} does
 * not, and inside a React Native host function the throw becomes a JS exception: Burger King's
 * expo-device reads the UI mode type while its JS bundle loads, and the null took down the app's
 * root component. The same app casts PowerManager, AlarmManager and ClipboardManager the same way.
 *
 * Same shape as LocationManagerAdapter: a Proxy over the AIDL interface attached to a local
 * Binder, so Stub.asInterface resolves it without a transaction; methods not listed answer the
 * type default. Nothing here reaches OH: alarms are accepted and never delivered, the clipboard is
 * local to the process, and power reports an interactive device without power saving.
 */
public final class LocalServiceBinders {
    private static final String TAG = "WESTLAKE-LOCAL-SERVICE";
    private static final Object DEFAULT = new Object();
    private static final Map<String, IBinder> sBinders = new HashMap<>();
    private static final Set<String> sSeen = Collections.synchronizedSet(new HashSet<String>());
    private static volatile ClipData sClip;

    private interface Answers {
        Object answer(String method, Object[] args);
    }

    private LocalServiceBinders() {}

    /** The binder for {@code name}, or null when this class does not answer that service. */
    public static synchronized IBinder get(String name) {
        IBinder binder = sBinders.get(name);
        if (binder != null) return binder;
        try {
            switch (name) {
                case "uimode":
                    binder = proxy(name, "android.app.IUiModeManager", LocalServiceBinders::uiMode);
                    break;
                case "power":
                    binder = proxy(name, "android.os.IPowerManager", LocalServiceBinders::power);
                    break;
                case "thermalservice": {
                    // PowerManager's fetcher also requires the thermal service.
                    westlake.impl.IThermalServiceImpl thermal = new westlake.impl.IThermalServiceImpl();
                    thermal.attachInterface(thermal, "android.os.IThermalService");
                    binder = thermal;
                    break;
                }
                case "alarm":
                    binder = proxy(name, "android.app.IAlarmManager", LocalServiceBinders::alarm);
                    break;
                case "clipboard":
                    binder = proxy(name, "android.content.IClipboard", LocalServiceBinders::clipboard);
                    break;
                case "account":
                    binder = proxy(name, "android.accounts.IAccountManager", LocalServiceBinders::account);
                    break;
                default:
                    return null;
            }
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] " + name + " unavailable: " + t);
            return null;
        }
        sBinders.put(name, binder);
        System.err.println("[" + TAG + "] " + name + " bound in process");
        return binder;
    }

    private static IBinder proxy(String name, String descriptor, Answers answers) throws ClassNotFoundException {
        Class<?> iface = Class.forName(descriptor);
        Binder binder = new Binder();
        Object local = Proxy.newProxyInstance(LocalServiceBinders.class.getClassLoader(), new Class<?>[] {iface},
                new InvocationHandler() {
                    @Override
                    public Object invoke(Object self, Method method, Object[] args) {
                        String call = method.getName();
                        if ("asBinder".equals(call)) return binder;
                        if (sSeen.add(name + "." + call)) {
                            System.err.println("[" + TAG + "] " + name + "." + call);
                        }
                        Object value = answers.answer(call, args);
                        return value == DEFAULT ? defaultValue(method.getReturnType()) : value;
                    }
                });
        binder.attachInterface((IInterface) local, descriptor);
        return binder;
    }

    private static Object uiMode(String method, Object[] args) {
        switch (method) {
            case "getCurrentModeType":
                return Configuration.UI_MODE_TYPE_NORMAL;
            case "getNightMode": {
                int night = Resources.getSystem().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK;
                return night == Configuration.UI_MODE_NIGHT_YES ? 2 /* MODE_NIGHT_YES */ : 1 /* MODE_NIGHT_NO */;
            }
            case "getNightModeCustomType":
                return -1;     // MODE_NIGHT_CUSTOM_TYPE_UNKNOWN
            case "getAttentionModeThemeOverlay":
                return 1000;   // MODE_ATTENTION_THEME_OVERLAY_OFF
            case "getProjectingPackages":
                return new ArrayList<String>();
            default:
                return DEFAULT;   // car mode off, nothing locked, standard contrast (0f)
        }
    }

    private static Object power(String method, Object[] args) {
        switch (method) {
            case "isInteractive":
            case "isDisplayInteractive":
            case "isWakeLockLevelSupported":
                return Boolean.TRUE;
            default:
                return DEFAULT;   // no power save, not idle; wake locks are accepted
        }
    }

    private static Object alarm(String method, Object[] args) {
        return DEFAULT;   // set() is accepted, nothing is ever delivered; no next alarm clock
    }

    private static Object account(String method, Object[] args) {
        // There is no account database on the board and no authenticator to add one to, so the
        // truthful answer is "signed in nowhere": an empty account list, no features, no token.
        // Wikipedia asks through AccountManager.get(context) in BaseActivity.onCreate and never
        // null-checks the manager, so the binder has to exist even though it owns nothing.
        switch (method) {
            case "addAccountExplicitly":
            case "removeAccountExplicitly":
            case "hasFeatures":
            case "someUserHasAccount":
            case "accountAuthenticated":
                return Boolean.FALSE;
            default:
                return DEFAULT;   // arrays come back empty, everything else null or zero
        }
    }

    private static Object clipboard(String method, Object[] args) {
        ClipData clip = sClip;
        switch (method) {
            case "setPrimaryClip":
            case "setPrimaryClipAsPackage":
                sClip = args != null && args.length > 0 && args[0] instanceof ClipData ? (ClipData) args[0] : null;
                return DEFAULT;
            case "clearPrimaryClip":
                sClip = null;
                return DEFAULT;
            case "getPrimaryClip":
                return clip;
            case "getPrimaryClipDescription":
                return clip != null ? clip.getDescription() : null;
            case "hasPrimaryClip":
                return clip != null;
            case "hasClipboardText":
                return clip != null && clip.getItemCount() > 0 && clip.getItemAt(0).getText() != null;
            default:
                return DEFAULT;
        }
    }

    private static Object defaultValue(Class<?> type) {
        // An empty array, never null: callers iterate what a service hands back without checking,
        // and a null Account[] from getAccounts() throws inside the caller rather than here.
        if (type != null && type.isArray()) {
            return java.lang.reflect.Array.newInstance(type.getComponentType(), 0);
        }
        if (type == null || !type.isPrimitive()) return null;
        if (type == boolean.class) return Boolean.FALSE;
        if (type == byte.class) return (byte) 0;
        if (type == short.class) return (short) 0;
        if (type == char.class) return (char) 0;
        if (type == int.class) return 0;
        if (type == long.class) return 0L;
        if (type == float.class) return 0f;
        if (type == double.class) return 0d;
        return null;   // void
    }
}
