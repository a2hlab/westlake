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

/** Throwaway APKs on dedicated channels, with no real Google code or account. */
public final class Fixture {
    static final String CLIENT = "org.westlake.googlebootstrap.remoteactivity";
    static final String PREFIX = "org.westlake.remoteactivity.fixture.Fixture$";
    static Intent target(String name) {
        return new Intent("westlake.test.REMOTE").setComponent(new ComponentName("com.google.android.gms", PREFIX + name));
    }
    public static class Host extends Activity {}
    /** Tap the colored dialog to grow, shrink, then move it to the display origin. */
    public static class Resize extends Activity {
        int stage;
        android.app.Dialog dialog;
        android.view.View content;
        final int[] heights = {240, 720, 360, 480};
        final int[] colors = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffff00ff};
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            android.view.View background = new android.view.View(this);
            background.setBackgroundColor(android.graphics.Color.BLACK);
            setContentView(background);
            dialog = new android.app.Dialog(this);
            dialog.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE);
            content = new android.view.View(this) {
                final android.graphics.Paint paint = new android.graphics.Paint();
                @Override protected void onDraw(android.graphics.Canvas canvas) {
                    paint.setColor(android.graphics.Color.YELLOW);
                    canvas.drawRect(30, getHeight() - 60, getWidth() - 30, getHeight() - 30, paint);
                    paint.setColor(android.graphics.Color.WHITE);
                    canvas.drawRect(32, 32, 48, 48, paint);
                }
            };
            content.setOnClickListener(view -> android.os.Handler.createAsync(getMainLooper())
                    .postDelayed(() -> { stage = (stage + 1) % heights.length; resize(); }, 1000));
            dialog.setContentView(content);
            dialog.getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(0));
            dialog.getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            dialog.show();
            resize();
        }
        void resize() {
            content.setBackgroundColor(colors[stage]);
            android.view.Window window = dialog.getWindow();
            window.setGravity(stage == 3 ? android.view.Gravity.TOP | android.view.Gravity.LEFT
                    : android.view.Gravity.CENTER);
            window.setLayout(600, heights[stage]);
            System.err.println("[WL-RESIZE-TEST] stage=" + stage + " width=600 height=" + heights[stage]);
        }
        @Override public void onDestroy() { if (dialog != null) dialog.dismiss(); super.onDestroy(); }
    }
    public static class DiscoveryService extends android.app.Service {
        @Override public android.os.IBinder onBind(Intent intent) { return new android.os.Binder(); }
    }
    public static class DiscoveryProvider extends android.content.ContentProvider {
        @Override public boolean onCreate() { return true; }
        @Override public android.database.Cursor query(Uri u, String[] p, String s, String[] a, String o) { return null; }
        @Override public String getType(Uri u) { return null; }
        @Override public Uri insert(Uri u, android.content.ContentValues v) { throw new UnsupportedOperationException(); }
        @Override public int update(Uri u, android.content.ContentValues v, String s, String[] a) { throw new UnsupportedOperationException(); }
        @Override public int delete(Uri u, String s, String[] a) { throw new UnsupportedOperationException(); }
    }
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
                        service.transact(2, data, reply, 0); reply.readException();
                        answer.putExtra("foreign_capability", reply.readTypedObject(android.app.PendingIntent.CREATOR));
                        reply.setDataSize(0); reply.setDataPosition(0); data.setDataPosition(0);
                        service.transact(3, data, reply, 0); reply.readException();
                        answer.putExtra("foreign_wait", reply.readTypedObject(android.app.PendingIntent.CREATOR));
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
                        if (code < 1 || code > 3 || !ready.await(10, java.util.concurrent.TimeUnit.SECONDS) || worker == null) return false;
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
                    if (code < 1 || code > 3) return false;
                    android.app.PendingIntent pending = android.app.PendingIntent.getActivity(Worker.this, code,
                            target(code == 1 ? "Receipt" : code == 2 ? "ForeignReceipt" : "Wait").putExtra("marker", "original"),
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
                    .putExtra("nested_caller", new ComponentName("com.google.android.gms", PREFIX + "Echo").equals(getCallingActivity()))
                    .putExtra("foreign_caller", new ComponentName(CLIENT, PREFIX + "Client").equals(getCallingActivity()))
                    .putExtra("creator_process", "com.google.android.gms:ui".equals(Application.getProcessName())));
            finish();
        }
    }
    public static class ForeignReceipt extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            boolean callerApk = false;
            try {
                android.content.pm.ApplicationInfo app = getPackageManager().getApplicationInfo(CLIENT, 0);
                android.content.pm.PackageInfo archive = getPackageManager().getPackageArchiveInfo(app.sourceDir, 0);
                callerApk = archive != null && CLIENT.equals(archive.packageName)
                        && getPackageManager().getApplicationLabel(app) != null;
            } catch (Exception ignored) {}
            setResult(RESULT_OK, new Intent().putExtra("immutable", "original".equals(getIntent().getStringExtra("marker")))
                    .putExtra("foreign_caller", new ComponentName(CLIENT, PREFIX + "Client").equals(getCallingActivity()))
                    .putExtra("caller_apk", callerApk)
                    .putExtra("creator_process", "com.google.android.gms:result".equals(Application.getProcessName())));
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
        @Override public void onDestroy() {
            System.err.println("[WL-ACTIVITY-TEST] wait_destroyed=true"); super.onDestroy();
            new android.os.Handler(getMainLooper()).postDelayed(() -> {
                boolean released = false;
                try { getPackageManager().getApplicationInfo(CLIENT, 0); }
                catch (android.content.pm.PackageManager.NameNotFoundException expected) { released = true; }
                System.err.println("[WL-ACTIVITY-TEST] caller_metadata_released=" + released);
            }, 250);
        }
    }
    public static class Private extends Activity {}
    public static class Disabled extends Activity {}
    public static class Protected extends Activity {}
    public static class Client extends Activity {
        final JSONObject results = new JSONObject();
        android.app.PendingIntent waitCapability;
        int replies;
        void put(String key, boolean value) { try { results.put(key, value); save(); } catch (Exception e) { throw new RuntimeException(e); } }
        void save() throws Exception {
            try (FileWriter writer = new FileWriter(new File(getFilesDir(), "remote-activity-results.json"))) { writer.write(results.toString(2)); }
        }
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            TextView view = new TextView(this); view.setText("Remote activity contract test"); setContentView(view);
            // Both configured hosts also connect to each other. Unqualified discovery must terminate
            // at each owner, including misses; forwarding a miss used to create thousands of threads.
            boolean services = true, providers = true;
            for (int i = 0; i < 25; i++) {
                services &= getPackageManager().queryIntentServices(new Intent("westlake.test.DISCOVERY"), 0).size() == 2;
                services &= getPackageManager().queryIntentServices(new Intent("westlake.test.MISSING"), 0).isEmpty();
                android.content.pm.ProviderInfo host = getPackageManager().resolveContentProvider("westlake.fixture.host", 0);
                android.content.pm.ProviderInfo store = getPackageManager().resolveContentProvider("westlake.fixture.store", 0);
                providers &= host != null && "com.google.android.gms".equals(host.packageName)
                        && store != null && "com.android.vending".equals(store.packageName);
                providers &= getPackageManager().resolveContentProvider("westlake.fixture.missing", 0) == null;
            }
            put("mutual_host_service_discovery", services);
            put("mutual_host_provider_discovery", providers);
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
                    waitCapability = data.getParcelableExtra("foreign_wait");
                    put("caller_identity", data.getBooleanExtra("caller_ok", false));
                    put("declared_process", data.getBooleanExtra("process_ok", false));
                    put("referrer", data.getBooleanExtra("referrer_ok", false));
                    put("pending_intent_result", data.getBooleanExtra("pending_result", false));
                    put("pending_intent_one_shot", data.getBooleanExtra("one_shot", false));
                    android.app.PendingIntent foreign = data.getParcelableExtra("foreign_capability");
                    if (foreign != null) {
                        android.os.Parcel sender = android.os.Parcel.obtain(), read = android.os.Parcel.obtain(), reply = android.os.Parcel.obtain();
                        boolean denied = false;
                        try {
                            foreign.getIntentSender().writeToParcel(sender, 0); sender.setDataPosition(0);
                            android.os.IBinder capability = sender.readStrongBinder();
                            read.writeInterfaceToken("android.content.IIntentSender");
                            read.writeInt(0); read.writeInt(0); read.writeInt(0);
                            capability.transact(0x574c41, read, reply, 0); reply.readException();
                        } catch (SecurityException expected) { denied = true; }
                        catch (Exception error) { put("foreign_read_unexpected_error", true); }
                        finally { sender.recycle(); read.recycle(); reply.recycle(); }
                        put("foreign_forwarded_intent_read_denied", denied);
                        try {
                            startIntentSenderForResult(foreign.getIntentSender(), 9,
                                    new Intent().putExtra("marker", "forged"), 0, 0, 0);
                            boolean consumed = false;
                            try { startIntentSenderForResult(foreign.getIntentSender(), 10, null, 0, 0, 0); }
                            catch (android.content.IntentSender.SendIntentException expected) { consumed = true; }
                            put("foreign_one_shot", consumed);
                        } catch (Exception error) { put("foreign_execution_failed", true); }
                    }
                    Bundle nested = data.getBundleExtra("nested");
                    put("nested_parcelable", nested != null && nested.getInt("number") == 42
                            && Uri.parse("test://westlake/parcelable").equals(nested.getParcelable("uri")));
                }
                testForgedSender();
            } else if (request == 9) {
                put("foreign_execution_result", result == RESULT_OK && data != null);
                put("foreign_execution_immutable", data != null && data.getBooleanExtra("immutable", false));
                put("foreign_execution_caller", data != null && data.getBooleanExtra("foreign_caller", false));
                put("foreign_execution_process", data != null && data.getBooleanExtra("creator_process", false));
                put("foreign_execution_caller_apk", data != null && data.getBooleanExtra("caller_apk", false));
                startActivityForResult(target("Cancel"), 101);
            } else if (request == 101) {
                put("explicit_cancel", result == RESULT_CANCELED && data == null);
                try { startIntentSenderForResult(waitCapability.getIntentSender(), 102, null, 0, 0, 0); }
                catch (Exception error) { put("foreign_wait_failed", true); return; }
                put("waiting_for_host_death", true);
            } else if (request == 102) {
                put("host_death_cancel", result == RESULT_CANCELED && data == null);
                put("exactly_four_results", replies == 4);
                System.err.println("[WL-ACTIVITY-TEST] complete=true");
            } else { put("unexpected_result", true); }
        }

        void testForgedSender() {
            android.os.Binder forged = new android.os.Binder() {
                @Override protected boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) {
                    data.enforceInterface("android.content.IIntentSender");
                    reply.writeNoException();
                    if (code == 0x574c49) { reply.writeString("com.google.android.gms"); reply.writeInt(1); reply.writeInt(2); }
                    else if (code == 0x574c41) { reply.writeInt(1); target("Private").writeToParcel(reply, 0); }
                    else return false;
                    return true;
                }
            };
            android.os.Parcel parcel = android.os.Parcel.obtain();
            boolean denied = false;
            try {
                parcel.writeStrongBinder(forged); parcel.setDataPosition(0);
                android.content.IntentSender sender = android.content.IntentSender.CREATOR.createFromParcel(parcel);
                startIntentSenderForResult(sender, 11, null, 0, 0, 0);
            } catch (android.content.IntentSender.SendIntentException | SecurityException expected) { denied = true; }
            finally { parcel.recycle(); }
            put("forged_creator_denied", denied);
        }
    }
}
