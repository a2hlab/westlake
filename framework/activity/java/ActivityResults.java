package adapter.activity;

import android.app.ActivityThread;
import android.app.IApplicationThread;
import android.app.ResultInfo;
import android.app.servertransaction.ActivityResultItem;
import android.app.servertransaction.ClientTransaction;
import android.content.Intent;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

import java.util.Collections;
import java.util.Map;
import java.util.WeakHashMap;

/**
 * startActivityForResult between the app's own activities: which activity asked, and its answer.
 *
 * The started activity's setResult and finish reached finishActivity, which dropped the result:
 * the caller's onActivityResult never ran. mpv's file picker returned the chosen file that way and
 * nothing played; every in-app picker, sub-screen returning a choice and sign-in step that hands
 * back a token does the same (346 corpus apps call startActivityForResult). ActivityTaskManager
 * keeps the caller in the started activity's record (resultTo, resultWho, requestCode) and, when
 * the activity finishes, sends its result to the caller before the caller resumes, always:
 * RESULT_CANCELED with no data when it set none.
 *
 * Here the start records the caller with the launch's pending Intent (PendingLaunchIntents), the
 * launch binds it to the new activity's token, and finishActivity delivers the result to the
 * caller with an ActivityResultItem, as the permission-request answer is delivered
 * (ActivityTaskManagerAdapter.answerPermissionRequest).
 */
final class ActivityResults {
    /** Who asked for a result: the caller's token, its embedded id and its request code. */
    static final class Request {
        final IBinder resultTo;
        final String resultWho;
        final int requestCode;

        Request(IBinder resultTo, String resultWho, int requestCode) {
            this.resultTo = resultTo;
            this.resultWho = resultWho;
            this.requestCode = requestCode;
        }
    }

    // Keyed by the started activity's token, held weakly: a token outlives no activity of its own.
    private static final Map<IBinder, Request> sPending = new WeakHashMap<>();

    private ActivityResults() {}

    /** The request a start asked for, or null for a start that wants no result. */
    static Request requested(IBinder resultTo, String resultWho, int requestCode) {
        return resultTo != null && requestCode >= 0 ? new Request(resultTo, resultWho, requestCode) : null;
    }

    /** Called as the started activity is launched, with its token. */
    static synchronized void bind(IBinder started, Request request) {
        if (started != null && request != null) sPending.put(started, request);
    }

    /**
     * Called as an activity finishes: when something asked it for a result, the result goes to that
     * caller. Posted, so finish() returns first, as on Android.
     */
    static void deliver(IBinder finishing, final int resultCode, final Intent data) {
        final Request request;
        synchronized (ActivityResults.class) {
            request = sPending.remove(finishing);
        }
        if (request == null) return;
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                ActivityThread thread = ActivityThread.currentActivityThread();
                if (thread == null || thread.getActivity(request.resultTo) == null) {
                    System.err.println("[WESTLAKE-RESULT] the caller of requestCode=" + request.requestCode
                            + " is gone; result dropped");
                    return;
                }
                IApplicationThread app = thread.getApplicationThread();
                ClientTransaction transaction = ClientTransaction.obtain(app);
                transaction.addTransactionItem(ActivityResultItem.obtain(request.resultTo, Collections.singletonList(
                        new ResultInfo(request.resultWho, request.requestCode, resultCode, data))));
                app.scheduleTransaction(transaction);
                System.err.println("[WESTLAKE-RESULT] delivered requestCode=" + request.requestCode
                        + " resultCode=" + resultCode + " data=" + (data != null) + " to "
                        + thread.getActivity(request.resultTo).getClass().getName());
            } catch (Throwable t) {
                System.err.println("[WESTLAKE-RESULT] delivery failed: " + t);
            }
        });
    }
}
