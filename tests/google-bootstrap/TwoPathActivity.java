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
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONObject;

/** Same app, two independently configured paths. Never writes account names, tokens or ad IDs. */
public final class TwoPathActivity extends Activity {
    private final AtomicBoolean running = new AtomicBoolean();
    private TextView status;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        LinearLayout layout = new LinearLayout(this); layout.setOrientation(LinearLayout.VERTICAL);
        for (String phase : new String[] {"live", "auth-restarted", "official-restarted", "auth-offline", "official-offline"}) {
            Button button = new Button(this); button.setText("Two paths: " + phase);
            button.setOnClickListener(v -> run(phase)); layout.addView(button);
        }
        status = new TextView(this); layout.addView(status); setContentView(layout);
        status.setText("Ready: authentication via microG; advertising ID via official GMS.");
    }

    private void run(String phase) {
        if (!running.compareAndSet(false, true)) return;
        new Thread(() -> {
            JSONObject result = new JSONObject();
            try {
                result.put("schema", "westlake-google-two-path-v1").put("phase", phase)
                        .put("startTime", System.currentTimeMillis());
                result.put("auth", authentication());
                result.put("officialApi", advertisingId());
                result.put("endTime", System.currentTimeMillis());
                File file = new File(getFilesDir(), "two-path-" + phase + "-" + System.currentTimeMillis() + ".json");
                try (FileOutputStream out = new FileOutputStream(file)) {
                    out.write(result.toString(2).getBytes(StandardCharsets.UTF_8));
                }
                runOnUiThread(() -> status.setText(result.toString()));
            } catch (Exception e) {
                runOnUiThread(() -> status.setText("Probe error: " + e.getClass().getSimpleName()));
            } finally { running.set(false); }
        }, "two-path-probe").start();
    }

    private JSONObject authentication() throws Exception {
        JSONObject result = new JSONObject();
        try {
            AccountManager manager = AccountManager.get(this);
            Account[] accounts = manager.getAccountsByType("com.google");
            result.put("accountCount", accounts.length);
            if (accounts.length != 1) return result.put("outcome", "account_unavailable");
            Account account = accounts[0];
            result.put("reusableCredentialWithheld", manager.getPassword(account) == null);
            Bundle options = new Bundle(); options.putBoolean("westlake.forceRefresh", true);
            Bundle first = manager.getAuthToken(account, "ac2dm", options, false, null, null).getResult(90, TimeUnit.SECONDS);
            String token = first.getString(AccountManager.KEY_AUTHTOKEN);
            boolean present = token != null && !token.isEmpty();
            result.put("tokenPresent", present).put("requestId", first.getLong("westlake.requestId", -1))
                    .put("forceRefresh", first.getBoolean("westlake.forceRefresh"))
                    .put("cachePresentBefore", first.getBoolean("westlake.cachePresentBefore"));
            if (!present) return result.put("outcome", "token_unavailable");
            manager.invalidateAuthToken("com.google", token);
            Bundle second = manager.getAuthToken(account, "ac2dm", options, false, null, null).getResult(90, TimeUnit.SECONDS);
            String next = second.getString(AccountManager.KEY_AUTHTOKEN);
            result.put("tokenAfterInvalidation", next != null && !next.isEmpty())
                    .put("secondRequestId", second.getLong("westlake.requestId", -1))
                    .put("cachePresentAfterInvalidation", second.getBoolean("westlake.cachePresentBefore"));
            try {
                manager.getAuthToken(account, "oauth2:westlake-ungranted", new Bundle(), false, null, null)
                        .getResult(30, TimeUnit.SECONDS);
                result.put("ungrantedScopeDenied", false);
            } catch (Exception denied) {
                result.put("ungrantedScopeDenied", authorizationFailure(denied))
                        .put("ungrantedScopeErrorType", denied.getClass().getSimpleName());
            }
            // microG's package override is not a supported delegation contract in this proxy.
            Bundle spoof = new Bundle(); spoof.putString("overridePackage", "com.google.android.gms");
            try {
                manager.getAuthToken(account, "ac2dm", spoof, false, null, null).getResult(30, TimeUnit.SECONDS);
                result.put("untrustedDelegationOptionDenied", false);
            } catch (Exception denied) {
                result.put("untrustedDelegationOptionDenied", authorizationFailure(denied))
                        .put("untrustedDelegationErrorType", denied.getClass().getSimpleName());
            }
            try {
                manager.getAuthToken(new Account("westlake.invalid.account", "com.google"), "ac2dm", options,
                        false, null, null).getResult(30, TimeUnit.SECONDS);
                result.put("wrongAccountDenied", false);
            } catch (Exception denied) {
                result.put("wrongAccountDenied", authorizationFailure(denied))
                        .put("wrongAccountErrorType", denied.getClass().getSimpleName());
            }
            return result.put("outcome", "token_operations_completed");
        } catch (Exception e) { return result.put("outcome", "failed").put("errorType", e.getClass().getSimpleName()); }
    }

    private static boolean authorizationFailure(Exception error) {
        // A timeout, cancellation or I/O failure does not establish an authorization denial.
        return error instanceof SecurityException || error instanceof android.accounts.AuthenticatorException;
    }

    private JSONObject advertisingId() throws Exception {
        JSONObject result = new JSONObject().put("api", "AdvertisingIdService");
        CountDownLatch connected = new CountDownLatch(1); IBinder[] service = new IBinder[1];
        ServiceConnection connection = new ServiceConnection() {
            @Override public void onServiceConnected(ComponentName name, IBinder binder) { service[0] = binder; connected.countDown(); }
            @Override public void onServiceDisconnected(ComponentName name) { connected.countDown(); }
            @Override public void onNullBinding(ComponentName name) { connected.countDown(); }
        };
        boolean bound = false;
        try {
            result.put("gmsVersion", getPackageManager().getPackageInfo("com.google.android.gms", 0).getLongVersionCode());
            bound = bindService(new Intent("com.google.android.gms.ads.identifier.service.START").setPackage("com.google.android.gms"),
                    connection, Context.BIND_AUTO_CREATE);
            if (!bound || !connected.await(40, TimeUnit.SECONDS) || service[0] == null) {
                return result.put("outcome", bound ? "bind_timeout_or_null" : "bind_refused");
            }
            String descriptor = "com.google.android.gms.ads.identifier.internal.IAdvertisingIdService";
            String id; boolean limited;
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            try {
                data.writeInterfaceToken(descriptor);
                if (!service[0].transact(1, data, reply, 0)) return result.put("outcome", "transaction_unsupported");
                reply.readException(); id = reply.readString();
            } finally { data.recycle(); reply.recycle(); }
            data = Parcel.obtain(); reply = Parcel.obtain();
            try {
                data.writeInterfaceToken(descriptor); data.writeInt(1);
                if (!service[0].transact(2, data, reply, 0)) return result.put("outcome", "transaction_unsupported");
                reply.readException(); limited = reply.readInt() != 0;
            } finally { data.recycle(); reply.recycle(); }
            boolean formatted = id != null && id.matches("[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}");
            return result.put("idHasUuidFormat", formatted).put("zeroId", "00000000-0000-0000-0000-000000000000".equals(id))
                    .put("limitAdTracking", limited).put("outcome", formatted ? "api_completed" : "invalid_result");
        } catch (Exception e) { return result.put("outcome", "failed").put("errorType", e.getClass().getSimpleName()); }
        finally { if (bound) unbindService(connection); }
    }
}
