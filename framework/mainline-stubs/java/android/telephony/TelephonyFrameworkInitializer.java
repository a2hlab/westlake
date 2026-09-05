package android.telephony;

import android.app.SystemServiceRegistry;
import android.content.Context;
import android.telephony.euicc.EuiccCardManager;
import android.telephony.euicc.EuiccManager;
import android.telephony.ims.ImsManager;

/** Registers the framework-side telephony service wrappers on OH. */
public final class TelephonyFrameworkInitializer {
    private static volatile android.os.TelephonyServiceManager sTelephonyServiceManager;

    private TelephonyFrameworkInitializer() {}
    public static void registerServiceWrappers() {
        // Keep AOSP's Context#getSystemService contract even when this device
        // has no Android radio daemon. The manager wrappers are still required
        // for feature detection and return the platform's normal unavailable
        // values when their backing binder services do not exist.
        SystemServiceRegistry.registerContextAwareService(
                Context.TELEPHONY_SERVICE,
                TelephonyManager.class,
                context -> new TelephonyManager(context));
        SystemServiceRegistry.registerContextAwareService(
                Context.TELEPHONY_SUBSCRIPTION_SERVICE,
                SubscriptionManager.class,
                context -> new SubscriptionManager(context));
        SystemServiceRegistry.registerContextAwareService(
                Context.CARRIER_CONFIG_SERVICE,
                CarrierConfigManager.class,
                context -> new CarrierConfigManager(context));
        SystemServiceRegistry.registerContextAwareService(
                Context.EUICC_SERVICE,
                EuiccManager.class,
                context -> new EuiccManager(context));
        SystemServiceRegistry.registerContextAwareService(
                Context.EUICC_CARD_SERVICE,
                EuiccCardManager.class,
                context -> new EuiccCardManager(context));
        SystemServiceRegistry.registerContextAwareService(
                Context.TELEPHONY_IMS_SERVICE,
                ImsManager.class,
                context -> new ImsManager(context));
    }
    public static void setTelephonyServiceManager(android.os.TelephonyServiceManager manager) {
        if (manager == null) {
            throw new NullPointerException("telephonyServiceManager");
        }
        if (sTelephonyServiceManager != null) {
            throw new IllegalStateException("setTelephonyServiceManager called twice!");
        }
        sTelephonyServiceManager = manager;
    }

    public static android.os.TelephonyServiceManager getTelephonyServiceManager() {
        return sTelephonyServiceManager;
    }
}
