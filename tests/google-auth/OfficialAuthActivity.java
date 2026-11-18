package org.westlake.googlebootstrap.probe;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import com.google.android.gms.auth.GoogleAuthUtil;
import com.google.android.gms.auth.UserRecoverableAuthException;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONObject;

/** Exercises the official SDK/host, not our direct AccountManager token route. */
public final class OfficialAuthActivity extends Activity {
    private final AtomicBoolean running = new AtomicBoolean();
    private final ExecutorService requests = Executors.newCachedThreadPool();
    private TextView status;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        LinearLayout layout = new LinearLayout(this); layout.setOrientation(LinearLayout.VERTICAL);
        for (String mode : new String[] {"sdk", "binder", "control", "spoof-package", "delegation-ui", "delegation-provider", "silent-ungranted", "silent-spoof", "delegation-native-provider"}) {
            Button button = new Button(this); button.setText("Official GMS token: " + mode);
            button.setOnClickListener(v -> run(mode)); layout.addView(button);
        }
        status = new TextView(this);
        status.setText("ac2dm token transport test. No full OAuth sign-in claim. Account and token values are never recorded.");
        layout.addView(status); setContentView(layout);
    }

    private void run(String mode) {
        if (!running.compareAndSet(false, true)) return;
        new Thread(() -> {
            JSONObject result = new JSONObject();
            try {
                result.put("schema", "westlake-official-auth-v1").put("mode", mode)
                        .put("scope", mode.startsWith("silent-") ? "audience_id_token" : "ac2dm").put("startTime", System.currentTimeMillis());
                result.put("gmsVersion", getPackageManager().getPackageInfo("com.google.android.gms", 0).getLongVersionCode());
                Account[] accounts = AccountManager.get(this).getAccountsByType("com.google");
                result.put("accountCount", accounts.length);
                if (accounts.length != 1) result.put("outcome", "account_unavailable");
                else {
                    Future<JSONObject> request = requests.submit(() -> "sdk".equals(mode)
                            ? sdk(accounts[0]) : "control".equals(mode) ? control(accounts[0])
                            : mode.startsWith("delegation-") ? delegationDenied(accounts[0], mode)
                            : binder(accounts[0], mode));
                    try { result.put("operation", request.get(100, TimeUnit.SECONDS)); }
                    catch (Exception error) { request.cancel(true); result.put("operation", failure(error)); }
                }
            } catch (Exception error) {
                try { result.put("operation", failure(error)); } catch (Exception ignored) {}
            }
            try {
                result.put("endTime", System.currentTimeMillis());
                File file = new File(getFilesDir(), "official-auth-" + mode + "-" + System.currentTimeMillis() + ".json");
                try (FileOutputStream out = new FileOutputStream(file)) {
                    out.write(result.toString(2).getBytes(StandardCharsets.UTF_8));
                }
                runOnUiThread(() -> status.setText(result.toString()));
            } catch (Exception error) { runOnUiThread(() -> status.setText("Report error: " + error.getClass().getSimpleName())); }
            finally { running.set(false); }
        }, "official-auth-probe").start();
    }

    private JSONObject sdk(Account account) throws Exception {
        try {
            String token = GoogleAuthUtil.getToken(this, account, "ac2dm", new Bundle());
            return new JSONObject().put("entry", "GoogleAuthUtil.getToken")
                    .put("tokenPresent", token != null && !token.isEmpty())
                    .put("outcome", token != null && !token.isEmpty() ? "token_received" : "empty_token");
        } catch (Exception error) { return failure(error).put("entry", "GoogleAuthUtil.getToken"); }
    }

    /** Adversarial wire requests keep the probe's real, independently verified APK identity. */
    private JSONObject delegationDenied(Account account, String mode) throws Exception {
        boolean nativeProvider = mode.equals("delegation-native-provider");
        boolean provider = mode.equals("delegation-provider") || nativeProvider;
        Class<?> channel = Class.forName("adapter.activity.PlayServicesChannel");
        java.lang.reflect.Field field = channel.getDeclaredField(provider ? "AUTH" : "SERVICES");
        field.setAccessible(true);
        java.lang.reflect.Method getRoot = channel.getDeclaredMethod("root", field.getType());
        getRoot.setAccessible(true);
        IBinder root = (IBinder) getRoot.invoke(null, field.get(null));
        JSONObject result = new JSONObject().put("entry", "PlayServicesChannel." + mode)
                .put("actualIdentity", getPackageName());
        if (root == null) return result.put("outcome", "channel_unavailable");
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            if (!provider) {
                data.writeString("peekAuthToken"); data.writeInt(1); account.writeToParcel(data, 0);
                data.writeString("com.spotify.music:d6a6dced4a85f24204bf9505ccc1fce114cadb32:oauth2:email");
            }
            // The foreign broker must be refused before any delegated payload can be read.
            boolean handled = root.transact(nativeProvider ? 28 : provider ? 23 : 24, data, reply, 0);
            if (!handled) return result.put("outcome", "transaction_unsupported");
            reply.readException();
            return result.put("outcome", "unexpected_acceptance");
        } catch (SecurityException expected) {
            return result.put("outcome", "security_exception").put("denied", true);
        } finally { data.recycle(); reply.recycle(); }
    }

    private JSONObject control(Account account) throws Exception {
        // Positive control only: this bypasses the official GMS token implementation.
        AccountManager manager = AccountManager.get(this);
        Bundle options = new Bundle(); options.putBoolean("westlake.forceRefresh", true);
        Bundle reply = manager.getAuthToken(account, "ac2dm", options, false, null, null)
                .getResult(90, TimeUnit.SECONDS);
        String token = reply.getString(AccountManager.KEY_AUTHTOKEN);
        return new JSONObject().put("entry", "AccountManager.getAuthToken")
                .put("route", "direct_microg_positive_control")
                .put("tokenPresent", token != null && !token.isEmpty())
                .put("requestId", reply.getLong("westlake.requestId", -1))
                .put("forceRefresh", reply.getBoolean("westlake.forceRefresh"))
                .put("outcome", token != null && !token.isEmpty() ? "token_received" : "empty_token");
    }

    private JSONObject binder(Account account, String mode) throws Exception {
        boolean silent = mode.startsWith("silent-");
        boolean spoofPackage = "spoof-package".equals(mode) || "silent-spoof".equals(mode);
        String scope = "ac2dm", claimed = "com.spotify.music";
        if (silent) {
            // Test-only operator input: an existing app's public client ID, never a credential.
            java.util.Properties test = new java.util.Properties();
            try (java.io.InputStream in = new java.io.FileInputStream(new File(getFilesDir(), "silent-auth-test.properties"))) {
                test.load(in);
            }
            String client = test.getProperty("client_id"); claimed = test.getProperty("claimed_package");
            if (client == null || !client.matches("[A-Za-z0-9_-]+\\.apps\\.googleusercontent\\.com")
                    || claimed == null || !claimed.matches("[A-Za-z0-9_]+(?:\\.[A-Za-z0-9_]+)+"))
                throw new IllegalArgumentException("invalid negative-test configuration");
            scope = "audience:server:client_id:" + client;
        }
        JSONObject result = new JSONObject().put("entry", "IAuthManagerService.getTokenWithAccount");
        result.put("spoofPackageTest", spoofPackage).put("silentNegativeTest", silent);
        CountDownLatch connected = new CountDownLatch(1); IBinder[] service = new IBinder[1];
        ServiceConnection connection = new ServiceConnection() {
            @Override public void onServiceConnected(ComponentName name, IBinder value) { service[0] = value; connected.countDown(); }
            @Override public void onServiceDisconnected(ComponentName name) { connected.countDown(); }
            @Override public void onNullBinding(ComponentName name) { connected.countDown(); }
        };
        boolean bound = false;
        try {
            bound = bindService(new Intent().setComponent(new ComponentName("com.google.android.gms",
                    "com.google.android.gms.auth.GetToken")), connection, Context.BIND_AUTO_CREATE);
            if (!bound || !connected.await(30, TimeUnit.SECONDS) || service[0] == null) {
                return result.put("outcome", bound ? "bind_timeout_or_null" : "bind_refused");
            }
            String descriptor = service[0].getInterfaceDescriptor();
            boolean expected = "com.google.android.auth.IAuthManagerService".equals(descriptor);
            result.put("expectedDescriptor", expected);
            if (!expected) return result.put("outcome", "unexpected_descriptor");
            // Negative identity test only. The probe's signed APK and channel identity stay unchanged.
            String claimedPackage = spoofPackage ? claimed : getPackageName();
            Bundle extras = new Bundle(); extras.putString("androidPackageName", claimedPackage);
            extras.putString("clientPackageName", claimedPackage);
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            try {
                data.writeInterfaceToken(descriptor); data.writeInt(1); account.writeToParcel(data, 0);
                data.writeString(scope); data.writeInt(1); extras.writeToParcel(data, 0);
                if (!service[0].transact(5, data, reply, 0)) return result.put("outcome", "transaction_unsupported");
                reply.readException();
                Bundle response = reply.readInt() == 0 ? null : Bundle.CREATOR.createFromParcel(reply);
                if (response == null) return result.put("outcome", "null_response");
                response.setClassLoader(getClassLoader());
                String token = response.getString("authtoken");
                Bundle details = response.getBundle("tokenDetails");
                if (details != null) details.setClassLoader(getClassLoader());
                boolean present = (token != null && !token.isEmpty())
                        || (details != null && details.getParcelable("TokenData") != null);
                return result.put("outcome", present ? "token_received" : "token_not_returned")
                        .put("tokenPresent", present).put("errorCode", safeError(response.getString("Error")))
                        .put("recoveryIntentKeyPresent", response.containsKey("userRecoveryIntent"))
                        .put("recoveryIntentPresent", response.getParcelable("userRecoveryIntent") instanceof Intent);
            } finally { data.recycle(); reply.recycle(); }
        } catch (Exception error) {
            return failure(error).put("entry", "IAuthManagerService.getTokenWithAccount")
                    .put("spoofPackageTest", spoofPackage);
        }
        finally { if (bound) unbindService(connection); }
    }

    private static JSONObject failure(Exception error) throws Exception {
        JSONObject result = new JSONObject().put("outcome", "failed")
                .put("errorType", error.getClass().getSimpleName()).put("errorCode", safeError(error.getMessage()));
        if (error instanceof UserRecoverableAuthException) {
            result.put("recoveryIntentPresent", ((UserRecoverableAuthException) error).getIntent() != null);
        }
        return result;
    }

    private static String safeError(String value) {
        if (value == null) return "none";
        // Never log arbitrary exception messages, Bundle contents or token/account values.
        for (String known : new String[] {"OK", "BadAuthentication", "NeedsBrowser", "NeedPermission", "NetworkError",
                "ServiceUnavailable", "ServiceDisabled", "InvalidScope", "BadRequest", "AccountDeleted", "AccountDisabled",
                "UNREGISTERED_ON_API_CONSOLE", "INVALID_AUDIENCE", "DeviceManagementRequired"}) {
            if (known.equals(value)) return known;
        }
        return "unclassified";
    }
}
