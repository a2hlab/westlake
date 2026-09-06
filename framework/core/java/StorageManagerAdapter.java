/*
 * StorageManagerAdapter.java
 *
 * The "mount" service boundary: makes the process report one primary external
 * storage volume instead of none.
 *
 * Why
 * ---
 * Environment.getExternalStorageDirectory() is Environment.getExternalDirs()[0],
 * and getExternalDirs() builds its array from IStorageManager.getVolumeList().
 * The adapter's mount stub returns an empty StorageVolume[], so that indexing
 * throws:
 *
 *     ArrayIndexOutOfBoundsException: length=0; index=0
 *
 * It throws from inside a <clinit>, which is what makes it so destructive: the
 * class is then permanently marked erroneous and every later touch of it fails
 * with NoClassDefFoundError, on threads that have nothing to do with storage.
 * On the reproduction target the first casualty was an ad-SDK preload task
 * (com.ss.android.ad.init.PreloadTask, on a background thread); the visible
 * symptom was SearchActivity dying much later on
 *
 *     NoClassDefFoundError: X.BdA
 *     Caused by: ExceptionInInitializerError
 *     Caused by: ArrayIndexOutOfBoundsException: length=0; index=0
 *
 * -- a corpse, several steps removed from the actual fault. Any component that
 * touches external storage during start-up hits the same edge; WeChat's VFS
 * core does it too.
 *
 * Reporting one emulated volume at /storage/emulated/0 is not a workaround for
 * a missing feature; it is what the platform is supposed to say. OH gives every
 * app a sandboxed data root, so a primary emulated volume is the accurate
 * answer, and every caller downstream then behaves normally.
 *
 * Where it is wired
 * -----------------
 * OHServiceManager.lookupAdapter() serves "mount" from here. The frontline
 * prototype could not do that -- it lived in the app's own jar -- so it wrote
 * itself into android.os.ServiceManager.sCache by reflection. That reached into
 * private framework state to install a service, which is precisely the kind of
 * thing this repository owns a service manager to avoid.
 *
 * What is still reflective, and why
 * ---------------------------------
 * Two things, both narrow and both deliberate:
 *
 *   * The delegating proxy. IStorageManager has around a hundred methods and
 *     this class has an opinion about exactly two of them. Subclassing
 *     IStorageManager.Stub would mean writing ninety-odd overrides that do
 *     nothing but forward, which is more code to read, more to get wrong, and
 *     it would break every time the interface gains a method. Interface
 *     delegation through java.lang.reflect.Proxy is the ordinary Java answer to
 *     that, and it is not the same act as reaching into another class's
 *     private fields.
 *
 *   * StorageVolume's constructor is @hide and its signature has changed across
 *     releases. Reflecting on it lets one source file work against more than
 *     one framework.jar; naming it directly would pin this file to a single
 *     platform revision for no benefit.
 */
package adapter.core;

import java.io.File;
import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.UUID;

import android.os.IBinder;

public final class StorageManagerAdapter {

    private static final String TAG = "[WL-STORAGE]";

    private static final String IFACE = "android.os.storage.IStorageManager";
    private static final String STORAGE_VOLUME = "android.os.storage.StorageVolume";

    /**
     * The primary emulated volume this adapter reports.
     *
     * /storage/emulated/0 is the path AOSP uses for the primary emulated volume
     * and the one app code has been written against for a decade. Reporting
     * anything else would be technically defensible and practically useless.
     */
    private static final String PRIMARY_PATH = "/storage/emulated/0";
    private static final String PRIMARY_ID = "primary";
    private static final String PRIMARY_DESCRIPTION = "Internal shared storage";
    private static final String STATE_MOUNTED = "mounted";

    private static final Object LOCK = new Object();
    private static StorageManagerAdapter sInstance;

    private final IBinder mBinder;

    private StorageManagerAdapter(IBinder binder) {
        mBinder = binder;
    }

    /**
     * The adapter, or null if this framework has no IStorageManager at all.
     *
     * Null rather than a throw: OHServiceManager treats a null the same way it
     * treats every other service it cannot provide, and a process that never
     * touches external storage should not fail to start over it.
     */
    public static StorageManagerAdapter getInstance() {
        synchronized (LOCK) {
            if (sInstance == null) {
                sInstance = create();
            }
            return sInstance;
        }
    }

    /** The binder to publish under the "mount" service name. */
    public IBinder asBinder() {
        return mBinder;
    }

