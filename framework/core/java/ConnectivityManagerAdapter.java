package adapter.core;

import android.net.ConnectivityManager;
import android.net.InetAddresses;
import android.net.IpPrefix;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.NetworkRequest;
import android.net.ProxyInfo;
import android.net.RouteInfo;
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
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/** Android connectivity facade backed by the current OpenHarmony default network. */
public final class ConnectivityManagerAdapter {
    private static final String TAG = "AdapterCM";
    private static final String DESCRIPTOR = "android.net.IConnectivityManager";
    private static final long INITIAL_CALLBACK_DELAY_MS = 120L;
    private static final long POLL_INTERVAL_MS = 1000L;

    // Values from NetConn_NetBearerType. Keep the wire values isolated at the boundary.
    private static final int OH_BEARER_CELLULAR = 0;
    private static final int OH_BEARER_WIFI = 1;
    private static final int OH_BEARER_BLUETOOTH = 2;
    private static final int OH_BEARER_ETHERNET = 3;
    private static final int OH_BEARER_VPN = 4;

    private static final int CALLBACK_AVAILABLE = 2;
    private static final int CALLBACK_LOST = 4;
    private static final int CALLBACK_UNAVAIL = 5;
    private static final int CALLBACK_CAP_CHANGED = 6;
    private static final int CALLBACK_IP_CHANGED = 7;

    private static final AtomicInteger sRequestId = new AtomicInteger(1);
    private static final ConcurrentHashMap<Integer, Registration> sRegistrations =
            new ConcurrentHashMap<Integer, Registration>();
    private static final Set<String> sSeen = Collections.synchronizedSet(new HashSet<String>());
    private static IBinder sBinder;
    private static Handler sDelivery;
    private static boolean sPolling;

    private ConnectivityManagerAdapter() {}

