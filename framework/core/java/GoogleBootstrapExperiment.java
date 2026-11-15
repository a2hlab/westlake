package adapter.core;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.app.Application;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.Properties;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/** Opt-in board experiment, disabled in normal launches. Run microG and official GMS sequentially. */
public final class GoogleBootstrapExperiment {
    private static final String MODE = System.getenv("WESTLAKE_GOOGLE_EXPERIMENT");
    private static final String RUN = System.getenv("WESTLAKE_GOOGLE_EXPERIMENT_RUN");
    private static final AtomicBoolean STARTED = new AtomicBoolean();
    static final String MICROG_AUTH = "org.microg.gms.auth.loginservice.GoogleLoginService";
    private static final String OFFICIAL_AUTH =
            "com.google.android.gms.auth.account.authenticator.GoogleAccountAuthenticatorService";

    private GoogleBootstrapExperiment() {}

    public static boolean enabled() { return MODE != null && !MODE.isEmpty(); }
    static boolean microg() { return "microg-export".equals(MODE); }

    public static void event(String outcome) {
        if (enabled()) System.err.println("[WL-GOOGLE-EXPERIMENT] run=" + RUN + " mode=" + MODE
                + " time=" + System.currentTimeMillis() + " " + outcome);
    }

    static String authenticator() { return microg() ? MICROG_AUTH : OFFICIAL_AUTH; }

    private static void validateHost(Context app) throws Exception {
        if (!enabled()) return;
        if (!(microg() || "official-baseline".equals(MODE) || "official-import".equals(MODE)
                || "official-recheck".equals(MODE)) || RUN == null || !RUN.matches("[A-Za-z0-9_-]{1,64}")) {
            throw new IllegalStateException("invalid Google experiment configuration");
        }
        if (!"com.google.android.gms".equals(app.getPackageName())
                || System.getenv("WESTLAKE_PLAY_SERVICES_SERVE") == null) {
            throw new IllegalStateException("experiment requires a Play services host");
        }
        app.getPackageManager().getServiceInfo(new ComponentName(app.getPackageName(), authenticator()), 0);
    }

    private static File directory(Context app) { return new File(app.getDataDir(), "westlake-bootstrap"); }

