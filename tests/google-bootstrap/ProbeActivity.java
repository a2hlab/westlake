package org.westlake.googlebootstrap.probe;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageInfo;
import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.util.Log;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONArray;
import org.json.JSONObject;

/** External client: no account credentials and no Google library dependency. */
public final class ProbeActivity extends Activity {
    private static final String GMS = "com.google.android.gms";
    private static final String CALLBACK = "com.google.android.gms.common.internal.IGmsCallbacks";
    private static final int[] IDS = {51, 8, 93, 300};
    private static final String[] ACTIONS = {
        "com.google.android.gms.phenotype.service.START", "com.google.android.gms.ads.service.START",
        "com.google.android.gms.measurement.START", "com.google.android.gms.appset.service.START"};
    private TextView text;
    private EditText runId;
    private final AtomicBoolean running = new AtomicBoolean();
    private final ExecutorService requests = Executors.newCachedThreadPool();

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        runId = new EditText(this);
        runId.setSingleLine(true);
        runId.setText("bootstrap01");
        runId.setHint("Match the host experiment run ID");
        layout.addView(runId);
        for (String phase : new String[] {"baseline", "after-handoff", "after-restart"}) {
            Button button = new Button(this);
            button.setText("Probe: " + phase);
            button.setOnClickListener(v -> run(phase));
            layout.addView(button);
        }
        text = new TextView(this);
        text.setText("Connect this app to the official GMS host. Each run checks four service brokers and executes App Set ID. Results stay in app-private files.");
        ScrollView scroll = new ScrollView(this);
        scroll.addView(text);
        layout.addView(scroll);
        setContentView(layout);
    }

    private void run(String phase) {
        final String run = runId.getText().toString();
        if (!run.matches("[A-Za-z0-9_-]{1,64}")) { text.setText("Enter a valid experiment run ID"); return; }
        if (!running.compareAndSet(false, true)) return;
        new Thread(() -> {
            JSONObject result = new JSONObject();
            try {
                result.put("schema", "westlake-google-probe-v1");
                result.put("run", run);
                result.put("phase", phase);
                result.put("time", System.currentTimeMillis());
                result.put("manufacturer", Build.MANUFACTURER);
                result.put("model", Build.MODEL);
                result.put("fingerprint", Build.FINGERPRINT);
                result.put("sdk", Build.VERSION.SDK_INT);
                PackageInfo gms = getPackageManager().getPackageInfo(GMS, 0);
                result.put("gmsVersion", gms.getLongVersionCode());
                JSONArray probes = new JSONArray();
                for (int i = 0; i < IDS.length; i++) {
                    final int index = i;
                    runOnUiThread(() -> text.setText("Probing service " + IDS[index]));
                    Future<JSONObject> request = requests.submit(() -> probe(IDS[index], ACTIONS[index]));
                    try { probes.put(request.get(100, TimeUnit.SECONDS)); }
                    catch (TimeoutException e) {
                        request.cancel(true);
                        probes.put(new JSONObject().put("serviceId", IDS[i]).put("outcome", "request_timeout"));
                    }
                }
                result.put("services", probes);
                result.put("policyFreshness", "must_correlate_with_host_checkin_evidence");
                result.put("mapsLocationLoginTested", false);
                File file = new File(getFilesDir(), phase + "-" + System.currentTimeMillis() + ".json");
                try (FileOutputStream out = new FileOutputStream(file)) {
                    out.write(result.toString(2).getBytes(StandardCharsets.UTF_8));
                }
                Log.i("WL-GOOGLE-PROBE", result.toString());
                runOnUiThread(() -> text.setText(result.toString()));
            } catch (Exception e) {
                runOnUiThread(() -> text.setText("Probe failed: " + e.getClass().getSimpleName()));
            } finally { running.set(false); }
        }, "google-api-probe").start();
    }

    @Override public void onDestroy() { requests.shutdownNow(); super.onDestroy(); }

    private JSONObject probe(int id, String action) throws Exception {
        JSONObject result = new JSONObject().put("serviceId", id).put("action", action);
        CountDownLatch connected = new CountDownLatch(1);
        IBinder[] broker = new IBinder[1];
        ServiceConnection connection = new ServiceConnection() {
            @Override public void onServiceConnected(ComponentName name, IBinder binder) {
                broker[0] = binder; connected.countDown();
            }
            @Override public void onServiceDisconnected(ComponentName name) { connected.countDown(); }
            @Override public void onNullBinding(ComponentName name) { connected.countDown(); }
        };
        boolean bound = false;
        try {
            bound = bindService(new Intent(action).setPackage(GMS), connection, Context.BIND_AUTO_CREATE);
            if (!bound || !connected.await(35, TimeUnit.SECONDS) || broker[0] == null) {
                return result.put("outcome", bound ? "bind_timeout_or_null" : "bind_refused");
            }
            CountDownLatch completed = new CountDownLatch(1);
            int[] status = {Integer.MIN_VALUE};
            IBinder[] service = new IBinder[1];
            Binder callback = new Binder() {
                @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                        throws RemoteException {
                    if (code == INTERFACE_TRANSACTION) { if (reply != null) reply.writeString(CALLBACK); return true; }
                    if (code != 1 && code != 3) return super.onTransact(code, data, reply, flags);
                    data.enforceInterface(CALLBACK);
                    status[0] = data.readInt();
                    service[0] = data.readStrongBinder();
                    // Bundle (1) or ConnectionInfo (3) follows; it is not needed for a gate probe.
                    if (reply != null) reply.writeNoException();
                    completed.countDown();
                    return true;
                }
            };
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            try {
                data.writeInterfaceToken("com.google.android.gms.common.internal.IGmsServiceBroker");
                data.writeStrongBinder(callback);
                data.writeInt(1);
                int object = begin(data, 20293);
                integer(data, 1, 6);
                integer(data, 2, id);
                integer(data, 3, 244034000);
                string(data, 4, getPackageName());
                int extras = begin(data, 7); data.writeBundle(new Bundle()); end(data, extras);
                integer(data, 12, 1);
                end(data, object);
                if (!broker[0].transact(46, data, reply, 0)) return result.put("outcome", "broker_transaction_unsupported");
                reply.readException();
            } finally { data.recycle(); reply.recycle(); }
            if (!completed.await(35, TimeUnit.SECONDS)) return result.put("outcome", "broker_callback_timeout");
            result.put("brokerStatus", status[0]).put("binderPresent", service[0] != null);
            if (status[0] != 0 || service[0] == null) return result.put("outcome", "broker_rejected");
            result.put("outcome", "broker_accepted");
            if (id == 300) result.put("appSet", appSet(service[0]));
            return result;
        } catch (Exception e) {
            return result.put("outcome", "exception").put("errorType", e.getClass().getSimpleName());
        } finally { if (bound) unbindService(connection); }
    }

    private JSONObject appSet(IBinder service) throws Exception {
        final String descriptor = "com.google.android.gms.appset.internal.IAppSetIdCallback";
        CountDownLatch completed = new CountDownLatch(1);
        int[] code = {Integer.MIN_VALUE}, scope = {0};
        boolean[] idPresent = {false};
        Binder callback = new Binder() {
            @Override protected boolean onTransact(int transaction, Parcel data, Parcel reply, int flags)
                    throws RemoteException {
                if (transaction == INTERFACE_TRANSACTION) { if (reply != null) reply.writeString(descriptor); return true; }
                if (transaction != 1) return super.onTransact(transaction, data, reply, flags);
                data.enforceInterface(descriptor);
                try {
                    if (data.readInt() != 0) fields(data, (p, field) -> { if (field == 1) code[0] = p.readInt(); });
                    if (data.readInt() != 0) fields(data, (p, field) -> {
                        if (field == 1) { String value = p.readString(); idPresent[0] = value != null && !value.isEmpty(); }
                        if (field == 2) scope[0] = p.readInt();
                    });
                    if (reply != null) reply.writeNoException();
                } finally { completed.countDown(); }
                return true;
            }
        };
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken("com.google.android.gms.appset.internal.IAppSetService");
            data.writeInt(1);
            int object = begin(data, 20293);
            string(data, 1, "16.1.0");
            string(data, 2, getPackageName());
            end(data, object);
            data.writeStrongBinder(callback);
            if (!service.transact(1, data, reply, 0)) return new JSONObject().put("outcome", "transaction_unsupported");
            reply.readException();
        } finally { data.recycle(); reply.recycle(); }
        if (!completed.await(35, TimeUnit.SECONDS)) return new JSONObject().put("outcome", "callback_timeout");
        return new JSONObject().put("status", code[0]).put("idPresent", idPresent[0]).put("scope", scope[0])
                .put("outcome", code[0] == 0 && idPresent[0] && (scope[0] == 1 || scope[0] == 2) ? "api_success" : "api_failed");
    }

    private static int begin(Parcel p, int field) {
        p.writeInt(0xffff0000 | field); p.writeInt(0); return p.dataPosition();
    }
    private static void end(Parcel p, int start) {
        int end = p.dataPosition(); p.setDataPosition(start - 4); p.writeInt(end - start); p.setDataPosition(end);
    }
    private static void integer(Parcel p, int field, int value) { p.writeInt(0x40000 | field); p.writeInt(value); }
    private static void string(Parcel p, int field, String value) { int start = begin(p, field); p.writeString(value); end(p, start); }
    private interface FieldReader { void read(Parcel p, int field); }
    private static int size(Parcel p, int header) { return (header >>> 16) == 65535 ? p.readInt() : header >>> 16; }
    private static void fields(Parcel p, FieldReader reader) {
        int header = p.readInt(), length = size(p, header), limit = p.dataPosition() + length;
        if ((header & 65535) != 20293 || length < 0 || limit > p.dataSize() || limit < p.dataPosition())
            throw new IllegalArgumentException("invalid parcel object");
        while (p.dataPosition() < limit) {
            int field = p.readInt(), bytes = size(p, field), next = p.dataPosition() + bytes;
            if (bytes < 0 || next > limit || next < p.dataPosition()) throw new IllegalArgumentException("invalid parcel field");
            reader.read(p, field & 65535);
            if (p.dataPosition() > next) throw new IllegalArgumentException("parcel field overrun");
            p.setDataPosition(next);
        }
    }
}
