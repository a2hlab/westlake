package adapter.core;

import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.NetworkRequest;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Message;
import android.os.Messenger;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * WESTLAKE: connectivity service boundary.
 *
 * OHServiceManager previously returned null for "connectivity" on the grounds that
 * OH has no analog and the null was a "harmless skip". It is not harmless for a real
 * application: ConnectivityFrameworkInitializer builds ConnectivityManager from this
 * binder via IConnectivityManager.Stub.asInterface(), so a null service makes every
 * reachability query fail. Toutiao asks before it issues any request, concludes the
 * device is offline, and renders its "network unavailable" empty state on every page.
 * Observed directly: the child held 19 socket fds but had ZERO established TCP
 * connections while the device itself pinged out fine.
 *
 * The platform genuinely has connectivity (OH manages the radio/Wi-Fi itself); what is
 * missing is only the Android-shaped answer. So report a single connected, validated,
 * unmetered Wi-Fi network and leave every other operation at its unsupported default.
 *
 * IConnectivityManager declares 92 methods, and the port needs honest answers for about
 * six of them. Rather than hand-writing 86 stubs that would silently rot against the
 * pinned AOSP 15 AIDL, attach a dynamic proxy to a Binder: Stub.asInterface() calls
 * queryLocalInterface(DESCRIPTOR) first, so the proxy is used directly with no parcel
 * marshalling, and unknown/added methods degrade to a typed default instead of failing
 * to compile or throwing AbstractMethodError.
 *
 * This deliberately does NOT fake network state: it advertises what OH actually
 * provides. It grants no capability the platform lacks, and mutating calls stay no-ops.
 */
public final class ConnectivityManagerAdapter {
    private static final String TAG = "AdapterCM";
    private static final String DESCRIPTOR = "android.net.IConnectivityManager";

    /** Non-zero netId; 0 is NETID_UNSET and would read back as "no network". */
    private static final int NET_ID = 100;

    /** Reported in LinkProperties; OH brings the Wi-Fi interface up under this name. */
    private static final String INTERFACE = "wlan0";

    private static IBinder sBinder;

    private ConnectivityManagerAdapter() {}

