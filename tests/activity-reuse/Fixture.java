package org.westlake.activityreuse.fixture;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.widget.TextView;

/** Local lifecycle tests with synthetic extras and no network or account permissions. */
public final class Fixture {
    static int redirectCreates, redirectDestroyed, topCreates, normalCreates;
    static void event(String value) { System.err.println("[WL-REUSE-TEST] " + value); }
    static void show(Activity activity, String text) {
        TextView view = new TextView(activity);
        view.setText(text);
        activity.setContentView(view);
    }

    public static class Root extends Activity {
        int completed;
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            show(this, "Activity reuse checks running");
            startActivityForResult(new Intent(this, RedirectTarget.class), 41);
            new Handler(getMainLooper()).postDelayed(() -> {
                if (completed != 3) event("FAIL timeout completed=" + completed);
            }, 15000);
        }
        @Override protected void onActivityResult(int request, int result, Intent data) {
            super.onActivityResult(request, result, data);
            boolean pass = result == RESULT_OK && data != null && data.getBooleanExtra("pass", false);
            event((pass ? "PASS" : "FAIL") + " request=" + request);
            if (!pass) { show(this, "Activity reuse check failed"); return; }
            completed++;
            if (request == 41) startActivityForResult(new Intent(this, Top.class), 42);
            else if (request == 42) startActivityForResult(new Intent(this, Normal.class), 43);
            else if (request == 43) {
                event("PASS all=3");
                show(this, "PASS: clear-top redirect, single-top, normal new instance");
            }
        }
    }

    public static class RedirectTarget extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            redirectCreates++;
            if (getIntent().getBooleanExtra("redirect", false)) {
                event("FAIL redirect_created_new_instance");
                setResult(RESULT_CANCELED); finish(); return;
            }
            startActivity(new Intent(this, Redirect.class));
        }
        @Override protected void onNewIntent(Intent intent) {
            super.onNewIntent(intent);
            boolean pass = redirectCreates == 1 && redirectDestroyed == 1
                    && intent.getBooleanExtra("redirect", false)
                    && "synthetic-payload".equals(intent.getStringExtra("payload"));
            setResult(RESULT_OK, new Intent().putExtra("pass", pass));
            finish();
        }
    }

    public static class Redirect extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            startActivityForResult(new Intent(this, RedirectTarget.class)
                    .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP)
                    .putExtra("redirect", true).putExtra("payload", "synthetic-payload"), 2);
        }
        @Override public void onDestroy() { redirectDestroyed++; super.onDestroy(); }
    }

    public static class Top extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            topCreates++;
            if (getIntent().getBooleanExtra("repeat", false)) {
                event("FAIL single_top_created_new_instance"); finish(); return;
            }
            startActivity(new Intent(this, Top.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
                    .putExtra("repeat", true));
        }
        @Override protected void onNewIntent(Intent intent) {
            super.onNewIntent(intent);
            setResult(RESULT_OK, new Intent().putExtra("pass", topCreates == 1
                    && intent.getBooleanExtra("repeat", false)));
            finish();
        }
    }

    public static class Normal extends Activity {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            normalCreates++;
            if (getIntent().getBooleanExtra("second", false)) {
                setResult(RESULT_OK); finish(); return;
            }
            startActivityForResult(new Intent(this, Normal.class).putExtra("second", true), 7);
        }
        @Override protected void onNewIntent(Intent intent) {
            super.onNewIntent(intent);
            event("FAIL ordinary_launch_reused");
        }
        @Override protected void onActivityResult(int request, int result, Intent data) {
            super.onActivityResult(request, result, data);
            setResult(RESULT_OK, new Intent().putExtra("pass", request == 7
                    && result == RESULT_OK && normalCreates == 2));
            finish();
        }
    }
}
