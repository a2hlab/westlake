package adapter.activity;

import android.app.Activity;
import android.app.ActivityThread;
import android.app.AlertDialog;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.drawable.Drawable;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.RemoteException;

import java.util.List;

/**
 * The user's confirmation of a package change an installer asked for (ADR-0006 decision 3): Westlake's own
 * stand-in for the system package installer's confirmation screens, "Do you want to install this app?", to update
 * it or to uninstall it.
 *
 * On Android an installer without INSTALL_PACKAGES gets STATUS_PENDING_USER_ACTION and starts the system
 * installer's confirmation activity itself. The Play Store holds that permission on a phone, so it never expects
 * that status; on Westlake, as on GrapheneOS, it is an ordinary app and holds no such permission. As GrapheneOS's
 * PlayStoreHooks do for its sandboxed Store, Westlake asks the user on the installer's behalf and gives the
 * installer only the outcome: installed, or STATUS_FAILURE_ABORTED when the user declined. OH refuses activity
 * starts from Westlake processes and Westlake's notifications do not reach OH's notification centre, so the
 * question is a dialog over the installer's top activity, in its launched process: the process with its windows
 * (a session lives in the process that created it, the Store's :background one, which has none).
 *
 * The answer goes back on the answer binder the asker supplied: one transaction, ANSWER, with the user's choice.
 * Dismissing the dialog, by Back or outside it, declines.
 */
public final class InstallConfirmDialog {
    private static final String TAG = "[WL-INSTALL] ";
    /** The one transaction on an answer binder: an int, 1 when the user accepted. */
    public static final int ANSWER = IBinder.FIRST_CALL_TRANSACTION;

    /** Request keys: the session (or 0 for an uninstall), the package, what is asked, and where its files are. */
    public static final String SESSION = "session";
    public static final String PACKAGE = "package";
    /** "install", "update" or "uninstall". */
    public static final String KIND = "kind";
    /** An APK of the package: the staged base APK, or the installed one for an uninstall. Its label and icon. */
    public static final String APK = "apk";
    /** The installer that asked, by package name. */
    public static final String INSTALLER = "installer";
    /** For an update: the package's update owner, when another installer asks (Android's ownership reminder). */
    public static final String UPDATE_OWNER = "updateOwner";

    private InstallConfirmDialog() {}

    /**
     * Asks the user about request, answering on answer: in this process if it is the app's launched one, else in
     * that process (PlayServicesChannel.CONFIRM). False when there is nowhere to ask: no live activity to show it
     * over, or the launched process could not be reached.
     */
    public static boolean ask(Bundle request, IBinder answer) {
        if (AppProcesses.isSecondary()) return PlayServicesChannel.confirmInHome(request, answer);
        return show(request, answer);
    }

    /** Shows the question over this process's top activity, on the main thread. False when there is none. */
    static boolean show(final Bundle request, final IBinder answer) {
        ActivityThread thread = ActivityThread.currentActivityThread();
        if (thread == null || request == null || answer == null) return false;
        List<IBinder> tokens = ActivityClientControllerAdapter.taskTokens();
        Activity over = null;
        for (int i = tokens.size() - 1; i >= 0 && over == null; i--) {
            Activity candidate = thread.getActivity(tokens.get(i));
            if (candidate != null && !candidate.isFinishing() && !candidate.isDestroyed()) over = candidate;
        }
        if (over == null) {
            System.err.println(TAG + "no activity to ask the user over for " + request.getString(PACKAGE));
            return false;
        }
        final Activity activity = over;
        new Handler(Looper.getMainLooper()).post(() -> open(activity, request, answer));
        return true;
    }