    private static String digest(byte[] bytes) throws Exception {
        StringBuilder out = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(bytes)) out.append(String.format("%02x", b & 255));
        return out.toString();
    }

    private static String profile() throws Exception {
        return digest((Build.MANUFACTURER + "\n" + Build.BRAND + "\n" + Build.MODEL + "\n"
                + Build.DEVICE + "\n" + Build.PRODUCT + "\n" + Build.FINGERPRINT + "\n"
                + Build.VERSION.RELEASE + "\n" + Build.VERSION.SDK_INT).getBytes(StandardCharsets.UTF_8));
    }

    private static String apkDigest(Context app) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (FileInputStream in = new FileInputStream(app.getApplicationInfo().sourceDir)) {
            byte[] bytes = new byte[65536];
            int count;
            while ((count = in.read(bytes)) != -1) digest.update(bytes, 0, count);
        }
        StringBuilder out = new StringBuilder();
        for (byte b : digest.digest()) out.append(String.format("%02x", b & 255));
        return out.toString();
    }

    /** Called before the account service exposes its store. Import never overwrites an existing account. */
    static Properties importIfRequested(Context app, File accountFile, Properties existing) {
        if (!"official-import".equals(MODE)) return existing;
        try {
            validateHost(app);
            if (RUN.equals(existing.getProperty("bootstrap/consumed"))) {
                event("credential_import=already_consumed");
                return existing;
            }
            File incoming = new File(directory(app), "incoming.properties");
            Properties transfer = BootstrapTransfer.read(incoming.toPath(), RUN, profile(), System.currentTimeMillis());
            verifyStagedDevice(app, transfer);
            Properties imported = BootstrapTransfer.accountStore(transfer, existing);
            BootstrapTransfer.writePrivate(accountFile.toPath(), imported);
            Files.deleteIfExists(incoming.toPath());
            event("credential_import=complete device_identity_staged=" + !"0".equals(transfer.getProperty("androidId"))
                    + " source_apk=" + transfer.getProperty("sourceApk"));
            return imported;
        } catch (Exception e) {
            event("credential_import=failed error=" + e.getClass().getSimpleName());
            throw new IllegalStateException("Google bootstrap import failed", e);
        }
    }

    /** Invoked after binding the primary application; account requests must run off the main thread. */
    public static void start(Application app) {
        if (!enabled() || !STARTED.compareAndSet(false, true)) return;
        new Thread(() -> {
            try {
                validateHost(app);
                event("start manufacturer=" + Build.MANUFACTURER + " model=" + Build.MODEL
                        + " android=" + Build.VERSION.RELEASE + " sdk=" + Build.VERSION.SDK_INT
                        + " profile=" + profile() + " apk=" + apkDigest(app));
                if (microg()) verifyMicrogProfile(app);
                AccountManager manager = AccountManager.get(app);
                Account[] accounts = manager.getAccountsByType("com.google");
                if ("official-baseline".equals(MODE)) {
                    event("baseline accounts=" + accounts.length + " account_exchange=not_requested");
                    requestCheckin(app);
                    return;
                }
                // Login occurs in microG's own UI. Never accept a password/token over a diagnostic intent.
                for (int attempt = 0; microg() && accounts.length == 0 && attempt < 120; attempt++) {
                    if (attempt == 0) event("login=required waiting_seconds=600");
                    Thread.sleep(5000);
                    accounts = manager.getAccountsByType("com.google");
                }
                if (accounts.length != 1) throw new IllegalStateException("experiment requires exactly one Google account");
                Account account = accounts[0];
                if (!microg()) verifyOfficialDeviceRead(app);
                long[] device = microg() ? microgCheckin(app) : null;
                boolean fresh = tokenRoundTrip(manager, account);
                if (microg() && fresh) {
                    String credential = manager.getPassword(account);
                    if (credential == null) throw new IOException("no reusable credential");
                    Properties transfer = BootstrapTransfer.envelope(RUN, account.name, credential,
                            profile(), apkDigest(app), System.currentTimeMillis());
                    transfer.setProperty("androidId", Long.toString(device[0]));
                    transfer.setProperty("securityToken", Long.toString(device[1]));
                    BootstrapTransfer.writePrivate(new File(directory(app), "outgoing.properties").toPath(), transfer);
                    event("credential_export=complete checkin_identity_exported=true policy_exported=false");
                }
                if (!microg()) requestCheckin(app);
            } catch (Exception | LinkageError e) {
                // No exception message, Bundle, account name, URL or token in diagnostic output.
                event("experiment=failed error=" + e.getClass().getSimpleName());
            }
        }, "google-bootstrap-experiment").start();
    }

    private static boolean tokenRoundTrip(AccountManager manager, Account account) throws Exception {
        Bundle options = new Bundle();
        options.putString(AccountManager.KEY_ANDROID_PACKAGE_NAME, "com.google.android.gms");
        // This is a Google legacy scoped-token exchange, not a Play Store login or an OAuth ID token.
        Bundle first = manager.getAuthToken(account, "ac2dm", options, false, null, null)
                .getResult(90, TimeUnit.SECONDS);
        String token = first.getString(AccountManager.KEY_AUTHTOKEN);
        if (token == null) {
            event("token_initial=missing consent=" + first.containsKey(AccountManager.KEY_INTENT));
            return false;
        }
        event("token_initial=present");
        manager.invalidateAuthToken("com.google", token);
        Bundle second = manager.getAuthToken(account, "ac2dm", options, false, null, null)
                .getResult(90, TimeUnit.SECONDS);
        boolean present = second.getString(AccountManager.KEY_AUTHTOKEN) != null;
        event("token_after_invalidation=" + (present ? "present" : "missing")
                + " consent=" + second.containsKey(AccountManager.KEY_INTENT));
        return present;
    }

    private static void verifyMicrogProfile(Context app) throws Exception {
        ClassLoader loader = app.getClassLoader();
        Class<?> manager = loader.loadClass("org.microg.gms.profile.ProfileManager");
        Object instance = manager.getField("INSTANCE").get(null);
        Object data = manager.getMethod("getActiveProfileData", Context.class).invoke(instance, app);
        java.util.Map<?, ?> values = (java.util.Map<?, ?>) data;
        if (!Build.MANUFACTURER.equals(values.get("Build.MANUFACTURER"))
                || !Build.BRAND.equals(values.get("Build.BRAND"))
                || !Build.MODEL.equals(values.get("Build.MODEL"))
                || !Build.DEVICE.equals(values.get("Build.DEVICE"))
                || !Build.PRODUCT.equals(values.get("Build.PRODUCT"))
                || !Build.FINGERPRINT.equals(values.get("Build.FINGERPRINT"))
                || !Build.VERSION.RELEASE.equals(values.get("Build.VERSION.RELEASE"))
                || !Integer.toString(Build.VERSION.SDK_INT).equals(values.get("Build.VERSION.SDK_INT"))) {
            throw new IllegalStateException("microG profile differs from the host");
        }
        event("microg_profile=matches_host serial_identity=not_transferred");
    }

    private static long[] microgCheckin(Context app) throws Exception {
        Class<?> manager = app.getClassLoader().loadClass("org.microg.gms.checkin.CheckinManager");
        Object result = manager.getMethod("checkin", Context.class, boolean.class).invoke(null, app, true);
        if (result == null) throw new IOException("microG check-in disabled or no result");
        long id = (Long) result.getClass().getMethod("getAndroidId").invoke(result);
        long secret = (Long) result.getClass().getMethod("getSecurityToken").invoke(result);
        if (id == 0 || secret == 0) throw new IOException("microG check-in has no device credentials");
        event("microg_checkin=complete device_id_hash=" + digest(Long.toString(id).getBytes(StandardCharsets.UTF_8)));
        return new long[] {id, secret};
    }

    private static void verifyStagedDevice(Context app, Properties transfer) throws Exception {
        if ("0".equals(transfer.getProperty("androidId"))) return;
        if (!"1159bb46cad6f4afd7f4a570b1a5674f1285a12e573dbe6501ed4b059fd902d1".equals(apkDigest(app)))
            throw new IOException("unverified official check-in format");
        File identity = new File(app.getFilesDir(), "checkin_id_token");
        if (!identity.isFile() || identity.length() > 128) throw new IOException("device identity was not staged before launch");
        String expected = transfer.getProperty("androidId") + ":" + transfer.getProperty("securityToken");
        String actual = new String(Files.readAllBytes(identity.toPath()), StandardCharsets.UTF_8);
        if (!expected.equals(actual)) throw new IOException("staged device identity mismatch");
        event("device_identity_file=matches_microg device_id_hash="
                + digest(transfer.getProperty("androidId").getBytes(StandardCharsets.UTF_8))
                + " official_consumption=unverified");
    }

    private static void verifyOfficialDeviceRead(Context app) throws Exception {
        File staged = new File(directory(app), "staging.properties");
        if (!staged.isFile()) return;
        Properties receipt = new Properties();
        try (FileInputStream in = new FileInputStream(staged)) { receipt.load(in); }
        if (!"account-and-device".equals(receipt.getProperty("mode"))) return;
        if (!"1159bb46cad6f4afd7f4a570b1a5674f1285a12e573dbe6501ed4b059fd902d1".equals(apkDigest(app)))
            throw new IOException("official identity reader not verified for this APK");
        File identity = new File(app.getFilesDir(), "checkin_id_token");
        if (!identity.isFile() || identity.length() > 128) throw new IOException("missing official identity");
        String[] pair = new String(Files.readAllBytes(identity.toPath()), StandardCharsets.UTF_8).split(":");
        if (pair.length != 2) throw new IOException("invalid official identity");
        // Version-pinned diagnostics only, not a production dependency on obfuscated Google classes.
        ClassLoader loader = app.getClassLoader();
        long id = (Long) loader.loadClass("aizt").getMethod("a", Context.class).invoke(null, app);
        long secret = (Long) loader.loadClass("aizc").getMethod("b", Context.class).invoke(null, app);
        if (id != Long.parseLong(pair[0]) || secret != Long.parseLong(pair[1]))
            throw new IOException("official check-in readers chose a different identity");
        event("official_identity_read=matches_staged device_id_hash=" + deviceIdHash(Long.toString(id)));
    }

    public static String deviceIdHash(String id) {
        try { return digest(id.getBytes(StandardCharsets.UTF_8)); }
        catch (Exception e) { return "unavailable"; }
    }

    private static void requestCheckin(Context app) {
        app.sendBroadcast(new Intent("android.server.checkin.CHECKIN").setPackage("com.google.android.gms"));
        event("official_checkin=requested completion=unverified");
    }
}
