/*
 * ActivityClientControllerAdapter.java
 *
 * Adapter for Android IActivityClientController interface using class inheritance.
 * Extends IActivityClientController.Stub directly — every AIDL method is overridden
 * with a typed signature checked at compile time.
 *
 * Replaces the prior Proxy.newProxyInstance + InvocationHandler approach
 * (B.20.r11), which was flagged in feedback.txt P0 as a recurrence of the
 * pattern user explicitly rejected in AA.23 (2026-04-27).  Per project iron
 * rule (CLAUDE.md "Forward Bridge Pattern (Class Inheritance)"), all AIDL
 * stub-side adapters must use class inheritance, not InvocationHandler.
 *
 * Routing strategy:
 *   - lifecycle reporters (activityIdle / activityResumed / activityPaused / ...) are
 *     informational signals from the client.  Native OH AbilityScheduler already
 *     has equivalent callbacks; the Android-side ones can be no-op for HelloWorld
 *     as the OH schedule path drives lifecycle directly.  Future stages may
 *     forward them to OH AbilityManagerService.
 *   - getters (getDisplayId / getCallingPackage / ...) return safe defaults.
 *   - controls (setRequestedOrientation / setImmersive / ...) are no-op.
 *
 * Method tags:
 *   [STUB]    - Returns safe default; no OH route yet.  HelloWorld doesn't need.
 *   [BRIDGED] - Routes to OH equivalent (none yet at B.30; placeholder for
 *               future Activity lifecycle bridge).
 *
 * Singleton accessor pattern matches DisplayManagerAdapter / ActivityManagerAdapter
 * etc., used by AppSpawnXInit.installActivityClientControllerStub() for
 * ActivityClient.INTERFACE_SINGLETON injection.
 */
package adapter.activity;

import android.app.ActivityManager;
import android.app.IActivityClientController;
import android.app.ICompatCameraControlCallback;
import android.app.IRequestFinishCallback;
import android.app.PictureInPictureParams;
import android.content.ComponentName;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Bundle;
import android.os.IBinder;
import android.os.IRemoteCallback;
import android.os.PersistableBundle;
import android.os.RemoteException;
import android.util.Log;
import android.view.RemoteAnimationDefinition;
import android.window.SizeConfigurationBuckets;

import com.android.internal.policy.IKeyguardDismissCallback;

public final class ActivityClientControllerAdapter extends IActivityClientController.Stub {

    private static final String TAG = "OH_ACCAdapter";

    private static volatile ActivityClientControllerAdapter sInstance;

    public static ActivityClientControllerAdapter getInstance() {
        if (sInstance == null) {
            synchronized (ActivityClientControllerAdapter.class) {
                if (sInstance == null) {
                    sInstance = new ActivityClientControllerAdapter();
                }
            }
        }
        return sInstance;
    }

    public ActivityClientControllerAdapter() {
        Log.i(TAG, "ActivityClientControllerAdapter created (extends Stub)");
    }

    // ====================================================================
    // Lifecycle reporters (oneway) — informational; OH AbilityScheduler
    // drives lifecycle directly so Android-side reports are no-op.  [STUB]
    // ====================================================================

    // ----- G2.1 (2026-04-30): OH AbilityTransitionDone reverse callback -----
    // OH AMS expects the App to call AbilityTransitionDone(token, ohState,...)
    // after each lifecycle transition.  Without it, AMS triggers
    // LIFECYCLE_HALF_TIMEOUT (~1s) and kills the process.  We hook the
    // Android-side lifecycle reporters and forward the equivalent OH state.
    //
    // OH AbilityState enum (foundation/ability/.../ability_state.h):
    //   INITIAL=0  INACTIVE=1  ACTIVE=2  INACTIVATING=5  ACTIVATING=6
    //   TERMINATING=8  FOREGROUND=9  BACKGROUND=10
    //   FOREGROUNDING=11  BACKGROUNDING=12
    //
    // NOTE: these are the INTERNAL AMS state values. AbilityTransitionDone IPC
    // expects AbilityLifeCycleState (different enum). The translation is done
    // in nativeAbilityTransitionDone (JNI side) — see oh_ability_manager_client.cpp.
    private static final int OH_STATE_FOREGROUND   = 9;
    private static final int OH_STATE_BACKGROUND   = 10;
    private static final int OH_STATE_INACTIVE     = 1;

    private static native int nativeAbilityTransitionDone(long ohTokenAddr, int ohState);

