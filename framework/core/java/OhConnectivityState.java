package adapter.core;

import java.util.Arrays;

/** Reads the current OpenHarmony default network through the source JNI boundary. */
final class OhConnectivityState {
    private static final String TAG = "OhConnectivityState";
    private static final int CAP_NOT_METERED = 11;
    private static final int CAP_INTERNET = 12;
    private static final int CAP_NOT_VPN = 15;
    private static final int CAP_VALIDATED = 16;
    private static volatile boolean sLoaded;
    private static volatile boolean sLoadAttempted;
    private static String sLastLogSignature;
    private static Snapshot sLastSnapshot;
    private static long sLastSnapshotNanos;

    private OhConnectivityState() {}

    private static native String nativeSnapshot();

    static synchronized Snapshot query() {
        long now = System.nanoTime();
        if (sLastSnapshot != null && now - sLastSnapshotNanos < 250_000_000L) {
            return sLastSnapshot;
        }
        if (!load()) return Snapshot.error("native-helper-unavailable");
        try {
            String raw = nativeSnapshot();
            Snapshot result = Snapshot.parse(raw);
            sLastSnapshot = result;
            sLastSnapshotNanos = now;
            logChanged(result);
            return result;
        } catch (Throwable t) {
            Snapshot result = Snapshot.error(t.getClass().getName() + ":" + t.getMessage());
            logChanged(result);
            return result;
        }
    }

