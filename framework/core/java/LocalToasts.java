package adapter.core;

import android.app.Activity;
import android.app.ActivityThread;
import android.app.Application;
import android.app.INotificationManager;
import android.app.ITransientNotification;
import android.app.ITransientNotificationCallback;
import android.content.Context;
import android.content.res.Resources;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ServiceManager;
import android.util.TypedValue;
import android.view.ContextThemeWrapper;
import android.view.Gravity;
import android.view.View;
import android.view.accessibility.IAccessibilityManager;
import android.widget.Toast;
import android.widget.ToastPresenter;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Toasts, shown in process: the queue NotificationManagerService keeps, and the window SystemUI
 * draws a text toast in. Nothing answered enqueueTextToast or enqueueToast, so no toast was ever
 * seen: an app's "copied", "saved" or "no browser found" said nothing.
 *
 * As the service does, one toast shows at a time, in the order the app asked for them, with at most
 * five waiting per package. Showing a toast again updates its duration in place. Each shows for 2 s
 * (LENGTH_SHORT) or 3.5 s (LENGTH_LONG), allowing for its animations, then the next one shows; a
 * cancelled toast goes at once. A text toast is drawn as SystemUI draws it: ToastPresenter puts the
 * framework's text toast view, in a system theme rather than the app's, at the bottom of the screen,
 * and the toast's callbacks (Toast.Callback) hear it shown and hidden. A custom toast (setView, or any toast of an app that
 * targets Android 10 or lower) is shown and hidden through its own TN, as the service calls it.
 *
 * The toast window is neither touchable nor focusable (ToastPresenter's flags), so taps and focus
 * stay with the activity under it. It takes the window token of the activity on top, once that
 * activity has its window, and the window adapter shows it over the activity as it shows a dialog.
 */
final class LocalToasts {
    private LocalToasts() {}

    private static final String TAG = "[WL-TOAST]";
    private static final int MAX_PACKAGE_TOASTS = 5;
    private static final long SHORT_DELAY = 2000;
    private static final long LONG_DELAY = 3500;
    private static final long IN_ANIMATION = 333;    // a text toast's in-animation
    private static final long OUT_ANIMATION = 250;   // the previous text toast's out-animation

    private static final class Record {
        final String pkg;
        final IBinder token;
        final CharSequence text;           // a text toast
        final ITransientNotification tn;   // a custom toast
        final ITransientNotificationCallback callback;
        int duration;
        ToastPresenter presenter;          // a text toast, while it shows

        Record(String pkg, IBinder token, CharSequence text, ITransientNotification tn, int duration,
                ITransientNotificationCallback callback) {
            this.pkg = pkg;
            this.token = token;
            this.text = text;
            this.tn = tn;
            this.duration = duration;
            this.callback = callback;
        }

        @Override
        public String toString() {
            return text != null ? "text toast \"" + text + "\"" : "custom toast";
        }
    }

    private static final List<Record> sQueue = new ArrayList<>();
    private static final Handler sMain = new Handler(Looper.getMainLooper());
    private static Record sShowing;
    private static boolean sLastWasText;

    /** The toast calls of INotificationManager; null for any other call. */
    static Object answer(String method, Object[] args) {
        switch (method) {
            case "enqueueTextToast":   // (pkg, token, text, duration, isUiContext, displayId, callback)
                return enqueue((String) args[0], (IBinder) args[1], (CharSequence) args[2], null,
                        (Integer) args[3], (ITransientNotificationCallback) args[6]);
            case "enqueueToast":       // (pkg, token, tn, duration, isUiContext, displayId)
                return enqueue((String) args[0], (IBinder) args[1], null, (ITransientNotification) args[2],
                        (Integer) args[3], null);
            case "cancelToast":        // (pkg, token)
                cancel((String) args[0], (IBinder) args[1]);
                return LocalServiceBinders.DEFAULT;
            case "finishToken":        // the toast's window is gone: no window token to remove here
                return LocalServiceBinders.DEFAULT;
            default:
                return null;
        }
    }

    private static boolean enqueue(String pkg, IBinder token, CharSequence text, ITransientNotification tn,
            int duration, ITransientNotificationCallback callback) {
        if (pkg == null || token == null || (text == null) == (tn == null)) {
            System.err.println(TAG + " not enqueuing a toast with no package, no token, or not one of text and view");
            return false;
        }
        synchronized (sQueue) {
            for (Record record : sQueue) {
                if (record.pkg.equals(pkg) && record.token == token) {
                    record.duration = duration;   // shown again: updated where it is, not moved
                    return true;
                }
            }
            int count = 0;
            for (Record record : sQueue) {
                if (record.pkg.equals(pkg) && ++count >= MAX_PACKAGE_TOASTS) {
                    System.err.println(TAG + " package has already queued " + count + " toasts; not showing more");
                    return false;
                }
            }
            sQueue.add(new Record(pkg, token, text, tn, duration, callback));
        }
        sMain.post(LocalToasts::showNext);
        return true;
    }

    private static void cancel(String pkg, IBinder token) {
        sMain.post(() -> {
            Record cancelled = null;
            synchronized (sQueue) {
                for (Record record : sQueue) {
                    if (record.pkg.equals(pkg) && record.token == token) {
                        cancelled = record;
                        break;
                    }
                }
                if (cancelled == null) return;
                sQueue.remove(cancelled);
            }
            if (cancelled == sShowing) {
                sMain.removeCallbacksAndMessages(cancelled);
                hide(cancelled);
                sShowing = null;
                showNext();
            }
        });
    }

    /** Main thread: the first toast in the queue shows, unless one already does. */
    private static void showNext() {
        while (sShowing == null) {
            Record record;
            synchronized (sQueue) {
                if (sQueue.isEmpty()) return;
                record = sQueue.get(0);
            }
            if (show(record)) {
                sShowing = record;
                long delay = record.duration == Toast.LENGTH_LONG ? LONG_DELAY : SHORT_DELAY;
                if (sLastWasText) delay += OUT_ANIMATION;
                if (record.text != null) delay += IN_ANIMATION;
                sMain.postAtTime(() -> durationReached(record), record,
                        android.os.SystemClock.uptimeMillis() + delay);
                return;
            }
            synchronized (sQueue) {
                sQueue.remove(record);
            }
        }
    }

    private static void durationReached(Record record) {
        if (sShowing != record) return;
        hide(record);
        synchronized (sQueue) {
            sQueue.remove(record);
        }
        sShowing = null;
        sLastWasText = record.text != null;
        showNext();
    }

    /** Main thread: puts the toast's window up; false when it cannot be shown. */
    private static boolean show(Record record) {
        Activity top = topActivity();
        Context context = top != null ? top : ActivityThread.currentApplication();
        IBinder windowToken = top != null ? top.getActivityToken() : new Binder();
        try {
            if (record.text != null) {
                ToastPresenter presenter = new ToastPresenter(context, IAccessibilityManager.Stub.asInterface(
                        ServiceManager.getService(Context.ACCESSIBILITY_SERVICE)),
                        INotificationManager.Stub.asInterface(ServiceManager.getService(Context.NOTIFICATION_SERVICE)),
                        record.pkg);
                View view = ToastPresenter.getTextToastView(toastContext(), record.text);
                Resources res = context.getResources();
                presenter.show(view, record.token, windowToken, record.duration, defaultGravity(res), 0,
                        defaultYOffset(res), 0, 0, record.callback);
                record.presenter = presenter;
            } else {
                record.tn.show(windowToken);
            }
            System.err.println(TAG + " showing " + record + (top != null ? " over " + top.getClass().getSimpleName() : ""));
            return true;
        } catch (Throwable t) {
            System.err.println(TAG + " " + record + " could not be shown: " + t);
            return false;
        }
    }

    private static void hide(Record record) {
        try {
            if (record.presenter != null) {
                record.presenter.hide(record.callback);
                record.presenter = null;
            } else if (record.tn != null) {
                record.tn.hide();
            }
        } catch (Throwable t) {
            System.err.println(TAG + " " + record + " could not be hidden: " + t);
        }
    }

    /**
     * The context a text toast's view is made in: a system theme, as SystemUI draws the toast, and
     * the application's own layout inflater. An activity's inflater carries the app's view
     * factories (AppCompat's, and Fossify's, which colour its text views itself), and Fossify's
     * toast text came out nearly white on a white frame. The theme is Material's, light or dark by
     * the night mode.
     */
    private static Context toastContext() {
        Application app = ActivityThread.currentApplication();
        boolean night = (app.getResources().getConfiguration().uiMode
                & android.content.res.Configuration.UI_MODE_NIGHT_MASK)
                == android.content.res.Configuration.UI_MODE_NIGHT_YES;
        return new ContextThemeWrapper(app, night ? android.R.style.Theme_Material : android.R.style.Theme_Material_Light);
    }

    /** The toast's place, as Toast.TN and SystemUI take it from the framework's resources. */
    private static int defaultGravity(Resources res) {
        int id = Resources.getSystem().getIdentifier("config_toastDefaultGravity", "integer", "android");
        return id != 0 ? res.getInteger(id) : Gravity.CENTER_HORIZONTAL | Gravity.BOTTOM;
    }

    private static int defaultYOffset(Resources res) {
        int id = Resources.getSystem().getIdentifier("toast_y_offset", "dimen", "android");
        return id != 0 ? res.getDimensionPixelSize(id)
                : (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 24, res.getDisplayMetrics());
    }

    /**
     * The activity a toast shows over: the resumed one with a window, else any other with a window
     * that is not finishing. Its window is up, so the toast's window comes after it, and OH stacks
     * a window that comes later on top.
     */
    private static Activity topActivity() {
        try {
            Field field = ActivityThread.class.getDeclaredField("mActivities");
            field.setAccessible(true);
            Map<?, ?> records = (Map<?, ?>) field.get(ActivityThread.currentActivityThread());
            Activity fallback = null;
            for (Object record : records.values()) {
                Field activityField = record.getClass().getDeclaredField("activity");
                activityField.setAccessible(true);
                Activity activity = (Activity) activityField.get(record);
                if (activity == null || activity.isFinishing() || activity.getWindow() == null) continue;
                View decor = activity.getWindow().peekDecorView();
                if (decor == null || !decor.isAttachedToWindow()) continue;
                Field paused = record.getClass().getDeclaredField("paused");
                paused.setAccessible(true);
                if (!paused.getBoolean(record)) return activity;
                fallback = activity;
            }
            return fallback;
        } catch (Throwable t) {
            System.err.println(TAG + " no activity found to show a toast over: " + t);
            return null;
        }
    }
}