    // G2.14ah reportOnCreateDone(...) REMOVED 2026-05-09 (R-fix root cause).
    // See AppSchedulerBridge.nativeOnScheduleLaunchAbility for full rationale.
    // Short version: it crashed main thread (SIGSEGV at 0x86c in sptr ctor),
    // and the reverse-notify it tried to provide was unnecessary — OH AMS drives
    // INACTIVE itself, and FOREGROUND is reverse-notified via activityResumed →
    // reportOhLifecycle below.

    // W9 (2026-05-18): INACTIVE heartbeat hook from Application.ActivityLifecycleCallbacks.onActivityStarted.
    // AOSP fires NO IPC between onCreate/onStart and onResume — investigation confirmed (see
    // doc §1.5 + §4.1.4 W9). Adapter therefore registers onActivityStarted via
    // Application.registerActivityLifecycleCallbacks (called once from
    // AppSchedulerBridge.ensureBindApplication after handleBindApplication returns) and forwards
    // AbilityTransitionDone(token, INACTIVE=1) to OH AMS at that point — semantic match for
    // OH "INACTIVE = onCreate completed, before onForeground" per §2.1.
    // Package-visible static wrapper so LifecycleAdapter (same package) can invoke it.
    static void reportInactiveHeartbeatForToken(IBinder token) {
        if (token == null) {
            Log.w(TAG, "W9 reportInactiveHeartbeatForToken: null token");
            return;
        }
        try {
            Long ohAddr = adapter.core.OhTokenRegistry.findOhToken(token);
            if (ohAddr != null && ohAddr != 0L) {
                int rc = nativeAbilityTransitionDone(ohAddr, OH_STATE_INACTIVE);
                Log.i(TAG, "W9 onActivityStarted: OH AbilityTransitionDone(token=0x"
                        + Long.toHexString(ohAddr) + ", state=INACTIVE=1) rc=" + rc);
            } else {
                Log.w(TAG, "W9 onActivityStarted: no OH token mapping for " + token);
            }
        } catch (Throwable t) {
            Log.e(TAG, "W9 onActivityStarted INACTIVE heartbeat failed", t);
        }
    }

    private void reportOhLifecycle(IBinder token, int ohState, String tag) {
        // G2.14f — direct BCP call. OhTokenRegistry was moved to BCP-resident
        // adapter.core.OhTokenRegistry (G2.14b token bridge work). The old
        // reflection path tried adapter.activity.AppSchedulerBridge$OhTokenRegistry
        // which is in oh-adapter-runtime.jar (PathClassLoader, non-BCP) — BCP
        // code can't see it via Class.forName even with ContextClassLoader,
        // because system_server-style threads here have BCP-only context CL.
        // The shim AppSchedulerBridge.OhTokenRegistry now delegates to
        // adapter.core.OhTokenRegistry, so we cut out the indirection entirely.
        try {
            Long ohAddr = adapter.core.OhTokenRegistry.findOhToken(token);
            if (ohAddr != null && ohAddr != 0L) {
                int rc = nativeAbilityTransitionDone(ohAddr, ohState);
                Log.i(TAG, tag + ": OH AbilityTransitionDone(token=0x"
                        + Long.toHexString(ohAddr) + ", state=" + ohState + ") rc=" + rc);
            } else {
                Log.w(TAG, tag + ": no OH token mapping for " + token);
            }
        } catch (Throwable t) {
            Log.e(TAG, tag + " reverse callback failed", t);
        }
    }

    @Override
    public void activityIdle(IBinder token, Configuration config, boolean stopProfiling) { /* no-op */ }

    // ====================================================================
    // Bug B first-frame gate (G2.14bk, 2026-05-13)
    // See doc/Ability_Lifecycle_Manangent_design.html Ch4 for design.
    //
    // AOSP `activityResumed` fires when Activity.onResume() returns, which
    // means "can accept input (rendering still async)".  OH AbilityState.
    // FOREGROUND semantically means "first frame composed".  Reporting FG at
    // the AOSP signal is semantically too early — OH AMS prematurely advances
    // to "rendered" state and the starting-window cancel chain
    // (IsStartingWindow + GetAbilityState + GetScheduler) misbehaves, leaving
    // leashWindow + startingWindow residual on the desktop after press-home
    // (the "Bug B" white-frame leak; see project_bugb_leashwindow_leak_deferred
    // memory).
    //
    // Fix: defer the OH_STATE_FOREGROUND report until the first hwui frame
    // actually draws.  Hook ViewTreeObserver.OnDrawListener on the Activity's
    // DecorView; on first onDraw → report FOREGROUND + remove listener.
    // Fallback 800ms timeout (< 1s OH AMS LIFECYCLE_HALF_TIMEOUT) ensures we
    // don't get stuck if the first frame never draws (e.g., empty Activity,
    // RenderThread death).
    // ====================================================================