    public static synchronized IBinder getBinder() {
        if (sBinder != null) return sBinder;
        try {
            Class<?> iface = Class.forName(DESCRIPTOR);
            Object proxy = Proxy.newProxyInstance(ConnectivityManagerAdapter.class.getClassLoader(),
                    new Class<?>[] { iface }, new Router());
            Binder binder = new Binder();
            binder.attachInterface((IInterface) proxy, DESCRIPTOR);
            sBinder = binder;
            OhConnectivityState.Snapshot initial = OhConnectivityState.query();
            System.err.println("[" + TAG + "] connectivity service bound to OH state: "
                    + initial.diagnosticSignature());
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] connectivity service unavailable: " + t);
        }
        return sBinder;
    }

    private static Network network(OhConnectivityState.Snapshot state) {
        return state.available ? new Network(state.netId) : null;
    }

    private static int legacyType(OhConnectivityState.Snapshot state) {
        if (state.hasBearer(OH_BEARER_VPN)) return ConnectivityManager.TYPE_VPN;
        if (state.hasBearer(OH_BEARER_WIFI)) return ConnectivityManager.TYPE_WIFI;
        if (state.hasBearer(OH_BEARER_CELLULAR)) return ConnectivityManager.TYPE_MOBILE;
        if (state.hasBearer(OH_BEARER_ETHERNET)) return ConnectivityManager.TYPE_ETHERNET;
        if (state.hasBearer(OH_BEARER_BLUETOOTH)) return ConnectivityManager.TYPE_BLUETOOTH;
        return ConnectivityManager.TYPE_NONE;
    }

    private static NetworkInfo activeInfo(OhConnectivityState.Snapshot state) {
        if (!state.available) return null;
        int type = legacyType(state);
        String name = ConnectivityManager.getNetworkTypeName(type);
        NetworkInfo info = new NetworkInfo(type, 0, name, "");
        info.setIsAvailable(true);
        info.setDetailedState(NetworkInfo.DetailedState.CONNECTED, null, null);
        return info;
    }

    private static LinkProperties linkProperties(OhConnectivityState.Snapshot state) {
        if (!state.available) return null;
        LinkProperties properties = new LinkProperties();
        if (state.propertiesRc == 0 || state.kernelFallback) {
            if (state.interfaceName != null && !state.interfaceName.isEmpty()) {
                properties.setInterfaceName(state.interfaceName);
            }
            if (state.mtu > 0) properties.setMtu(state.mtu);
            for (String address : state.linkAddresses) {
                try {
                    properties.addLinkAddress(new LinkAddress(address));
                } catch (IllegalArgumentException badNumericAddress) {
                    System.err.println("[" + TAG + "] skipped invalid OH link address: "
                            + badNumericAddress);
                }
            }
            for (String resolver : state.dnsServers) {
                try {
                    properties.addDnsServer(InetAddresses.parseNumericAddress(resolver));
                } catch (IllegalArgumentException badNumericAddress) {
                    System.err.println("[" + TAG + "] skipped invalid OH resolver: "
                            + badNumericAddress);
                }
            }
            for (String wireRoute : state.routes) {
                try {
                    int gatewayMark = wireRoute.indexOf('@');
                    if (gatewayMark >= 0 &&
                            (gatewayMark == 0 || gatewayMark == wireRoute.length() - 1 ||
                             wireRoute.indexOf('@', gatewayMark + 1) >= 0)) {
                        throw new IllegalArgumentException("invalid route wire format");
                    }
                    String destination = gatewayMark < 0
                            ? wireRoute : wireRoute.substring(0, gatewayMark);
                    java.net.InetAddress gateway = gatewayMark < 0 ? null
                            : InetAddresses.parseNumericAddress(
                                    wireRoute.substring(gatewayMark + 1));
                    properties.addRoute(new RouteInfo(new IpPrefix(destination), gateway,
                            properties.getInterfaceName()));
                } catch (IllegalArgumentException badRoute) {
                    System.err.println("[" + TAG + "] skipped invalid OH route: " + badRoute);
                }
            }
            ProxyInfo proxy = proxyInfo(state);
            if (proxy != null) properties.setHttpProxy(proxy);
        }
        return properties;
    }

    private static ProxyInfo buildProxyInfo(String host, int port, String[] exclusions) {
        if (host == null || host.isEmpty() || port <= 0 || port > 65535 ||
                exclusions == null) return null;
        try {
            ProxyInfo proxy = ProxyInfo.buildDirectProxy(host, port,
                    java.util.Arrays.asList(exclusions));
            return proxy.isValid() ? proxy : null;
        } catch (RuntimeException invalidProxy) {
            System.err.println("[" + TAG + "] skipped invalid OH proxy: " + invalidProxy);
            return null;
        }
    }

    private static ProxyInfo proxyInfo(OhConnectivityState.Snapshot state) {
        return state.available && state.proxyRc == 0
                ? buildProxyInfo(state.proxyHost, state.proxyPort, state.proxyExclusions)
                : null;
    }

    private static NetworkCapabilities capabilities(OhConnectivityState.Snapshot state) {
        if (!state.available) return null;
        NetworkCapabilities caps = new NetworkCapabilities();
        if (state.hasBearer(OH_BEARER_CELLULAR)) {
            caps.addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR);
        }
        if (state.hasBearer(OH_BEARER_WIFI)) {
            caps.addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
        }
        if (state.hasBearer(OH_BEARER_BLUETOOTH)) {
            caps.addTransportType(NetworkCapabilities.TRANSPORT_BLUETOOTH);
        }
        if (state.hasBearer(OH_BEARER_ETHERNET)) {
            caps.addTransportType(NetworkCapabilities.TRANSPORT_ETHERNET);
        }
        if (state.hasBearer(OH_BEARER_VPN)) {
            caps.addTransportType(NetworkCapabilities.TRANSPORT_VPN);
        }
        if (state.hasInternet()) caps.addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
        if (state.isValidated()) caps.addCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
        if (!state.isMetered()) caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED);
        if (state.isNotVpn()) caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN);

        // These describe the Android application's local policy view, not OH radio state.
        // The bridge does not impose restricted, suspended, roaming, or congested status.
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_RESTRICTED);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_TRUSTED);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_SUSPENDED);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_ROAMING);
        caps.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_CONGESTED);
        if (state.upKbps > 0) caps.setLinkUpstreamBandwidthKbps(state.upKbps);
        if (state.downKbps > 0) caps.setLinkDownstreamBandwidthKbps(state.downKbps);
        return caps;
    }

    private static synchronized Handler delivery() {
        if (sDelivery == null) {
            HandlerThread thread = new HandlerThread("AdapterCM-OH-state");
            thread.start();
            sDelivery = new Handler(thread.getLooper());
        }
        return sDelivery;
    }

    private static Object register(String method, Object[] args) {
        boolean listen = "listenForNetwork".equals(method);
        int minimum = listen ? 3 : 8;
        int capsIndex = listen ? 0 : 1;
        if (args == null || args.length < minimum ||
                (args[capsIndex] != null &&
                        !(args[capsIndex] instanceof NetworkCapabilities)) ||
                !(args[listen ? 1 : 3] instanceof Messenger)) {
            throw new IllegalArgumentException("Invalid " + method + " arguments");
        }
        NetworkCapabilities requested = listen ? (NetworkCapabilities) args[0]
                : (NetworkCapabilities) args[1];
        Messenger messenger = (Messenger) args[listen ? 1 : 3];
        NetworkRequest.Type type = NetworkRequest.Type.LISTEN;
        int timeoutMs = 0;
        int legacy = ConnectivityManager.TYPE_NONE;
        if (!listen) {
            if (!(args[2] instanceof Integer) || !(args[4] instanceof Integer) ||
                    !(args[6] instanceof Integer)) {
                throw new IllegalArgumentException("Invalid requestNetwork arguments");
            }
            int ordinal = ((Integer) args[2]).intValue();
            NetworkRequest.Type[] types = NetworkRequest.Type.values();
            if (ordinal < 0 || ordinal >= types.length) {
                throw new IllegalArgumentException("Unsupported request type " + ordinal);
            }
            type = types[ordinal];
            timeoutMs = ((Integer) args[4]).intValue();
            if (timeoutMs < 0) throw new IllegalArgumentException("Negative request timeout");
            legacy = ((Integer) args[6]).intValue();
        }
        OhConnectivityState.Snapshot state = OhConnectivityState.query();
        NetworkCapabilities requestCaps = requested != null
                ? new NetworkCapabilities(requested)
                : (state.available ? capabilities(state) : new NetworkCapabilities());
        NetworkRequest request = new NetworkRequest(requestCaps, legacy,
                sRequestId.getAndIncrement(), type);
        System.err.println("[" + TAG + "] register(" + method + ", " + type
                + ", timeoutMs=" + timeoutMs + ") -> requestId=" + request.requestId);
        Registration registration = new Registration(request, requested, messenger, timeoutMs);
        sRegistrations.put(request.requestId, registration);
        startPolling();
        return request;
    }

    private static synchronized void startPolling() {
        if (sPolling) return;
        sPolling = true;
        delivery().postDelayed(sPoll, INITIAL_CALLBACK_DELAY_MS);
    }

    private static final Runnable sPoll = new Runnable() {
        @Override
        public void run() {
            OhConnectivityState.Snapshot current = OhConnectivityState.query();
            for (Registration registration : sRegistrations.values()) {
                updateRegistration(registration, current);
            }
            synchronized (ConnectivityManagerAdapter.class) {
                if (sRegistrations.isEmpty()) {
                    sPolling = false;
                } else {
                    delivery().postDelayed(this, POLL_INTERVAL_MS);
                }
            }
        }
    };

    private static void updateRegistration(Registration registration,
            OhConnectivityState.Snapshot current) {
        OhConnectivityState.Snapshot previous = registration.last;
        if (!registration.knownSatisfied && registration.deadlineNanos != 0 &&
                System.nanoTime() - registration.deadlineNanos >= 0 &&
                (!current.querySucceeded || !matches(registration, current))) {
            if (sRegistrations.remove(registration.request.requestId, registration)) {
                send(registration, CALLBACK_UNAVAIL, current);
            }
            return;
        }
        // Unknown OH state is not reported as an invented loss transition.
        if (!current.querySucceeded) return;
        registration.last = current;
        boolean nowAvailable = matches(registration, current);
        if (previous == null) {
            if (nowAvailable) {
                registration.knownSatisfied = true;
                send(registration, CALLBACK_AVAILABLE, current);
            }
            return;
        }
        boolean wasAvailable = matches(registration, previous);
        if (wasAvailable && (!nowAvailable || previous.netId != current.netId)) {
            send(registration, CALLBACK_LOST, previous);
        }
        if (nowAvailable && (!wasAvailable || previous.netId != current.netId)) {
            registration.knownSatisfied = true;
            send(registration, CALLBACK_AVAILABLE, current);
            return;
        }
        if (!nowAvailable) return;
        if (!previous.capabilitySignature().equals(current.capabilitySignature())) {
            send(registration, CALLBACK_CAP_CHANGED, current);
        }
        if (!previous.linkSignature().equals(current.linkSignature())) {
            send(registration, CALLBACK_IP_CHANGED, current);
        }
    }

    private static boolean matches(Registration registration, OhConnectivityState.Snapshot state) {
        if (!state.available) return false;
        NetworkCapabilities actual = capabilities(state);
        return registration.requested == null
                || registration.requested.satisfiedByNetworkCapabilities(actual);
    }

    private static void send(Registration registration, int what,
            OhConnectivityState.Snapshot state) {
        Bundle data = new Bundle();
        data.putParcelable(NetworkRequest.class.getSimpleName(), registration.request);
        data.putParcelable(Network.class.getSimpleName(), network(state));
        NetworkCapabilities caps = capabilities(state);
        LinkProperties links = linkProperties(state);
        if (caps != null) data.putParcelable(NetworkCapabilities.class.getSimpleName(), caps);
        if (links != null) data.putParcelable(LinkProperties.class.getSimpleName(), links);
        Message message = Message.obtain();
        message.what = what;
        message.arg1 = 0;
        message.setData(data);
        try {
            registration.target.send(message);
            System.err.println("[" + TAG + "] delivered callback=" + what + " requestId="
                    + registration.request.requestId + " netId=" + state.netId);
        } catch (Throwable t) {
            sRegistrations.remove(registration.request.requestId);
            System.err.println("[" + TAG + "] callback delivery failed; removed requestId="
                    + registration.request.requestId + ": " + t);
        }
    }

    private static void release(Object[] args) {
        if (args == null) return;
        for (Object arg : args) {
            if (arg instanceof NetworkRequest) {
                NetworkRequest request = (NetworkRequest) arg;
                Registration removed = sRegistrations.remove(request.requestId);
                System.err.println("[" + TAG + "] release requestId=" + request.requestId
                        + " registered=" + (removed != null));
                return;
            }
        }
    }

    private static boolean supportsType(OhConnectivityState.Snapshot state, Object[] args) {
        if (!state.available || args == null || args.length == 0 || !(args[0] instanceof Integer)) {
            return false;
        }
        return ((Integer) args[0]).intValue() == legacyType(state);
    }

    private static boolean isCurrentNetwork(OhConnectivityState.Snapshot state, Object[] args) {
        return state.available && args != null && args.length > 0
                && args[0] instanceof Network && ((Network) args[0]).netId == state.netId;
    }

    private static NetworkCapabilities redactedCapabilities(Object[] args) {
        if (args == null || args.length < 2 ||
                !(args[0] instanceof NetworkCapabilities) || !(args[1] instanceof Integer)) {
            throw new IllegalArgumentException("Missing capabilities or target UID");
        }
        if (((Integer) args[1]).intValue() != Binder.getCallingUid()) {
            throw new SecurityException("Cannot redact capabilities for another UID");
        }
        NetworkCapabilities source = (NetworkCapabilities) args[0];
        NetworkCapabilities result = new NetworkCapabilities(source,
                source.getApplicableRedactions());
        result.setUids(null);
        result.setSSID(null);
        result.setAdministratorUids(new int[0]);
        result.setAllowedUids(new android.util.ArraySet<Integer>());
        result.setUnderlyingNetworks(null);
        if (result.getNetworkSpecifier() != null) {
            result.setNetworkSpecifier(result.getNetworkSpecifier().redact());
        }
        return result;
    }

    private static final class Registration {
        final NetworkRequest request;
        final NetworkCapabilities requested;
        final Messenger target;
        final long deadlineNanos;
        volatile OhConnectivityState.Snapshot last;
        volatile boolean knownSatisfied;

        Registration(NetworkRequest request, NetworkCapabilities requested, Messenger target,
                int timeoutMs) {
            this.request = request;
            this.requested = requested == null ? null : new NetworkCapabilities(requested);
            this.target = target;
            this.deadlineNanos = timeoutMs == 0 ? 0
                    : System.nanoTime() + timeoutMs * 1_000_000L;
        }
    }

    private static final class Router implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            if (sSeen.add(method.getName())) {
                System.err.println("[" + TAG + "] call " + method.getName());
            }
            String name = method.getName();
            if ("asBinder".equals(name)) return getBinder();
            if ("listenForNetwork".equals(name) || "requestNetwork".equals(name)) {
                return register(name, args);
            }
            if ("releaseNetworkRequest".equals(name)) {
                release(args);
                return null;
            }

            OhConnectivityState.Snapshot state = OhConnectivityState.query();
            switch (name) {
                case "getActiveNetwork":
                case "getActiveNetworkForUid":
                    return network(state);
                case "getNetworkForType":
                    return supportsType(state, args) ? network(state) : null;
                case "getAllNetworks":
                    return state.available ? new Network[] { network(state) } : new Network[0];
                case "getActiveNetworkInfo":
                case "getActiveNetworkInfoForUid":
                    return activeInfo(state);
                case "getNetworkInfo":
                    return supportsType(state, args) ? activeInfo(state) : null;
                case "getNetworkInfoForUid":
                    return isCurrentNetwork(state, args) ? activeInfo(state) : null;
                case "getAllNetworkInfo":
                    NetworkInfo info = activeInfo(state);
                    return info == null ? new NetworkInfo[0] : new NetworkInfo[] { info };
                case "getNetworkCapabilities":
                    return isCurrentNetwork(state, args) ? capabilities(state) : null;
                case "getRedactedNetworkCapabilitiesForPackage":
                    return redactedCapabilities(args);
                case "getLinkProperties":
                    return isCurrentNetwork(state, args) ? linkProperties(state) : null;
                case "getProxyForNetwork":
                    return args == null || args.length == 0 || args[0] == null ||
                            isCurrentNetwork(state, args) ? proxyInfo(state) : null;
                case "isNetworkSupported":
                    return Boolean.valueOf(supportsType(state, args));
                case "isActiveNetworkMetered":
                    return Boolean.valueOf(state.isMetered());
                default:
                    System.err.println("[" + TAG + "] unhandled " + name + " -> default");
                    return unsupported(method.getReturnType());
            }
        }

        private static Object unsupported(Class<?> type) {
            if (!type.isPrimitive()) {
                return type.isArray() ? java.lang.reflect.Array.newInstance(type.getComponentType(), 0) : null;
            }
            if (type == boolean.class) return Boolean.FALSE;
            if (type == int.class) return Integer.valueOf(0);
            if (type == long.class) return Long.valueOf(0L);
            if (type == short.class) return Short.valueOf((short) 0);
            if (type == byte.class) return Byte.valueOf((byte) 0);
            if (type == char.class) return Character.valueOf('\0');
            if (type == float.class) return Float.valueOf(0f);
            if (type == double.class) return Double.valueOf(0d);
            return null;
        }
    }
}
