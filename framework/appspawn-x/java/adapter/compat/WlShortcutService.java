package adapter.compat;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

/** Installs a typed, in-process Android shortcut service at the OH boundary. */
public final class WlShortcutService implements InvocationHandler {
    public static final class LocalBinder extends Binder {
        private IInterface iface;
        void setIface(IInterface value) { iface = value; }
        @Override public IInterface queryLocalInterface(String descriptor) { return iface; }
    }

    private IBinder self;

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) {
        String name = method.getName();
        if (name.equals("asBinder")) return self;
        if (name.equals("toString")) return "WlShortcutService";
        if (name.equals("hashCode")) return Integer.valueOf(System.identityHashCode(proxy));
        if (name.equals("equals")) {
            return Boolean.valueOf(args != null && args.length == 1 && proxy == args[0]);
        }
        return defaultFor(method.getReturnType());
    }

    private static Object defaultFor(Class<?> type) {
        if (type == void.class || type == Void.class) return null;
        if (type == boolean.class) return Boolean.FALSE;
        if (type == int.class) return Integer.valueOf(0);
        if (type == long.class) return Long.valueOf(0L);
        if (type == float.class) return Float.valueOf(0f);
        if (type == double.class) return Double.valueOf(0d);
        if (type == byte.class) return Byte.valueOf((byte) 0);
        if (type == short.class) return Short.valueOf((short) 0);
        if (type == char.class) return Character.valueOf('\0');
        if (type.getName().equals("android.content.pm.ParceledListSlice")) return emptySlice();
        if (List.class.isAssignableFrom(type)) return new ArrayList<Object>();
        return null;
    }

    private static Object emptySlice() {
        try {
            Class<?> type = Class.forName("android.content.pm.ParceledListSlice");
            Constructor<?> constructor = type.getConstructor(List.class);
            constructor.setAccessible(true);
            return constructor.newInstance(new ArrayList<Object>());
        } catch (Throwable first) {
            try {
                Class<?> type = Class.forName("android.content.pm.ParceledListSlice");
                return type.getMethod("emptyList").invoke(null);
            } catch (Throwable second) {
                return null;
            }
        }
    }

    public static String install() {
        try {
            ClassLoader loader = WlShortcutService.class.getClassLoader();
            Class<?> iface = Class.forName("android.content.pm.IShortcutService", false, loader);
            WlShortcutService handler = new WlShortcutService();
            Object proxy = Proxy.newProxyInstance(loader, new Class<?>[] { iface }, handler);
            LocalBinder binder = new LocalBinder();
            binder.setIface((IInterface) proxy);
            handler.self = binder;

            Class<?> manager = Class.forName("android.os.ServiceManager", false, loader);
            java.lang.reflect.Field field = manager.getDeclaredField("sCache");
            field.setAccessible(true);
            Object value = field.get(null);
            if (!(value instanceof java.util.Map)) return "sCache is not a Map";
            @SuppressWarnings("unchecked")
            java.util.Map<String, IBinder> cache = (java.util.Map<String, IBinder>) value;
            cache.put("shortcut", binder);
            IInterface readback = binder.queryLocalInterface("android.content.pm.IShortcutService");
            return "installed, queryLocalInterface="
                    + (readback != null && iface.isInstance(readback) ? "OK" : "WRONG");
        } catch (Throwable error) {
            return "FAILED: " + error.getClass().getName() + ": " + error.getMessage();
        }
    }

    private WlShortcutService() {}
}