    /** Per-token first-frame state. true = already reported FG, suppress dup. */
    private static final java.util.WeakHashMap<IBinder, Boolean> sFgReported =
            new java.util.WeakHashMap<>();

    @Override
    public void activityResumed(IBinder token, boolean handleSplashScreenExit) {
        if (token != null) taskPosition(token);
        // Android Activity.onResume done → defer OH AbilityState.FOREGROUND
        // report until first hwui frame draws (Bug B fix).
        if (token == null) {
            Log.w(TAG, "activityResumed: null token");
            return;
        }

        // Re-resume scenario (token already saw first frame in a prior cycle):
        // OH AMS is already past first-launch state machine; reporting FG
        // immediately is safe + needed to satisfy LIFECYCLE_HALF_TIMEOUT.
        synchronized (sFgReported) {
            if (Boolean.TRUE.equals(sFgReported.get(token))) {
                reportOhLifecycle(token, OH_STATE_FOREGROUND, "activityResumed (re-resume)");
                return;
            }
        }

        final IBinder fToken = token;
        final boolean[] fReported = new boolean[]{ false };
        final boolean[] fFrameLogged = new boolean[]{ false };
        final long resumedAt = android.os.SystemClock.uptimeMillis();
        // The reason names the path that fired: a frame drawn, or the timeout with none drawn yet.
        // Start-up times read the first; an app that holds its draws (a pre-draw listener that
        // returns false) only ever logs the second.
        final class Reporter implements Runnable {
            private final String reason;
            private final boolean frame;
            Reporter(String reason, boolean frame) { this.reason = reason; this.frame = frame; }
            @Override public void run() {
                boolean report;
                synchronized (sFgReported) {
                    if (!fReported[0]) {
                        fReported[0] = true;
                        sFgReported.put(fToken, Boolean.TRUE);
                        report = true;
                    } else if (frame && !fFrameLogged[0]) {
                        // The timeout reported first. Log the first frame anyway: start-up times
                        // read it, and without it an app that draws after the timeout reads as one
                        // that never drew (YouTube Music drew about 3 s after resuming, after the
                        // 800 ms timeout had run).
                        report = false;
                    } else {
                        return;
                    }
                    if (frame) fFrameLogged[0] = true;
                }
                if (report) {
                    reportOhLifecycle(fToken, OH_STATE_FOREGROUND, reason);
                } else {
                    Log.i(TAG, reason + " after the timeout, "
                            + (android.os.SystemClock.uptimeMillis() - resumedAt)
                            + " ms after resume: FOREGROUND already reported");
                }
            }
        }
        final Runnable reporter = new Reporter("activityResumed (first-frame)", true);

        // Try to attach OnDrawListener on the Activity's DecorView.  Need to
        // reach Activity from token via ActivityThread.mActivities reflection.
        boolean listenerAttached = false;
        try {
            android.app.ActivityThread at = android.app.ActivityThread.currentActivityThread();
            if (at != null) {
                java.lang.reflect.Field activitiesField =
                        android.app.ActivityThread.class.getDeclaredField("mActivities");
                activitiesField.setAccessible(true);
                Object activities = activitiesField.get(at);  // ArrayMap<IBinder, ActivityClientRecord>
                java.lang.reflect.Method getMethod =
                        activities.getClass().getMethod("get", Object.class);
                Object record = getMethod.invoke(activities, token);
                if (record != null) {
                    java.lang.reflect.Field activityField =
                            record.getClass().getDeclaredField("activity");
                    activityField.setAccessible(true);
                    final android.app.Activity activity =
                            (android.app.Activity) activityField.get(record);
                    if (activity != null && activity.getWindow() != null) {
                        final android.view.View decor = activity.getWindow().getDecorView();
                        if (decor != null) {
                            final android.view.ViewTreeObserver.OnDrawListener[] dl =
                                    new android.view.ViewTreeObserver.OnDrawListener[1];
                            dl[0] = new android.view.ViewTreeObserver.OnDrawListener() {
                                @Override public void onDraw() {
                                    // OnDrawListener forbids self-removal inside onDraw();
                                    // post to main looper to detach.
                                    decor.post(new Runnable() {
                                        @Override public void run() {
                                            try {
                                                decor.getViewTreeObserver().removeOnDrawListener(dl[0]);
                                            } catch (Throwable ignored) { /* listener may already be gone */ }
                                        }
                                    });
                                    reporter.run();
                                }
                            };
                            // ViewTreeObserver mutators must be called on UI thread.
                            decor.post(new Runnable() {
                                @Override public void run() {
                                    try {
                                        decor.getViewTreeObserver().addOnDrawListener(dl[0]);
                                    } catch (Throwable t) {
                                        Log.e(TAG, "activityResumed: addOnDrawListener failed", t);
                                    }
                                }
                            });
                            listenerAttached = true;
                            Log.i(TAG, "activityResumed: OnDrawListener attached, FG deferred to first frame (token=" + token + ")");
                        }
                    }
                }
            }
        } catch (Throwable t) {
            Log.e(TAG, "activityResumed: OnDrawListener setup failed, falling back to 800ms timeout", t);
        }

        // Fallback timeout — fire reporter if first frame doesn't arrive in 800ms.
        // 800ms < OH AMS LIFECYCLE_HALF_TIMEOUT (1s) — keeps us safe from kill.
        new android.os.Handler(android.os.Looper.getMainLooper())
                .postDelayed(new Reporter("activityResumed (first-frame timeout)", false), 800);

        if (!listenerAttached) {
            Log.w(TAG, "activityResumed: no OnDrawListener attached, relying on 800ms timeout for FG report");
        }
    }

