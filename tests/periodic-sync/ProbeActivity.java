package org.westlake.syncprobe;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.app.Activity;
import android.content.ContentResolver;
import android.content.PeriodicSync;
import android.content.SharedPreferences;
import android.content.SyncRequest;
import android.os.Bundle;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import org.json.JSONObject;

/** A local fixture account and a real sync adapter; no Google account or network. */
public final class ProbeActivity extends Activity {
    private static final String AUTHORITY = "org.westlake.syncprobe.data";
    private final Account account = new Account("local-fixture", "org.westlake.syncprobe");
    private int checks;
    private void yes(boolean value) { if (!value) throw new AssertionError("check " + (checks + 1)); checks++; }
    private void refused(Runnable call) {
        try { call.run(); throw new AssertionError("unauthorized operation accepted"); }
        catch (SecurityException | IllegalArgumentException expected) { checks++; }
    }
    private SharedPreferences prefs() { return getSharedPreferences("probe", MODE_PRIVATE); }
    private void await(boolean periodic) throws Exception {
        long end = android.os.SystemClock.elapsedRealtime() + 20_000;
        while (android.os.SystemClock.elapsedRealtime() < end) {
            if (prefs().getInt(periodic ? "periodicCalls" : "calls", 0) > 0
                    && !ContentResolver.isSyncActive(account, AUTHORITY)) return;
            Thread.sleep(100);
        }
        throw new AssertionError("sync adapter did not complete");
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        new Thread(() -> {
            int phase = prefs().getInt("phase", 1);
            JSONObject report = new JSONObject();
            try {
                report.put("phase", phase).put("started", System.currentTimeMillis());
                AccountManager manager = AccountManager.get(this);
                if (phase == 1) {
                    yes(manager.addAccountExplicitly(account, null, null));
                    ContentResolver.setIsSyncable(account, AUTHORITY, 1);
                    yes(ContentResolver.getPeriodicSyncs(account, AUTHORITY).isEmpty());
                    refused(() -> ContentResolver.addPeriodicSync(account, "foreign.authority", Bundle.EMPTY, 900));
                    refused(() -> ContentResolver.addPeriodicSync(new Account("missing", account.type), AUTHORITY, Bundle.EMPTY, 900));
                    Bundle invalid = new Bundle(); invalid.putBoolean(ContentResolver.SYNC_EXTRAS_MANUAL, true);
                    refused(() -> ContentResolver.addPeriodicSync(account, AUTHORITY, invalid, 900));
                    ContentResolver.setSyncAutomatically(account, AUTHORITY, true); await(false); yes(true);
                    int callsAfterEnabling = prefs().getInt("calls", 0);
                    ContentResolver.setSyncAutomatically(account, AUTHORITY, true);
                    yes(!ContentResolver.isSyncPending(account, AUTHORITY)
                            && !ContentResolver.isSyncActive(account, AUTHORITY)
                            && prefs().getInt("calls", 0) == callsAfterEnabling);
                    Bundle extra = new Bundle(); extra.putBoolean("periodic_marker", true);
                    SyncRequest request = new SyncRequest.Builder().setSyncAdapter(account, AUTHORITY)
                            .setExtras(extra).syncPeriodic(900, 900).build();
                    ContentResolver.requestSync(request); await(true); yes(true);
                    List<PeriodicSync> registered = ContentResolver.getPeriodicSyncs(account, AUTHORITY);
                    yes(registered.size() == 1); yes(registered.get(0).period == 900);
                    registered.get(0).extras.putString("mutation", "caller");
                    yes(!ContentResolver.getPeriodicSyncs(account, AUTHORITY).get(0).extras.containsKey("mutation"));
                    int calls = prefs().getInt("periodicCalls", 0);
                    ContentResolver.requestSync(request); Thread.sleep(300);
                    yes(ContentResolver.getPeriodicSyncs(account, AUTHORITY).size() == 1);
                    yes(prefs().getInt("periodicCalls", 0) == calls);
                    ContentResolver.cancelSync(request);
                    yes(ContentResolver.getPeriodicSyncs(account, AUTHORITY).isEmpty());
                    Bundle persisted = new Bundle(); persisted.putString("marker", "persisted");
                    ContentResolver.addPeriodicSync(account, AUTHORITY, persisted, 3600);
                    yes(ContentResolver.getPeriodicSyncs(account, AUTHORITY).size() == 1);
                    yes(prefs().edit().putInt("phase", 2).commit());
                } else if (phase == 2) {
                    List<PeriodicSync> restored = ContentResolver.getPeriodicSyncs(account, AUTHORITY);
                    yes(restored.size() == 1); yes(restored.get(0).period == 3600);
                    yes("persisted".equals(restored.get(0).extras.getString("marker")));
                    yes(ContentResolver.getSyncAutomatically(account, AUTHORITY));
                    Bundle wrong = new Bundle(); wrong.putString("marker", "other");
                    ContentResolver.removePeriodicSync(account, AUTHORITY, wrong);
                    yes(ContentResolver.getPeriodicSyncs(account, AUTHORITY).size() == 1);
                    ContentResolver.removePeriodicSync(account, AUTHORITY, restored.get(0).extras);
                    yes(ContentResolver.getPeriodicSyncs(account, AUTHORITY).isEmpty());
                    yes(prefs().edit().putInt("phase", 3).commit());
                } else throw new AssertionError("fixture already completed");
                report.put("passed", true);
            } catch (Throwable failure) {
                try { report.put("passed", false).put("failureType", failure.getClass().getSimpleName())
                        .put("failureMessage", failure.getMessage()); } catch (Exception ignored) { }
            } finally {
                try {
                    report.put("checks", checks).put("ended", System.currentTimeMillis());
                    File f = new File(getFilesDir(), "periodic-sync-phase" + phase + ".json");
                    try (FileOutputStream out = new FileOutputStream(f)) {
                        out.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
                    }
                } catch (Exception ignored) { }
                runOnUiThread(this::finish);
            }
        }, "periodic-sync-probe").start();
    }
}
