package adapter.compat;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/** Installs typed local media-session interfaces for a runtime without Android SystemServer. */
public final class WlMediaSession {
    public static final class LocalBinder extends Binder {}

    static final class Inert implements InvocationHandler {
        private final String label;
        private final ClassLoader loader;
        private final IBinder binder;

        Inert(String label, ClassLoader loader, IBinder binder) {
            this.label = label;
            this.loader = loader;
            this.binder = binder;
        }

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if (name.equals("asBinder")) return binder != null ? binder : proxy;
            if (name.equals("toString")) return label;
            if (name.equals("hashCode")) return Integer.valueOf(System.identityHashCode(proxy));
            if (name.equals("equals")) {
                return Boolean.valueOf(args != null && args.length == 1 && args[0] == proxy);
            }
            Class<?> type = method.getReturnType();
            if (type == void.class) return null;
            if (type == boolean.class) return Boolean.FALSE;
            if (type == int.class) return Integer.valueOf(0);
            if (type == long.class) return Long.valueOf(0L);
            if (type == float.class) return Float.valueOf(0f);
            if (type == double.class) return Double.valueOf(0d);
            if (type.isInterface()) {
                Object value = make(loader, type.getName(), "Wl" + type.getSimpleName());
                if (value != null) return value;
            }
            return null;
        }
    }

    static final class FetcherHandler implements InvocationHandler {
        private final ClassLoader loader;
        private Object cached;

        FetcherHandler(ClassLoader loader) { this.loader = loader; }

        @Override
        public synchronized Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if (name.equals("toString")) return "WlMediaSessionFetcher";
            if (name.equals("hashCode")) return Integer.valueOf(System.identityHashCode(proxy));
            if (name.equals("equals")) {
                return Boolean.valueOf(args != null && args.length == 1 && args[0] == proxy);
            }
            if (cached != null) return cached;
            try {
                Object context = args != null && args.length > 0 ? args[0] : null;
                Class<?> manager = Class.forName(
                        "android.media.session.MediaSessionManager", false, loader);
                Class<?> contextType = Class.forName("android.content.Context", false, loader);
                java.lang.reflect.Constructor<?> constructor =
                        manager.getDeclaredConstructor(contextType);
                constructor.setAccessible(true);
                cached = constructor.newInstance(context);
                return cached;
            } catch (Throwable error) {
                Throwable cause = error instanceof java.lang.reflect.InvocationTargetException
                        && error.getCause() != null ? error.getCause() : error;
                System.err.println("[WESTLAKE-469] MediaSessionManager build failed: " + cause);
                return null;
            }
        }
    }

    static Object make(ClassLoader loader, String name, String label) {
        return make(loader, name, label, null);
    }

    static Object make(ClassLoader loader, String name, String label, IBinder binderValue) {
        try {
            Class<?> iface = Class.forName(name, false, loader);
            Class<?> binder = Class.forName("android.os.IBinder", false, loader);
            return Proxy.newProxyInstance(
                    loader, new Class<?>[] { iface, binder },
                    new Inert(label, loader, binderValue));
        } catch (Throwable error) {
            return null;
        }
    }

    @SuppressWarnings("unchecked")
    static String registerFetcher(ClassLoader loader) {
        try {
            Class<?> registry = Class.forName("android.app.SystemServiceRegistry", false, loader);
            java.lang.reflect.Field field = registry.getDeclaredField("SYSTEM_SERVICE_FETCHERS");
            field.setAccessible(true);
            Object value = field.get(null);
            if (!(value instanceof java.util.Map)) return "fetchers not a Map";
            java.util.Map<String, Object> fetchers = (java.util.Map<String, Object>) value;
            if (fetchers.get("media_session") != null) return "already registered";
            Class<?> fetcherType = null;
            for (Class<?> nested : registry.getDeclaredClasses()) {
                if (nested.getSimpleName().equals("ServiceFetcher")) {
                    fetcherType = nested;
                    break;
                }
            }
            if (fetcherType == null) return "no ServiceFetcher interface";
            Object fetcher = Proxy.newProxyInstance(
                    loader, new Class<?>[] { fetcherType }, new FetcherHandler(loader));
            fetchers.put("media_session", fetcher);
            return "registered (" + fetchers.size() + " fetchers)";
        } catch (Throwable error) {
            return "FAILED " + error;
        }
    }

    public static String install() {
        try {
            ClassLoader loader = WlMediaSession.class.getClassLoader();
            Class<?> managerType = Class.forName(
                    "android.media.session.ISessionManager", false, loader);
            LocalBinder binder = new LocalBinder();
            Object manager = make(loader, "android.media.session.ISessionManager",
                    "WlSessionManager", binder);
            if (manager == null) return "could not build ISessionManager proxy";
            binder.attachInterface((IInterface) manager,
                    "android.media.session.ISessionManager");

            Class<?> serviceManager = Class.forName("android.os.ServiceManager", false, loader);
            java.lang.reflect.Field field = serviceManager.getDeclaredField("sCache");
            field.setAccessible(true);
            Object value = field.get(null);
            if (!(value instanceof java.util.Map)) return "sCache is not a Map";
            @SuppressWarnings("unchecked")
            java.util.Map<String, IBinder> cache = (java.util.Map<String, IBinder>) value;
            cache.put("media_session", binder);
            IInterface readback = binder.queryLocalInterface(
                    "android.media.session.ISessionManager");
            IBinder interfaceBinder = readback == null ? null : readback.asBinder();
            return "installed media_session, queryLocalInterface="
                    + (readback != null && managerType.isInstance(readback)
                            && interfaceBinder == binder ? "OK" : "WRONG")
                    + "; fetcher: " + registerFetcher(loader);
        } catch (Throwable error) {
            return "FAILED: " + error.getClass().getName() + ": " + error.getMessage();
        }
    }

    private WlMediaSession() {}
}