    @Override
    public void activityRefreshed(IBinder token) { /* no-op */ }

    @Override
    public void activityTopResumedStateLost() { /* no-op */ }

    @Override
    public void activityPaused(IBinder token) {
        if (token != null) taskPosition(token);
        // Android Activity.onPause done → OH AbilityState.INACTIVE (active→inactive transition).
        reportOhLifecycle(token, OH_STATE_INACTIVE, "activityPaused");
    }

    @Override
    public void activityStopped(IBinder token, Bundle state, PersistableBundle persistentState,
            CharSequence description) {
        // Android Activity.onStop done → OH AbilityState.BACKGROUND.
        reportOhLifecycle(token, OH_STATE_BACKGROUND, "activityStopped");
    }

    @Override
    public void activityDestroyed(IBinder token) {
        ActivityResults.destroyed(token);
        // finishActivity already drove OH TerminateAbility; only the task model needs the news.
        leaveTask(token);
    }

    @Override
    public void activityLocalRelaunch(IBinder token) { /* no-op */ }

    @Override
    public void activityRelaunched(IBinder token) { /* no-op */ }

    @Override
    public void reportSizeConfigurations(IBinder token, SizeConfigurationBuckets sizeConfigurations) { /* no-op */ }

    // ====================================================================
    // Task / activity control — return safe defaults.  [STUB]
    // ====================================================================

    @Override
    public boolean moveActivityTaskToBack(IBinder token, boolean nonRoot) {
        // ADR-0008: OH holds a managed app's task; SceneBoard moves its ability to the background. A direct launch
        // has no task to move.
        if (!"1".equals(System.getenv("WESTLAKE_MANAGED_LAUNCH"))) return false;
        if (!nonRoot && taskPosition(token) != 0) return false;
        return nativeMoveManagedTaskToBack() == 0;
    }

    @Override
    public boolean shouldUpRecreateTask(IBinder token, String destAffinity) { return false; }

    @Override
    public boolean navigateUpTo(IBinder token, Intent target, String resolvedType,
            int resultCode, Intent resultData) { return false; }

    @Override
    public boolean releaseActivityInstance(IBinder token) { return false; }

    /**
     * B.48 (2026-04-30 §1.2.4.3 P2 reverse): App finish() reverse callback.
     * Look up OH IRemoteObject token addr from OhTokenRegistry (set at SLA time),
     * then call OH AbilityMS::TerminateAbility via JNI bridge.
     * Returns true (App side expects success) regardless of OH result — OH-side
     * failure should not unwind App's onDestroy chain.
     */
    private static native int nativeTerminateAbilityByTokenAddr(long ohTokenAddr, int resultCode);