    private static StorageManagerAdapter create() {
        try {
            Class<?> ifaceClass = Class.forName(IFACE);
            Object volumes = buildVolumeList();
            if (volumes == null) {
                System.err.println(TAG + " no StorageVolume could be built;"
                        + " leaving the mount service unprovided");
                return null;
            }
            // binder and interface refer to each other, so build both against a
            // shared holder and fill it in once they exist.
            Object[] pair = new Object[2];
            ClassLoader loader = ifaceClass.getClassLoader();
            Object iface = Proxy.newProxyInstance(loader, new Class<?>[] { ifaceClass },
                    new MountHandler(volumes, pair, true));
            Object binder = Proxy.newProxyInstance(loader, new Class<?>[] { IBinder.class },
                    new MountHandler(volumes, pair, false));
            pair[0] = binder;
            pair[1] = iface;
            System.err.println(TAG + " mount service provided; getVolumeList -> "
                    + PRIMARY_PATH);
            return new StorageManagerAdapter((IBinder) binder);
        } catch (Throwable t) {
            System.err.println(TAG + " mount service unavailable: " + t);
            return null;
        }
    }

    /** A StorageVolume[] holding exactly the primary emulated volume. */
    private static Object buildVolumeList() {
        Object volume = buildPrimaryVolume();
        if (volume == null) {
            return null;
        }
        try {
            Object array = Array.newInstance(Class.forName(STORAGE_VOLUME), 1);
            Array.set(array, 0, volume);
            return array;
        } catch (Throwable t) {
            System.err.println(TAG + " buildVolumeList failed: " + t);
            return null;
        }
    }

    /**
     * One primary, emulated, mounted, non-removable volume.
     *
     * The constructor arguments, in order, are: id, path, internalPath,
     * description, primary, removable, emulated, externallyManaged, allowMassStorage,
     * maxFileSize, owner, uuid, fsUuid, state.
     */
    private static Object buildPrimaryVolume() {
        try {
            Class<?> volumeClass = Class.forName(STORAGE_VOLUME);
            Class<?> userHandleClass = Class.forName("android.os.UserHandle");
            Object owner = Class.forName("android.os.Process")
                    .getMethod("myUserHandle").invoke(null);
            File path = new File(PRIMARY_PATH);

            Constructor<?> constructor = volumeClass.getDeclaredConstructor(
                    String.class, File.class, File.class, String.class,
                    boolean.class,      // primary
                    boolean.class,      // removable
                    boolean.class,      // emulated
                    boolean.class,      // externallyManaged
                    boolean.class,      // allowMassStorage
                    long.class,         // maxFileSize, 0 = no limit
                    userHandleClass, UUID.class, String.class, String.class);
            constructor.setAccessible(true);
            return constructor.newInstance(PRIMARY_ID, path, path, PRIMARY_DESCRIPTION,
                    true, false, true, false, false, 0L, owner, null, null, STATE_MOUNTED);
        } catch (Throwable t) {
            System.err.println(TAG + " buildPrimaryVolume failed: " + t);
            return null;
        }
    }

    /**
     * Answers the two volume queries; everything else gets a benign default.
     *
     * There is no real IStorageManager behind this on OH -- there is no volume
     * daemon to delegate to -- so unhandled methods return the zero value for
     * their return type rather than throwing. A caller asking this adapter to
     * format a volume should get "no" and carry on, not an exception from a
     * service that was never going to do it.
     */
    private static final class MountHandler implements InvocationHandler {

        private final Object mVolumes;
        private final Object[] mPair;      // [0] = binder, [1] = interface
        private final boolean mIsInterface;

        MountHandler(Object volumes, Object[] pair, boolean isInterface) {
            mVolumes = volumes;
            mPair = pair;
            mIsInterface = isInterface;
        }

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();

            // Binder plumbing: the two proxies have to point at each other, or
            // IStorageManager.Stub.asInterface() will not recognise the binder
            // as local and will try to transact with it.
            switch (name) {
                case "queryLocalInterface":
                    return mPair[1];
                case "asBinder":
                    return mPair[0];
                case "isBinderAlive":
                case "pingBinder":
                    return Boolean.TRUE;
                case "getInterfaceDescriptor":
                    return IFACE;
                case "toString":
                    return "StorageManagerAdapter" + (mIsInterface ? "$iface" : "$binder");
                case "hashCode":
                    return System.identityHashCode(proxy);
                case "equals":
                    return proxy == (args == null || args.length == 0 ? null : args[0]);
                case "getVolumeList":
                case "getVolumes":
                    return mVolumes;
                default:
                    break;
            }
            return defaultValue(method.getReturnType());
        }

        private static Object defaultValue(Class<?> type) {
            if (type == void.class) {
                return null;
            }
            if (!type.isPrimitive()) {
                return null;
            }
            if (type == boolean.class) {
                return Boolean.FALSE;
            }
            if (type == long.class) {
                return 0L;
            }
            if (type == double.class) {
                return 0d;
            }
            if (type == float.class) {
                return 0f;
            }
            if (type == char.class) {
                return (char) 0;
            }
            if (type == byte.class) {
                return (byte) 0;
            }
            if (type == short.class) {
                return (short) 0;
            }
            return 0;
        }
    }
}
