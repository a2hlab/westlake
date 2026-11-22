/*
 * SplashScreenHandoff.java
 *
 * Android 12 and later hand an activity's splash screen to the app when the app asks for it. An
 * activity with a SplashScreen.OnExitAnimationListener (androidx core-splashscreen sets one through
 * the platform on API 31 and later) gets, after its first frame, a SplashScreenView copied from the
 * system's starting window: ActivityThread.createSplashScreen adds it to the decor view, and at its
 * first draw reportSplashscreenViewShown hands it to the listener
 * (SplashScreenManagerGlobal.handOverSplashScreenView). The app animates it out and removes it.
 *
 * Westlake has no system starting window to copy and never handed a view over, so an exit listener
 * was never called. An app that goes on only from that call stayed on its first frame: ZipoApps
 * PremiumHelper 5.x (its SplashScreenConfiguratorImpl33, chosen on SDK 33 and later) starts its own
 * initialization when its exit animation ends, so its splash activity stayed a plain white screen.
 *
 * Here the app's process makes the view itself from what the starting window would show: the
 * starting theme's windowSplashScreenBackground and windowSplashScreenAnimatedIcon, else the window
 * background's color and the activity's icon. The view is added once the activity has drawn a frame
 * (an app holding its frames with a keep-on-screen condition holds the hand-off too, as the starting
 * window would stay) and handed over at its own first draw, once per activity.
 */
package adapter.activity;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.Window;
import android.window.SplashScreen;
import android.window.SplashScreenView;

import java.util.Collections;
import java.util.Map;
import java.util.WeakHashMap;

final class SplashScreenHandoff {

    /** The starting window's icon size (com.android.internal.R.dimen.starting_surface_icon_size). */
    private static final int ICON_SIZE_DP = 160;

    /** Activities whose view is made or being made: once per activity, as a starting window. */
    private static final Map<IBinder, Boolean> sStarted =
            Collections.synchronizedMap(new WeakHashMap<>());

    private SplashScreenHandoff() {}