    private static synchronized boolean load() {
        if (sLoadAttempted) return sLoaded;
        sLoadAttempted = true;
        String path = System.getenv("WESTLAKE_CONNECTIVITY_HELPER_PATH");
        try {
            if (path != null && !path.isEmpty()) System.load(path);
            else System.loadLibrary("oh_connectivity_state");
            sLoaded = true;
            System.err.println("[" + TAG + "] loaded source OH network helper"
                    + (path == null ? " from library path" : " from " + path));
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] native helper load failed: " + t);
        }
        return sLoaded;
    }

    private static synchronized void logChanged(Snapshot state) {
        String signature = state.diagnosticSignature();
        if (!signature.equals(sLastLogSignature)) {
            sLastLogSignature = signature;
            System.err.println("[" + TAG + "] OH snapshot " + signature);
        }
    }

    static final class Snapshot {
        final boolean querySucceeded;
        final boolean available;
        final boolean kernelFallback;
        final boolean uncachedOhRecovery;
        final int hasRc;
        final int defaultRc;
        final int netId;
        final int meteredRc;
        final boolean metered;
        final int capsRc;
        final long capabilityMask;
        final long bearerMask;
        final int upKbps;
        final int downKbps;
        final int propertiesRc;
        final String interfaceName;
        final int mtu;
        final int addressCount;
        final int dnsCount;
        final int routeCount;
        final String[] linkAddresses;
        final String[] dnsServers;
        final String[] routes;
        final int proxyRc;
        final String proxyHost;
        final int proxyPort;
        final String[] proxyExclusions;
        final String error;

        private Snapshot(boolean querySucceeded, boolean available, boolean kernelFallback,
                boolean uncachedOhRecovery,
                int hasRc, int defaultRc,
                int netId, int meteredRc, boolean metered, int capsRc, long capabilityMask,
                long bearerMask, int upKbps, int downKbps, int propertiesRc,
                String interfaceName, int mtu, int addressCount, int dnsCount, int routeCount,
                String[] linkAddresses, String[] dnsServers, String[] routes,
                int proxyRc, String proxyHost, int proxyPort, String[] proxyExclusions,
                String error) {
            this.querySucceeded = querySucceeded;
            this.available = available;
            this.kernelFallback = kernelFallback;
            this.uncachedOhRecovery = uncachedOhRecovery;
            this.hasRc = hasRc;
            this.defaultRc = defaultRc;
            this.netId = netId;
            this.meteredRc = meteredRc;
            this.metered = metered;
            this.capsRc = capsRc;
            this.capabilityMask = capabilityMask;
            this.bearerMask = bearerMask;
            this.upKbps = upKbps;
            this.downKbps = downKbps;
            this.propertiesRc = propertiesRc;
            this.interfaceName = interfaceName;
            this.mtu = mtu;
            this.addressCount = addressCount;
            this.dnsCount = dnsCount;
            this.routeCount = routeCount;
            this.linkAddresses = linkAddresses;
            this.dnsServers = dnsServers;
            this.routes = routes;
            this.proxyRc = proxyRc;
            this.proxyHost = proxyHost;
            this.proxyPort = proxyPort;
            this.proxyExclusions = proxyExclusions;
            this.error = error;
        }

        private static String[] numericList(String raw, int maximum) {
            if (raw.isEmpty()) return new String[0];
            String[] values = raw.split(";", -1);
            if (values.length > maximum) throw new IllegalArgumentException("too-many-values");
            for (String value : values) {
                if (value.isEmpty()) throw new IllegalArgumentException("empty-address");
            }
            return values;
        }

        static Snapshot parse(String raw) {
            if (raw == null) return error("null-native-snapshot");
            String[] f = raw.split("\\|", -1);
            if (f.length != 25 || !"3".equals(f[0])) return error("bad-native-snapshot:" + raw);
            try {
                int hasRc = Integer.parseInt(f[1]);
                int hasValue = Integer.parseInt(f[2]);
                boolean has = hasValue != 0 && hasValue != 4;
                int defaultRc = Integer.parseInt(f[3]);
                int netId = Integer.parseInt(f[4]);
                int meteredRc = Integer.parseInt(f[5]);
                boolean metered = Integer.parseInt(f[6]) != 0;
                int capsRc = Integer.parseInt(f[7]);
                long caps = Long.parseLong(f[8]);
                long bearers = Long.parseLong(f[9]);
                int up = Integer.parseInt(f[10]);
                int down = Integer.parseInt(f[11]);
                int propsRc = Integer.parseInt(f[12]);
                boolean fallback = hasValue == 2 && netId > 0;
                boolean recovered = hasValue == 3 && netId > 0 && capsRc == 0 &&
                        propsRc == 0;
                boolean available = fallback || recovered || (defaultRc == 0 && netId > 0);
                boolean confirmedAbsent = hasValue == 4 && netId == 0;
                boolean queryOk = available || confirmedAbsent || (hasRc == 0 && !has);
                return new Snapshot(queryOk, available, fallback, recovered,
                        hasRc, defaultRc, netId,
                        meteredRc, metered, capsRc, caps, bearers, up, down, propsRc,
                        f[13], Integer.parseInt(f[14]), Integer.parseInt(f[15]),
                        Integer.parseInt(f[16]), Integer.parseInt(f[17]),
                        numericList(f[18], 32), numericList(f[19], 32),
                        numericList(f[20], 64), Integer.parseInt(f[21]), f[22],
                        Integer.parseInt(f[23]), numericList(f[24], 256),
                        confirmedAbsent ? "oh-empty+kernel-no-route" : null);
            } catch (RuntimeException e) {
                return error("parse:" + e.getMessage());
            }
        }

        static Snapshot error(String error) {
            return new Snapshot(false, false, false, false, -1, -1, 0, -1, true, -1, 0, 0,
                    0, 0, -1, "", 0, 0, 0, 0,
                    new String[0], new String[0], new String[0],
                    -1, "", 0, new String[0], error);
        }

        boolean hasCapability(int capability) {
            return capability >= 0 && capability < 32
                    && (capabilityMask & (1L << capability)) != 0;
        }

        boolean hasBearer(int bearer) {
            return bearer >= 0 && bearer < 32 && (bearerMask & (1L << bearer)) != 0;
        }

        boolean isMetered() {
            if (!available) return true;
            if (meteredRc == 0) return metered;
            return !hasCapability(CAP_NOT_METERED);
        }

        boolean hasInternet() { return hasCapability(CAP_INTERNET); }
        boolean isValidated() { return hasCapability(CAP_VALIDATED); }
        boolean isNotVpn() { return hasCapability(CAP_NOT_VPN); }

        String networkSignature() {
            return querySucceeded + ":" + available + ":" + netId;
        }

        String capabilitySignature() {
            return capabilityMask + ":" + bearerMask + ":" + meteredRc + ":" + metered
                    + ":" + upKbps + ":" + downKbps;
        }

        String linkSignature() {
            return propertiesRc + ":" + interfaceName + ":" + mtu + ":"
                    + Arrays.toString(linkAddresses) + ":" + Arrays.toString(dnsServers)
                    + ":" + Arrays.toString(routes) + ":" + proxyRc + ":" + proxyHost
                    + ":" + proxyPort + ":" + Arrays.toString(proxyExclusions);
        }

        String diagnosticSignature() {
            return "ok=" + querySucceeded + " available=" + available + " backend="
                    + (kernelFallback ? "kernel-route" : uncachedOhRecovery
                            ? "oh-uncached+kernel-links" : "oh-netconn") + " netId=" + netId
                    + " iface=" + interfaceName + " mtu=" + mtu + " caps=0x"
                    + Long.toHexString(capabilityMask) + " bearers=0x"
                    + Long.toHexString(bearerMask) + " metered=" + isMetered()
                    + " counts=" + addressCount + "/" + dnsCount + "/" + routeCount
                    + " proxy=" + proxyRc + "/" + (!proxyHost.isEmpty())
                    + " rc=" + hasRc + "/" + defaultRc + "/" + meteredRc + "/"
                    + capsRc + "/" + propertiesRc + (error == null ? "" : " error=" + error);
        }
    }
}
