package org.westlake.syncprobe;

import android.accounts.Account;
import android.app.Service;
import android.content.AbstractThreadedSyncAdapter;
import android.content.ContentProviderClient;
import android.content.Intent;
import android.content.SyncResult;
import android.os.Bundle;
import android.os.IBinder;

public final class ProbeService extends Service {
    private AbstractThreadedSyncAdapter adapter;
    @Override public void onCreate() {
        super.onCreate();
        adapter = new AbstractThreadedSyncAdapter(this, false) {
            @Override public void onPerformSync(Account account, Bundle extras, String authority,
                    ContentProviderClient provider, SyncResult result) {
                android.content.SharedPreferences p = getSharedPreferences("probe", MODE_PRIVATE);
                if (!p.edit().putInt("calls", p.getInt("calls", 0) + 1).commit())
                    result.databaseError = true;
                if (extras.getBoolean("periodic_marker"))
                    p.edit().putInt("periodicCalls", p.getInt("periodicCalls", 0) + 1).commit();
            }
        };
    }
    @Override public IBinder onBind(Intent intent) { return adapter.getSyncAdapterBinder(); }
}
