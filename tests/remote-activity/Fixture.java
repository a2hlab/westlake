package org.westlake.remoteactivity.fixture;

import android.app.Activity;
import android.app.Application;
import android.content.ComponentName;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.TextView;
import java.io.File;
import java.io.FileWriter;
import org.json.JSONObject;

/** Two throwaway APKs on a dedicated channel, with no real Google code or account. */
public final class Fixture {
    static final String CLIENT = "org.westlake.googlebootstrap.remoteactivity";
    static final String PREFIX = "org.westlake.remoteactivity.fixture.Fixture$";
    static Intent target(String name) {
        return new Intent("westlake.test.REMOTE").setComponent(new ComponentName("com.google.android.gms", PREFIX + name));
    }
    public static class Host extends Activity {}
    public static class ForwardEntry extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            startActivity(target("Echo").putExtras(getIntent()).addFlags(Intent.FLAG_ACTIVITY_FORWARD_RESULT));
            finish();
        }
    }
    public static class Echo extends Activity {
        Intent answer;
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            answer = new Intent();
            answer.putExtra("nested", getIntent().getBundleExtra("nested"));
            answer.putExtra("caller_ok", CLIENT.equals(getCallingPackage())
                    && new ComponentName(CLIENT, PREFIX + "Client").equals(getCallingActivity()));
            answer.putExtra("process_ok", "com.google.android.gms:ui".equals(Application.getProcessName()));
            answer.putExtra("referrer_ok", Uri.parse("android-app://" + CLIENT).equals(getReferrer()));
            bindService(target("Factory"), new android.content.ServiceConnection() {
                @Override public void onServiceConnected(ComponentName name, android.os.IBinder service) {
                    android.os.Parcel data = android.os.Parcel.obtain(), reply = android.os.Parcel.obtain();
                    try {
                        service.transact(1, data, reply, 0); reply.readException();
                        android.app.PendingIntent pending = reply.readTypedObject(android.app.PendingIntent.CREATOR);
                        startIntentSenderForResult(pending.getIntentSender(), 7,
                                new Intent().putExtra("marker", "changed"), 0, 0, 0);
                        boolean used = false;
                        try { startIntentSenderForResult(pending.getIntentSender(), 8, null, 0, 0, 0); }
                        catch (android.content.IntentSender.SendIntentException expected) { used = true; }
                        answer.putExtra("one_shot", used);
                        reply.setDataSize(0); reply.setDataPosition(0); data.setDataPosition(0);
                        service.transact(1, data, reply, 0); reply.readException();
                        answer.putExtra("foreign_capability", reply.readTypedObject(android.app.PendingIntent.CREATOR));
                    } catch (Exception error) {
                        answer.putExtra("pending_error", true); setResult(RESULT_OK, answer); finish();
                    } finally { data.recycle(); reply.recycle(); unbindService(this); }
                }
                @Override public void onServiceDisconnected(ComponentName name) {}
            }, BIND_AUTO_CREATE);
        }
        @Override protected void onActivityResult(int request, int result, Intent data) {
            super.onActivityResult(request, result, data);
            answer.putExtra("pending_result", request == 7 && result == RESULT_OK && data != null
                    && data.getBooleanExtra("immutable", false) && data.getBooleanExtra("nested_caller", false));
            setResult(RESULT_OK, answer); finish();
        }
    }
    public static class Factory extends android.app.Service {
        final java.util.concurrent.CountDownLatch ready = new java.util.concurrent.CountDownLatch(1);
        android.os.IBinder worker;
        final android.content.ServiceConnection connection = new android.content.ServiceConnection() {
            @Override public void onServiceConnected(ComponentName name, android.os.IBinder service) { worker = service; ready.countDown(); }
            @Override public void onServiceDisconnected(ComponentName name) { worker = null; }
        };
        @Override public void onCreate() { super.onCreate(); bindService(target("Worker"), connection, BIND_AUTO_CREATE); }
        @Override public void onDestroy() { unbindService(connection); super.onDestroy(); }
        @Override public android.os.IBinder onBind(Intent intent) {
            return new android.os.Binder() {
                @Override protected boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags)
                        throws android.os.RemoteException {
                    try {
                        if (code != 1 || !ready.await(10, java.util.concurrent.TimeUnit.SECONDS) || worker == null) return false;
                    } catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); return false; }
                    return worker.transact(code, data, reply, flags);
                }
            };
        }
    }
    public static class Worker extends android.app.Service {
        @Override public android.os.IBinder onBind(Intent intent) {
            return new android.os.Binder() {
                @Override protected boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) {
                    if (code != 1) return false;
                    android.app.PendingIntent pending = android.app.PendingIntent.getActivity(Worker.this, 1,
                            target("Receipt").putExtra("marker", "original"),
                            android.app.PendingIntent.FLAG_IMMUTABLE | android.app.PendingIntent.FLAG_ONE_SHOT);
                    reply.writeNoException(); reply.writeTypedObject(pending, 0); return true;
                }
            };
        }
    }
    public static class Receipt extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            startActivity(target("ForwardReceipt").putExtras(getIntent()).addFlags(Intent.FLAG_ACTIVITY_FORWARD_RESULT));
            finish();
        }
    }
    public static class ForwardReceipt extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            setResult(RESULT_OK, new Intent().putExtra("immutable", "original".equals(getIntent().getStringExtra("marker")))
                    .putExtra("nested_caller", new ComponentName("com.google.android.gms", PREFIX + "Echo").equals(getCallingActivity())));
            finish();
        }
    }
    public static class Cancel extends Activity {
        @Override public void onCreate(Bundle state) { super.onCreate(state); finish(); }
    }
    public static class Wait extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            TextView view = new TextView(this); view.setText("Remote activity fixture: waiting for host-death test");
            setContentView(view); System.err.println("[WL-ACTIVITY-TEST] waiting=true pid=" + android.os.Process.myPid());
        }
        @Override public void onDestroy() { System.err.println("[WL-ACTIVITY-TEST] wait_destroyed=true"); super.onDestroy(); }
    }
    public static class Private extends Activity {}
    public static class Disabled extends Activity {}
    public static class Protected extends Activity {}
    public static class Client extends Activity {
        final JSONObject results = new JSONObject();
        int replies;
        void put(String key, boolean value) { try { results.put(key, value); save(); } catch (Exception e) { throw new RuntimeException(e); } }
        void save() throws Exception {
            try (FileWriter writer = new FileWriter(new File(getFilesDir(), "remote-activity-results.json"))) { writer.write(results.toString(2)); }
        }
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            TextView view = new TextView(this); view.setText("Remote activity contract test"); setContentView(view);
            for (String name : new String[]{"Private", "Disabled", "Protected"}) {
                put(name + "_hidden", getPackageManager().queryIntentActivities(target(name), 0).isEmpty());
                boolean denied = false;
                try { startActivityForResult(target(name), 99); }
                catch (android.content.ActivityNotFoundException | SecurityException expected) { denied = true; }
                put(name + "_denied", denied);
            }
            Bundle nested = new Bundle(); nested.putParcelable("uri", Uri.parse("test://westlake/parcelable"));
            nested.putInt("number", 42);
            startActivityForResult(target("ForwardEntry").putExtra("nested", nested), 100);
        }
        @Override protected void onActivityResult(int request, int result, Intent data) {
            super.onActivityResult(request, result, data); replies++;
            if (request == 100) {
                put("echo_result", result == RESULT_OK && data != null);
                if (data != null) {
                    put("caller_identity", data.getBooleanExtra("caller_ok", false));
                    put("declared_process", data.getBooleanExtra("process_ok", false));
                    put("referrer", data.getBooleanExtra("referrer_ok", false));
                    put("pending_intent_result", data.getBooleanExtra("pending_result", false));
                    put("pending_intent_one_shot", data.getBooleanExtra("one_shot", false));
                    boolean denied = false;
                    android.app.PendingIntent foreign = data.getParcelableExtra("foreign_capability");
                    if (foreign != null) {
                        try { startIntentSenderForResult(foreign.getIntentSender(), 9, null, 0, 0, 0); }
                        catch (android.content.IntentSender.SendIntentException expected) { denied = true; }
                    }
                    put("foreign_forwarded_intent_read_denied", denied);
                    Bundle nested = data.getBundleExtra("nested");
                    put("nested_parcelable", nested != null && nested.getInt("number") == 42
                            && Uri.parse("test://westlake/parcelable").equals(nested.getParcelable("uri")));
                }
                startActivityForResult(target("Cancel"), 101);
            } else if (request == 101) {
                put("explicit_cancel", result == RESULT_CANCELED && data == null);
                startActivityForResult(target("Wait"), 102);
                put("waiting_for_host_death", true);
            } else if (request == 102) {
                put("host_death_cancel", result == RESULT_CANCELED && data == null);
                put("exactly_three_results", replies == 3);
                System.err.println("[WL-ACTIVITY-TEST] complete=true");
            } else { put("unexpected_result", true); }
        }
    }
}