    @Override
    public boolean finishActivity(IBinder token, int code, Intent data, int finishTask) {
        // A start for result: the caller hears the result (ActivityResults), as from ATMS.
        ActivityResults.deliver(token, code, data);
        leaveTask(token);
        try {
            // G2.14f — direct BCP call (see reportOhLifecycle for rationale).
            Long ohAddr = adapter.core.OhTokenRegistry.findOhToken(token);
            if (ohAddr != null && ohAddr != 0L && "1".equals(System.getenv("WESTLAKE_MANAGED_LAUNCH"))) {
                // ADR-0008: one OH ability hosts the app's task. When its activity with the OH token finishes
                // while others remain (a splash that started the main activity), the ability stays, its token
                // passes to the task's top activity, and this one is destroyed here, as ATMS would; the last
                // activity's finish ends the ability. Decided on the main thread after what is queued there:
                // a splash calls startActivity and then finish(), and the activity it started does not exist yet.
                final long addr = ohAddr;
                final int resultCode = code;
                final IBinder finishing = token;
                adapter.core.OhTokenRegistry.holderFinishing(finishing);
                final android.os.Handler main = new android.os.Handler(android.os.Looper.getMainLooper());
                main.post(new Runnable() {
                    private int waits;

                    @Override public void run() {
                        if (adapter.core.OhTokenRegistry.findOhToken(finishing) == null) {
                            // The activity it started took the token with its window (a splash that never drew:
                            // WindowSessionAdapter.takeTokenFromSplash).
                            adapter.core.OhTokenRegistry.finishingDone(finishing);
                            System.err.println("[WL-MANAGED] the OH ability went with the next activity's window");
                            scheduleLocalDestroy(finishing);
                            return;
                        }
                        IBinder heir = taskHeir(finishing);
                        if (heir == null && ActivityTaskManagerAdapter.ownStartQueued() && waits++ < 20) {
                            // The app started one of its activities after finish() (GitHub's MainActivity finishes
                            // and then starts its sign-in). Android adds it to the task within startActivity, and the
                            // task goes on; here its start is queued on this looper behind this decision. Decide
                            // after it: the activity it creates is the heir, or its window takes the token.
                            main.post(this);
                            return;
                        }
                        adapter.core.OhTokenRegistry.finishingDone(finishing);
                        if (waits > 0) {
                            System.err.println("[WL-MANAGED] the finished activity's ability waited for "
                                    + waits + " queued start(s) of the app's activities");
                        }
                        if (heir != null && adapter.core.OhTokenRegistry.transfer(finishing, heir)) {
                            Log.i(TAG, "finishActivity: OH ability 0x" + Long.toHexString(addr)
                                    + " stays with the task's top " + heir);
                            System.err.println("[WL-MANAGED] the OH ability stays with the task's next activity");
                            scheduleLocalDestroy(finishing);
                        } else if (AppSchedulerBridge.returnSpareAbility(addr)) {
                            System.err.println("[WL-MANAGED] the windowless host's last activity finished: its OH"
                                    + " ability stays, unused, so that OH keeps the process");
                            scheduleLocalDestroy(finishing);
                        } else {
                            int rc = nativeTerminateAbilityByTokenAddr(addr, resultCode);
                            System.err.println("[WL-MANAGED] the task's last activity finished: OH ability ends, rc=" + rc);
                        }
                    }
                });
            } else if (ohAddr != null && ohAddr != 0L) {
                int rc = nativeTerminateAbilityByTokenAddr(ohAddr, code);
                Log.i(TAG, "finishActivity: OH TerminateAbility(token=0x"
                        + Long.toHexString(ohAddr) + ", code=" + code + ") rc=" + rc);
            } else {
                // Direct launch: no OH ability stands behind this activity, so nothing on the OH
                // side will destroy it, and its window would stay on screen. Do what ATMS does
                // for a finishing activity: send it a destroy transaction (the executor walks it
                // through pause and stop first). Posted, so finish() returns first, as on Android.
                scheduleLocalDestroy(token);
            }
        } catch (Throwable t) {
            Log.e(TAG, "finishActivity reverse callback failed", t);
        }
        return true;
    }

    /**
     * Another live activity of the app's task to carry its OH ability: the task's top, else any other; never one that
     * is finishing or destroyed. Tantan's SplashAct finished during its own launch, while SplashProxyAct still held the
     * token (so its finish only scheduled a local destroy), and taskPosition() puts any activity it is asked about back
     * in the task list: SplashProxyAct's token went to SplashAct, was destroyed with it, and WelcomeAct's window waited
     * for a main window that never came.
     */
    private static IBinder taskHeir(IBinder finishing) {
        android.app.ActivityThread thread = android.app.ActivityThread.currentActivityThread();
        java.util.List<IBinder> remaining = taskTokens();
        for (int i = remaining.size() - 1; i >= 0; i--) {
            IBinder candidate = remaining.get(i);
            if (candidate != finishing && carriesTask(thread, candidate)) return candidate;
        }
        try {
            java.lang.reflect.Field field = android.app.ActivityThread.class.getDeclaredField("mActivities");
            field.setAccessible(true);
            java.util.Map<?, ?> activities = (java.util.Map<?, ?>) field.get(thread);
            for (Object key : activities.keySet().toArray()) {
                IBinder candidate = (IBinder) key;
                if (candidate != finishing && carriesTask(thread, candidate)) return candidate;
            }
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.w(TAG, "taskHeir: " + e);
        }
        return null;
    }