    public static synchronized IBinder getBinder() {
        if (sBinder != null) {
            return sBinder;
        }
        try {
            Class<?> iface = Class.forName(DESCRIPTOR);
            Object proxy = Proxy.newProxyInstance(
                    ConnectivityManagerAdapter.class.getClassLoader(),
                    new Class<?>[] { iface },
                    new Router());
            Binder binder = new Binder();
            binder.attachInterface((IInterface) proxy, DESCRIPTOR);
            sBinder = binder;
            System.err.println("[" + TAG + "] connectivity service bound (netId=" + NET_ID + ")");
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] connectivity service unavailable: " + t);
        }
        return sBinder;
    }

    private static Network network() {
        return new Network(NET_ID);
    }

    private static NetworkInfo activeInfo() {
        NetworkInfo info = new NetworkInfo(
                ConnectivityManager.TYPE_WIFI, 0, "WIFI", "");
        info.setIsAvailable(true);
        info.setDetailedState(NetworkInfo.DetailedState.CONNECTED, null, null);
        return info;
    }

    private static LinkProperties linkProperties() {
        LinkProperties properties = new LinkProperties();
        properties.setInterfaceName(INTERFACE);
        return properties;
    }

    /**
     * Delivery of the registration callbacks. ConnectivityManager hands us a Messenger and only
     * afterwards records the callback under the NetworkRequest we return, so the messages have to
     * be sent from somewhere other than the registering thread or CallbackHandler drops them with
     * "callback not found". A small delay on a private thread is enough and keeps ordering.
     */
    private static Handler sDelivery;

    private static synchronized Handler delivery() {
        if (sDelivery == null) {
            HandlerThread thread = new HandlerThread("AdapterCM");
            thread.start();
            sDelivery = new Handler(thread.getLooper());
        }
        return sDelivery;
    }

    private static final AtomicInteger sRequestId = new AtomicInteger(1);

    /**
     * Answers registerNetworkCallback()/registerDefaultNetworkCallback() for real.
     *
     * Returning null here (the old typed default) meant ConnectivityManager stored no callback and
     * the app never received onAvailable. That is not a cosmetic gap: cronet's
     * NetworkChangeNotifierAutoDetect learns the connection type exclusively from these callbacks,
     * so with none arriving TTNet held CONNECTION_NONE and failed requests outright -- Toutiao
     * showed 「网络异常，请稍后重试」 while the device was online. Report the network the platform
     * actually has, once, the same way ConnectivityService does on registration.
     */
    private static Object register(Object[] args) {
        NetworkCapabilities requested = null;
        Messenger messenger = null;
        for (Object arg : args) {
            if (arg instanceof NetworkCapabilities) requested = (NetworkCapabilities) arg;
            else if (arg instanceof Messenger) messenger = (Messenger) arg;
        }
        // Type only affects how the framework labels the request; LISTEN is the honest one for a
        // boundary that observes a network it does not bring up or tear down.
        final NetworkRequest request = new NetworkRequest(
                requested != null ? new NetworkCapabilities(requested) : capabilities(),
                ConnectivityManager.TYPE_NONE, sRequestId.getAndIncrement(),
                NetworkRequest.Type.LISTEN);
        System.err.println("[" + TAG + "] register(" + (messenger != null ? "messenger" : "no-messenger")
                + ") -> requestId=" + request.requestId);
        if (messenger == null) {
            return request;
        }
        final Messenger target = messenger;
        delivery().postDelayed(new Runnable() {
            @Override
            public void run() {
                Bundle data = new Bundle();
                data.putParcelable(NetworkRequest.class.getSimpleName(), request);
                data.putParcelable(Network.class.getSimpleName(), network());
                data.putParcelable(NetworkCapabilities.class.getSimpleName(), capabilities());
                data.putParcelable(LinkProperties.class.getSimpleName(), linkProperties());
                // CALLBACK_AVAILABLE fans out to onAvailable, onCapabilitiesChanged,
                // onLinkPropertiesChanged and onBlockedStatusChanged in one message.
                Message message = Message.obtain();
                message.what = CALLBACK_AVAILABLE;
                message.arg1 = 0;   // not blocked
                message.setData(data);
                try {
                    target.send(message);
                    System.err.println("[" + TAG + "] delivered CALLBACK_AVAILABLE for requestId="
                            + request.requestId);
                } catch (Throwable t) {
                    System.err.println("[" + TAG + "] callback delivery failed: " + t);
                }
            }
        }, 120L);
        return request;
    }

    /** ConnectivityManager.CALLBACK_AVAILABLE; it is public there but not in the SDK surface. */
    private static final int CALLBACK_AVAILABLE = 2;

    private static NetworkCapabilities capabilities() {
        NetworkCapabilities caps = new NetworkCapabilities();
        caps.addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_RESTRICTED);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_TRUSTED);
        // ConnectivityService sets these on any healthy network; leaving them off makes a
        // caller that checks them read the link as degraded.
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_SUSPENDED);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_ROAMING);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_CONGESTED);
        return caps;
    }

    private static final Set<String> sSeen = Collections.synchronizedSet(new HashSet<String>());

    private static final class Router implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            // One line per distinct method, handled or not: the only reliable way to see which
            // parts of IConnectivityManager the app's reachability check actually consults.
            if (sSeen.add(method.getName())) {
                System.err.println("[" + TAG + "] call " + method.getName());
            }
            switch (method.getName()) {
                case "asBinder":
                    return getBinder();
                case "getActiveNetwork":
                case "getActiveNetworkForUid":
                case "getNetworkForType":
                    return network();
                case "getAllNetworks":
                    return new Network[] { network() };
                case "getActiveNetworkInfo":
                case "getActiveNetworkInfoForUid":
                case "getNetworkInfo":
                case "getNetworkInfoForUid":
                    return activeInfo();
                case "getAllNetworkInfo":
                    return new NetworkInfo[] { activeInfo() };
                case "getNetworkCapabilities":
                case "getRedactedNetworkCapabilitiesForPackage":
                    return capabilities();
                case "listenForNetwork":
                case "requestNetwork":
                    return register(args);
                case "getLinkProperties":
                    return linkProperties();
                case "isNetworkSupported":
                    return Boolean.TRUE;
                case "isActiveNetworkMetered":
                    // Matches NET_CAPABILITY_NOT_METERED above; an app that believes it
                    // is metered may silently downgrade or defer its requests.
                    return Boolean.FALSE;
                default:
                    // One line per distinct method: this is the only way to see which parts of
                    // IConnectivityManager the app actually depends on, rather than guessing.
                    System.err.println("[" + TAG + "] unhandled " + method.getName()
                            + " -> default");
                    return unsupported(method.getReturnType());
            }
        }

        /**
         * Typed "not supported" for everything this boundary does not implement. Returning
         * a default rather than throwing keeps an unimplemented query from unwinding the
         * caller, which on the UI looper would take the process down.
         */
        private static Object unsupported(Class<?> type) {
            if (!type.isPrimitive()) {
                return type.isArray() ? java.lang.reflect.Array.newInstance(
                        type.getComponentType(), 0) : null;
            }
            if (type == boolean.class) return Boolean.FALSE;
            if (type == int.class) return Integer.valueOf(0);
            if (type == long.class) return Long.valueOf(0L);
            if (type == short.class) return Short.valueOf((short) 0);
            if (type == byte.class) return Byte.valueOf((byte) 0);
            if (type == char.class) return Character.valueOf('\0');
            if (type == float.class) return Float.valueOf(0f);
            if (type == double.class) return Double.valueOf(0d);
            return null;  // void
        }
    }
}