    /**
     * From activityResumed, for an activity with an exit listener (the handleSplashScreenExit that
     * ResumeActivityItem reports from ActivityThread.isHandleSplashScreenExit).
     */
    static void onResumed(final IBinder token, final Activity activity) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            new Handler(Looper.getMainLooper()).post(() -> onResumed(token, activity));
            return;
        }
        if (sStarted.put(token, Boolean.TRUE) != null) return;
        Window window = activity.getWindow();
        final View decor = window != null ? window.peekDecorView() : null;
        if (!(decor instanceof ViewGroup)) {
            log(activity, "no decor view, so its exit listener is not called");
            return;
        }
        final ViewTreeObserver.OnDrawListener[] listener = new ViewTreeObserver.OnDrawListener[1];
        final boolean[] drawn = new boolean[1];
        listener[0] = () -> {
            if (drawn[0]) return;
            drawn[0] = true;
            // A draw listener may not remove itself while the frame draws.
            decor.post(() -> {
                decor.getViewTreeObserver().removeOnDrawListener(listener[0]);
                attach(token, activity, (ViewGroup) decor);
            });
        };
        decor.getViewTreeObserver().addOnDrawListener(listener[0]);
        // The first frame may have drawn already: ask for another (a held frame stays held).
        decor.invalidate();
    }

    /** As ActivityThread.createSplashScreen: add the view, hand it over at its first draw. */
    private static void attach(final IBinder token, final Activity activity, final ViewGroup decor) {
        if (activity.isFinishing() || activity.isDestroyed()) return;
        if (!SplashScreen.SplashScreenManagerGlobal.getInstance().containsExitListener(token)) return;
        final SplashScreenView view;
        try {
            view = build(activity);
        } catch (RuntimeException e) {
            log(activity, "its splash screen view could not be made (" + e
                    + "), so its exit listener is not called");
            return;
        }
        view.attachHostWindow(activity.getWindow());
        decor.addView(view);
        view.requestLayout();
        view.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
            private boolean mHandled;

            @Override
            public boolean onPreDraw() {
                if (mHandled) return true;
                mHandled = true;
                decor.postOnAnimation(() -> handOver(token, activity, view));
                view.post(() -> view.getViewTreeObserver().removeOnPreDrawListener(this));
                return true;
            }
        });
    }

    /** As ActivityThread.reportSplashscreenViewShown, less the starting window it would remove. */
    private static void handOver(IBinder token, Activity activity, SplashScreenView view) {
        SplashScreen.SplashScreenManagerGlobal global =
                SplashScreen.SplashScreenManagerGlobal.getInstance();
        if (activity.isDestroyed() || !global.containsExitListener(token)) {
            view.remove();
            return;
        }
        log(activity, "its splash screen view goes to its exit listener");
        global.handOverSplashScreenView(token, view);
    }

    /** The view the starting window would show, from the activity's starting (manifest) theme. */
    private static SplashScreenView build(Activity activity) {
        Resources res = activity.getResources();
        PackageManager pm = activity.getPackageManager();
        ActivityInfo info = null;
        try {
            info = pm.getActivityInfo(activity.getComponentName(), 0);
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            // The activity's current theme and the application's icon, then.
        }
        Resources.Theme theme = activity.getTheme();
        int themeId = info != null ? info.getThemeResource() : 0;
        if (themeId != 0) {
            // The activity's theme may have moved on since: androidx's installSplashScreen applies
            // postSplashScreenTheme in onCreate.
            theme = res.newTheme();
            theme.applyStyle(themeId, true);
        }
        TypedValue value = new TypedValue();
        Integer background = color(res, theme, android.R.attr.windowSplashScreenBackground, value);
        if (background == null) {
            Drawable window = drawable(res, theme, android.R.attr.windowBackground, value);
            if (window instanceof ColorDrawable) {
                int c = ((ColorDrawable) window).getColor();
                if (Color.alpha(c) != 0) background = c;
            }
        }
        if (background == null) background = color(res, theme, android.R.attr.colorBackground, value);
        if (background == null) {
            boolean night = (res.getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
                    == Configuration.UI_MODE_NIGHT_YES;
            background = night ? Color.BLACK : Color.WHITE;
        }
        Drawable icon = drawable(res, theme, android.R.attr.windowSplashScreenAnimatedIcon, value);
        if (icon == null) {
            icon = info != null ? info.loadIcon(pm) : activity.getApplicationInfo().loadIcon(pm);
        }
        int iconSize;
        try {
            iconSize = res.getDimensionPixelSize(
                    com.android.internal.R.dimen.starting_surface_icon_size);
        } catch (Resources.NotFoundException e) {
            iconSize = Math.round(TypedValue.applyDimension(
                    TypedValue.COMPLEX_UNIT_DIP, ICON_SIZE_DP, res.getDisplayMetrics()));
        }
        return new SplashScreenView.Builder(activity)
                .setBackgroundColor(background | 0xFF000000)
                .setIconSize(iconSize)
                .setCenterViewDrawable(icon)
                .build();
    }

    /** A color attribute's value, or null when it is unset or fully transparent. */
    private static Integer color(Resources res, Resources.Theme theme, int attr, TypedValue value) {
        if (!theme.resolveAttribute(attr, value, true)) return null;
        Integer c = null;
        if (value.type >= TypedValue.TYPE_FIRST_COLOR_INT
                && value.type <= TypedValue.TYPE_LAST_COLOR_INT) {
            c = value.data;
        } else if (value.resourceId != 0) {
            try {
                c = res.getColor(value.resourceId, theme);
            } catch (Resources.NotFoundException e) {
                return null;
            }
        }
        return c != null && Color.alpha(c) != 0 ? c : null;
    }

    private static Drawable drawable(Resources res, Resources.Theme theme, int attr,
            TypedValue value) {
        if (!theme.resolveAttribute(attr, value, true)) return null;
        try {
            if (value.resourceId != 0) return res.getDrawable(value.resourceId, theme);
            if (value.type >= TypedValue.TYPE_FIRST_COLOR_INT
                    && value.type <= TypedValue.TYPE_LAST_COLOR_INT) {
                return new ColorDrawable(value.data);
            }
        } catch (RuntimeException e) {
            // Unset, then.
        }
        return null;
    }

    private static void log(Activity activity, String what) {
        System.err.println("[WL-SPLASH] " + activity.getComponentName().flattenToShortString()
                + ": " + what);
    }
}