    /** An activity that can carry the task's OH ability: created, and neither finishing nor destroyed. */
    private static boolean carriesTask(android.app.ActivityThread thread, IBinder token) {
        android.app.Activity activity = thread != null ? thread.getActivity(token) : null;
        return activity != null && !activity.isFinishing() && !activity.isDestroyed();
    }

    private static void scheduleLocalDestroy(final IBinder token) {
        new android.os.Handler(android.os.Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                try {
                    android.app.ActivityThread thread = android.app.ActivityThread.currentActivityThread();
                    if (thread == null || thread.getActivity(token) == null) return;
                    android.app.IApplicationThread app = thread.getApplicationThread();
                    android.app.servertransaction.ClientTransaction transaction =
                            android.app.servertransaction.ClientTransaction.obtain(app);
                    transaction.addTransactionItem(
                            android.app.servertransaction.DestroyActivityItem.obtain(token, true));
                    app.scheduleTransaction(transaction);
                    Log.i(TAG, "finishActivity: no OH ability; local destroy scheduled for " + token);
                } catch (Throwable t) {
                    Log.e(TAG, "finishActivity: local destroy failed", t);
                }
            }
        });
    }

    /**
     * Activity.finishAffinity(): this activity and the activities below it in its task that share its task affinity,
     * as ATMS finishes them (the ones below first; it stops at the first one with another affinity). This answered
     * true and finished nothing: Activity then marked itself finished, and the activity stayed, holding its OH
     * ability's token in a managed launch. WhatsApp's first activity starts its EULA and calls finishAffinity(); the
     * EULA's window waited for an OH main window that never came. Each one goes through finishActivity, so the OH
     * ability passes to the task's next activity as when an activity calls finish().
     */
    @Override
    public boolean finishActivityAffinity(IBinder token) {
        if (token == null) return false;
        String affinity = taskAffinity(token);
        java.util.List<IBinder> task = taskTokens();
        for (int i = task.indexOf(token) - 1; i >= 0; i--) {
            IBinder below = task.get(i);
            if (!java.util.Objects.equals(affinity, taskAffinity(below))) break;
            finishActivity(below, android.app.Activity.RESULT_CANCELED, null,
                    0 /* Activity.DONT_FINISH_TASK_WITH_ACTIVITY */);
        }
        return finishActivity(token, android.app.Activity.RESULT_CANCELED, null,
                0 /* Activity.DONT_FINISH_TASK_WITH_ACTIVITY */);
    }

    /** The task affinity of the activity whose token this is (its ActivityInfo's), or null. */
    private static String taskAffinity(IBinder token) {
        try {
            android.app.ActivityThread thread = android.app.ActivityThread.currentActivityThread();
            android.app.Activity activity = thread == null ? null : thread.getActivity(token);
            if (activity == null) return null;
            java.lang.reflect.Field info = android.app.Activity.class.getDeclaredField("mActivityInfo");
            info.setAccessible(true);
            android.content.pm.ActivityInfo activityInfo = (android.content.pm.ActivityInfo) info.get(activity);
            return activityInfo == null ? null : activityInfo.taskAffinity;
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.w(TAG, "taskAffinity: " + e);
            return null;
        }
    }

    @Override
    public void finishSubActivity(IBinder token, String resultWho, int requestCode) { /* no-op */ }

    @Override
    public void setForceSendResultForMediaProjection(IBinder token) { /* no-op */ }

    // ====================================================================
    // Activity state queries.  [STUB]
    // ====================================================================

    // The app's one task: its activity tokens in the order this process first asked about or
    // reported them, the oldest still alive its root. getTaskForActivity answered -1 for every
    // activity, so Activity.isTaskRoot() was false and getTaskId() -1: Instagram's launcher activity
    // logged "InstagramMainActivity is not the root. Finishing activity instead of launching." and
    // finished as soon as it resumed. An activity first asks about itself in onCreate, before any
    // activity it starts exists, so first-seen order is creation order.
    private static final int TASK_ID = 1;
    private static final java.util.ArrayList<java.lang.ref.WeakReference<IBinder>> sTask =
            new java.util.ArrayList<>();

    /** Records the token if new; returns its position in the task (0 = root). */
    private static synchronized int taskPosition(IBinder token) {
        int position = 0;
        for (java.util.Iterator<java.lang.ref.WeakReference<IBinder>> it = sTask.iterator(); it.hasNext(); ) {
            IBinder known = it.next().get();
            if (known == null) {
                it.remove();
            } else if (known == token) {
                return position;
            } else {
                position++;
            }
        }
        sTask.add(new java.lang.ref.WeakReference<>(token));
        return position;
    }

    private static synchronized boolean isTaskTop(IBinder token) {
        taskPosition(token);
        for (int i = sTask.size() - 1; i >= 0; i--) {
            IBinder known = sTask.get(i).get();
            if (known != null) return known == token;
        }
        return true;
    }

    private static synchronized void leaveTask(IBinder token) {
        sTask.removeIf(ref -> ref.get() == null || ref.get() == token);
    }

    /** The task's live activity tokens, root first. */
    public static synchronized java.util.List<IBinder> taskTokens() {
        java.util.ArrayList<IBinder> live = new java.util.ArrayList<>(sTask.size());
        for (java.lang.ref.WeakReference<IBinder> ref : sTask) {
            IBinder token = ref.get();
            if (token != null) live.add(token);
        }
        return live;
    }

    /** The task's id, as getTaskForActivity answers it. */
    public static int taskId() { return TASK_ID; }

    @Override
    public boolean isTopOfTask(IBinder token) { return token == null || isTaskTop(token); }

    @Override
    public boolean willActivityBeVisible(IBinder token) { return true; }

    @Override
    public int getDisplayId(IBinder activityToken) { return 0; }   // OH default display

    @Override
    public int getTaskForActivity(IBinder token, boolean onlyRoot) {
        if (token == null) return -1;
        int position = taskPosition(token);
        return !onlyRoot || position == 0 ? TASK_ID : -1;
    }

    @Override
    public Configuration getTaskConfiguration(IBinder activityToken) { return null; }

    @Override
    public IBinder getActivityTokenBelow(IBinder token) { return null; }

    @Override
    public ComponentName getCallingActivity(IBinder token) { return ActivityResults.callingActivity(token); }

    @Override
    public String getCallingPackage(IBinder token) {
        ComponentName caller = ActivityResults.callingActivity(token);
        return caller == null ? null : caller.getPackageName();
    }

    @Override
    public int getLaunchedFromUid(IBinder token) { return ActivityResults.launchedFromUid(token); }

    @Override
    public String getLaunchedFromPackage(IBinder token) { return ActivityResults.launchedFromPackage(token); }

    // ====================================================================
    // Orientation.  [STUB]
    // ====================================================================

    @Override
    public void setRequestedOrientation(IBinder token, int requestedOrientation) { /* no-op */ }

    @Override
    public int getRequestedOrientation(IBinder token) { return -1; /* SCREEN_ORIENTATION_UNSPECIFIED */ }

    // ====================================================================
    // Translucency / immersive.  [STUB]
    // ====================================================================

    @Override
    public boolean convertFromTranslucent(IBinder token) { return false; }

    @Override
    public boolean convertToTranslucent(IBinder token, Bundle options) { return false; }

    @Override
    public boolean isImmersive(IBinder token) { return false; }

    @Override
    public void setImmersive(IBinder token, boolean immersive) { /* no-op */ }

    // ====================================================================
    // Picture-in-picture / multiwindow.  [STUB]
    // ====================================================================

    @Override
    public boolean enterPictureInPictureMode(IBinder token, PictureInPictureParams params) { return false; }

    @Override
    public void setPictureInPictureParams(IBinder token, PictureInPictureParams params) { /* no-op */ }

    @Override
    public void setShouldDockBigOverlays(IBinder token, boolean shouldDockBigOverlays) { /* no-op */ }

    @Override
    public void toggleFreeformWindowingMode(IBinder token) { /* no-op */ }

    @Override
    public void requestMultiwindowFullscreen(IBinder token, int request, IRemoteCallback callback) { /* no-op */ }

    // ====================================================================
    // Lock task / assist / voice interaction.  [STUB]
    // ====================================================================

    @Override
    public void startLockTaskModeByToken(IBinder token) { /* no-op */ }

    @Override
    public void stopLockTaskModeByToken(IBinder token) { /* no-op */ }

    @Override
    public void showLockTaskEscapeMessage(IBinder token) { /* no-op */ }

    @Override
    public void setTaskDescription(IBinder token, ActivityManager.TaskDescription values) { /* no-op */ }

    @Override
    public boolean showAssistFromActivity(IBinder token, Bundle args) { return false; }

    @Override
    public boolean isRootVoiceInteraction(IBinder token) { return false; }

    @Override
    public void startLocalVoiceInteraction(IBinder token, Bundle options) { /* no-op */ }

    @Override
    public void stopLocalVoiceInteraction(IBinder token) { /* no-op */ }

    // ====================================================================
    // Show-when-locked / turn-screen-on / draw flags.  [STUB]
    // ====================================================================

    @Override
    public void setShowWhenLocked(IBinder token, boolean showWhenLocked) { /* no-op */ }

    @Override
    public void setInheritShowWhenLocked(IBinder token, boolean setInheritShownWhenLocked) { /* no-op */ }

    @Override
    public void setTurnScreenOn(IBinder token, boolean turnScreenOn) { /* no-op */ }

    @Override
    public void setAllowCrossUidActivitySwitchFromBelow(IBinder token, boolean allowed) { /* no-op */ }

    @Override
    public void reportActivityFullyDrawn(IBinder token, boolean restoredFromBundle) { /* no-op */ }

    // ====================================================================
    // Activity transitions.  [STUB]
    // ====================================================================

    @Override
    public void overrideActivityTransition(IBinder token, boolean open, int enterAnim,
            int exitAnim, int backgroundColor) { /* no-op */ }

    @Override
    public void clearOverrideActivityTransition(IBinder token, boolean open) { /* no-op */ }

    @Override
    public void overridePendingTransition(IBinder token, String packageName,
            int enterAnim, int exitAnim, int backgroundColor) { /* no-op */ }

    @Override
    public int setVrMode(IBinder token, boolean enabled, ComponentName packageName) { return 0; }

    @Override
    public void setRecentsScreenshotEnabled(IBinder token, boolean enabled) { /* no-op */ }

    @Override
    public void invalidateHomeTaskSnapshot(IBinder homeToken) { /* no-op */ }

    // ====================================================================
    // Keyguard / remote animations.  [STUB]
    // ====================================================================

    @Override
    public void dismissKeyguard(IBinder token, IKeyguardDismissCallback callback,
            CharSequence message) { /* no-op */ }

    @Override
    public void registerRemoteAnimations(IBinder token, RemoteAnimationDefinition definition) { /* no-op */ }

    @Override
    public void unregisterRemoteAnimations(IBinder token) { /* no-op */ }

    // ====================================================================
    // Misc.  [STUB]
    // ====================================================================

    @Override
    public void onBackPressed(IBinder activityToken, IRequestFinishCallback callback) {
        // Android 12+ (ATMS): a back press the activity did not consume finishes it; at the root of a task the
        // launcher started, the task moves to the back instead. In an ADR-0008 managed launch OH holds that task
        // (OH's launcher started its ability): SceneBoard moves the ability to the background. A direct launch keeps
        // its earlier behaviour (nothing happens).
        if (!"1".equals(System.getenv("WESTLAKE_MANAGED_LAUNCH"))) return;
        if (taskPosition(activityToken) == 0) {
            int rc = nativeMoveManagedTaskToBack();
            System.err.println("[WL-BACK] back at the task's root: the app moves to the background (rc=" + rc + ")");
            return;
        }
        try {
            if (callback != null) callback.requestFinish();
            System.err.println("[WL-BACK] back press finishes the activity");
        } catch (android.os.RemoteException e) {
            Log.w(TAG, "onBackPressed: " + e);
        }
    }

    private static native int nativeMoveManagedTaskToBack();

    @Override
    public void splashScreenAttached(IBinder token) { /* no-op */ }

    @Override
    public void requestCompatCameraControl(IBinder token, boolean showControl,
            boolean transformationApplied, ICompatCameraControlCallback callback) { /* no-op */ }

    @Override
    public void enableTaskLocaleOverride(IBinder token) { /* no-op */ }

    @Override
    public boolean isRequestedToLaunchInTaskFragment(IBinder activityToken,
            IBinder taskFragmentToken) { return false; }

    // Android 15 service boundary. Unsupported OH capabilities fail explicitly.
    @Override
    public int getActivityCallerUid(android.os.IBinder activityToken, android.os.IBinder callerToken) throws android.os.RemoteException {
        return -1;
    }

    @Override
    public java.lang.String getActivityCallerPackage(android.os.IBinder activityToken, android.os.IBinder callerToken) throws android.os.RemoteException {
        return null;
    }

    @Override
    public int checkActivityCallerContentUriPermission(android.os.IBinder activityToken, android.os.IBinder callerToken, android.net.Uri uri, int modeFlags, int userId) throws android.os.RemoteException {
        return android.content.pm.PackageManager.PERMISSION_DENIED;
    }

    @Override
    public void setActivityRecordInputSinkEnabled(android.os.IBinder activityToken, boolean enabled) throws android.os.RemoteException {
        throw new UnsupportedOperationException("OpenHarmony activity input sink is not connected");
    }
}