    private static void open(Activity activity, Bundle request, IBinder answer) {
        final String packageName = request.getString(PACKAGE);
        final String kind = request.getString(KIND, "install");
        final boolean[] answered = {false};
        try {
            CharSequence label = packageName;
            Drawable icon = null;
            PackageManager pm = activity.getPackageManager();
            String apk = request.getString(APK);
            try {
                PackageInfo info = apk != null ? pm.getPackageArchiveInfo(apk, 0) : null;
                if (info != null && info.applicationInfo != null) {
                    ApplicationInfo app = info.applicationInfo;
                    app.sourceDir = apk;
                    app.publicSourceDir = apk;
                    label = pm.getApplicationLabel(app);
                    icon = pm.getApplicationIcon(app);
                }
            } catch (RuntimeException e) {
                System.err.println(TAG + "label and icon of " + packageName + " not read: " + e);
            }
            String question;
            String yes;
            switch (kind) {
                case "update":
                    question = "Do you want to update this app?";
                    yes = "Update";
                    break;
                case "uninstall":
                    question = "Do you want to uninstall this app?";
                    yes = "Uninstall";
                    break;
                default:
                    question = "Do you want to install this app?";
                    yes = "Install";
                    break;
            }
            CharSequence installer = request.getString(INSTALLER);
            try {
                if (installer != null) installer = pm.getApplicationLabel(pm.getApplicationInfo(installer.toString(), 0));
            } catch (PackageManager.NameNotFoundException | RuntimeException ignored) {
                // the package name, then
            }
            String message = question + (installer != null && !"uninstall".equals(kind)
                    ? "\n\nAsked by " + installer + "." : "");
            String owner = request.getString(UPDATE_OWNER);
            if (owner != null) {
                CharSequence ownerLabel = owner;
                try {
                    ownerLabel = pm.getApplicationLabel(pm.getApplicationInfo(owner, 0));
                } catch (PackageManager.NameNotFoundException | RuntimeException ignored) {
                    // the package name, then
                }
                message += "\n\nThis app normally gets its updates from " + ownerLabel
                        + ". If you update it from another source, its future updates may come from any source.";
            }
            AlertDialog.Builder builder = new AlertDialog.Builder(activity)
                    .setTitle(label)
                    .setMessage(message)
                    .setPositiveButton(yes, (d, which) -> reply(answer, answered, true, packageName, kind))
                    .setNegativeButton(android.R.string.cancel, (d, which) -> reply(answer, answered, false, packageName, kind))
                    .setOnDismissListener(d -> reply(answer, answered, false, packageName, kind));
            if (icon != null) builder.setIcon(icon);
            builder.show();
            System.err.println(TAG + "asking the user: " + kind + " " + packageName + " (" + label + ") over "
                    + activity.getComponentName().flattenToShortString());
        } catch (Throwable throwable) {
            System.err.println(TAG + "could not ask the user about " + packageName + ": " + throwable);
            reply(answer, answered, false, packageName, kind);
        }
    }

    /** The first answer only: a click is followed by the dialog's dismissal, which must not answer again. */
    private static void reply(IBinder answer, boolean[] answered, boolean accepted, String packageName, String kind) {
        if (answered[0]) return;
        answered[0] = true;
        System.err.println(TAG + "the user " + (accepted ? "accepted" : "declined") + ": " + kind + " " + packageName);
        Parcel data = Parcel.obtain();
        try {
            data.writeInt(accepted ? 1 : 0);
            answer.transact(ANSWER, data, null, IBinder.FLAG_ONEWAY);
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "answer for " + packageName + " not delivered: " + e);
        } finally {
            data.recycle();
        }
    }

    /**
     * An answer binder that waits: the asker creates one, passes it to ask() and waits for the user. A null from
     * await is no answer in the time given.
     */
    public static final class Answer extends Binder {
        private final java.util.concurrent.CountDownLatch mDone = new java.util.concurrent.CountDownLatch(1);
        private volatile boolean mAccepted;

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code != ANSWER) return super.onTransact(code, data, reply, flags);
            if (Binder.getCallingUid() != android.os.Process.myUid()) {
                throw new SecurityException("only this app's own processes answer its questions");
            }
            mAccepted = data.readInt() == 1;
            mDone.countDown();
            return true;
        }

        public Boolean await(long minutes) {
            try {
                return mDone.await(minutes, java.util.concurrent.TimeUnit.MINUTES) ? mAccepted : null;
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return null;
            }
        }
    }
}
