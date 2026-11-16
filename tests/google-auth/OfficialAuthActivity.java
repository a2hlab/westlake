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
        for (String mode : new String[] {"sdk", "binder", "control", "spoof-package"}) {
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
                        .put("scope", "ac2dm").put("startTime", System.currentTimeMillis());
                result.put("gmsVersion", getPackageManager().getPackageInfo("com.google.android.gms", 0).getLongVersionCode());
                Account[] accounts = AccountManager.get(this).getAccountsByType("com.google");
                result.put("accountCount", accounts.length);
                if (accounts.length != 1) result.put("outcome", "account_unavailable");
                else {
                    Future<JSONObject> request = requests.submit(() -> "sdk".equals(mode)
                            ? sdk(accounts[0]) : "control".equals(mode) ? control(accounts[0])
                            : binder(accounts[0], "spoof-package".equals(mode)));
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

    private JSONObject binder(Account account, boolean spoofPackage) throws Exception {
        JSONObject result = new JSONObject().put("entry", "IAuthManagerService.getTokenWithAccount");
        result.put("spoofPackageTest", spoofPackage);
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
            String claimedPackage = spoofPackage ? "com.spotify.music" : getPackageName();
            Bundle extras = new Bundle(); extras.putString("androidPackageName", claimedPackage);
            extras.putString("clientPackageName", claimedPackage);
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            try {
                data.writeInterfaceToken(descriptor); data.writeInt(1); account.writeToParcel(data, 0);
                data.writeString("ac2dm"); data.writeInt(1); extras.writeToParcel(data, 0);
                if (!service[0].transact(5, data, reply, 0)) return result.put("outcome", "transaction_unsupported");
                reply.readException();
                Bundle response = reply.readInt() == 0 ? null : Bundle.CREATOR.createFromParcel(reply);
                if (response == null) return result.put("outcome", "null_response");
                response.setClassLoader(getClassLoader());
                String token = response.getString("authtoken");
                boolean present = token != null && !token.isEmpty();
                return result.put("outcome", present ? "token_received" : "token_not_returned")
                        .put("tokenPresent", present).put("errorCode", safeError(response.getString("Error")))
                        .put("recoveryIntentPresent", response.containsKey("userRecoveryIntent"));
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
