package adapter.core;

import android.app.ActivityThread;
import android.app.AlarmManager;
import android.app.IAlarmCompleteListener;
import android.app.IAlarmListener;
import android.app.PendingIntent;
import android.content.Intent;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.SystemClock;

import java.util.ArrayList;
import java.util.List;

/**
 * The in-process alarm service: an alarm set while the app runs is delivered at its time, as
 * AlarmManagerService delivers it. A PendingIntent alarm is sent, with the alarm count filled in; a
 * listener alarm calls its listener. A later set with the same PendingIntent or listener replaces the
 * alarm, remove cancels it, and a repeating alarm comes back every interval, counting the intervals
 * it missed. The RTC types count wall-clock time, the ELAPSED ones time since boot. An alarm clock
 * (setAlarmClock) is the next alarm clock until it fires. Chrono's timer expires on an alarm, and
 * before this its end passed in silence.
 *
 * These alarms live as long as the app's process: nothing restarts the app for one once it is
 * gone, and nothing here wakes the device. Each is delivered at its trigger time, an inexact alarm
 * as well, so the app may schedule exact alarms (canScheduleExactAlarms), as when the user has
 * allowed it alarms and reminders.
 */
final class LocalAlarms {
    private LocalAlarms() {}

    private static final String TAG = "[WL-ALARM]";
    // The longest the scheduler waits before it looks at the clocks again: its handler counts
    // uptime, which stops while the device sleeps, and the wall clock can be set.
    private static final long RECHECK_MS = 60_000;

    private static final class Alarm {
        final int type;
        long when;
        final long interval;
        final PendingIntent operation;
        final IAlarmListener listener;
        final String tag;
        final AlarmManager.AlarmClockInfo clock;

        Alarm(int type, long when, long interval, PendingIntent operation, IAlarmListener listener,
                String tag, AlarmManager.AlarmClockInfo clock) {
            this.type = type;
            this.when = when;
            this.interval = interval;
            this.operation = operation;
            this.listener = listener;
            this.tag = tag;
            this.clock = clock;
        }

        /** AlarmManagerService's Alarm.matches: the same PendingIntent, or the same listener. */
        boolean matches(PendingIntent otherOperation, IAlarmListener otherListener) {
            if (operation != null) return operation.equals(otherOperation);
            return otherListener != null && listener.asBinder() == otherListener.asBinder();
        }

        @Override
        public String toString() {
            return (operation != null ? "operation " + operation : "listener " + tag)
                    + (interval > 0 ? " every " + interval + " ms" : "")
                    + (clock != null ? " (alarm clock)" : "");
        }
    }

    private static final List<Alarm> sAlarms = new ArrayList<>();
    private static final Runnable DELIVER_DUE = LocalAlarms::deliverDue;
    private static Handler sHandler;

    static Object answer(String method, Object[] args) {
        switch (method) {
            case "set":
                // set(callingPackage, type, triggerAtTime, windowLength, interval, flags, operation,
                //     listener, listenerTag, workSource, alarmClock)
                set((Integer) args[1], (Long) args[2], (Long) args[4], (PendingIntent) args[6],
                        (IAlarmListener) args[7], (String) args[8], (AlarmManager.AlarmClockInfo) args[10]);
                return LocalServiceBinders.DEFAULT;
            case "remove":
                remove((PendingIntent) args[0], (IAlarmListener) args[1]);
                return LocalServiceBinders.DEFAULT;
            case "getNextAlarmClock":
                return nextAlarmClock();
            case "canScheduleExactAlarms":
            case "hasScheduleExactAlarm":
                return Boolean.TRUE;
            default:
                return LocalServiceBinders.DEFAULT;   // the time and zone are not set from here
        }
    }

    private static synchronized Handler handler() {
        if (sHandler == null) {
            HandlerThread thread = new HandlerThread("WL-Alarms");
            thread.start();
            sHandler = new Handler(thread.getLooper());
        }
        return sHandler;
    }

    private static long now(int type) {
        return type == AlarmManager.RTC || type == AlarmManager.RTC_WAKEUP
                ? System.currentTimeMillis() : SystemClock.elapsedRealtime();
    }

    private static void set(int type, long when, long interval, PendingIntent operation,
            IAlarmListener listener, String tag, AlarmManager.AlarmClockInfo clock) {
        if (operation == null && listener == null) return;
        Alarm alarm = new Alarm(type, when, interval, operation, listener, tag, clock);
        synchronized (sAlarms) {
            sAlarms.removeIf(old -> old.matches(operation, listener));
            sAlarms.add(alarm);
            reschedule();
        }
        System.err.println(TAG + " set " + alarm + " in " + Math.max(0, when - now(type)) + " ms");
    }

    private static void remove(PendingIntent operation, IAlarmListener listener) {
        synchronized (sAlarms) {
            if (sAlarms.removeIf(old -> old.matches(operation, listener))) reschedule();
        }
    }

    /** With sAlarms held: wakes the scheduler for the earliest alarm, or for a recheck. */
    private static void reschedule() {
        Handler handler = handler();
        handler.removeCallbacks(DELIVER_DUE);
        long delay = Long.MAX_VALUE;
        for (Alarm alarm : sAlarms) delay = Math.min(delay, Math.max(0, alarm.when - now(alarm.type)));
        if (delay != Long.MAX_VALUE) handler.postDelayed(DELIVER_DUE, Math.min(delay, RECHECK_MS));
    }

    private static final class Due {
        final Alarm alarm;
        final int count;
        final long late;

        Due(Alarm alarm, int count, long late) {
            this.alarm = alarm;
            this.count = count;
            this.late = late;
        }
    }

    /** On the scheduler's thread: delivers every alarm that is due, the earliest first. */
    private static void deliverDue() {
        List<Due> due = new ArrayList<>();
        synchronized (sAlarms) {
            for (Alarm alarm : new ArrayList<>(sAlarms)) {
                long late = now(alarm.type) - alarm.when;
                if (late < 0) continue;
                int count = 1;
                if (alarm.interval > 0) {
                    // As AlarmManagerService counts it: the intervals missed come with the delivery.
                    count += (int) (late / alarm.interval);
                    alarm.when += count * alarm.interval;
                } else {
                    sAlarms.remove(alarm);
                }
                due.add(new Due(alarm, count, late));
            }
            reschedule();
        }
        due.sort((a, b) -> Long.compare(b.late, a.late));
        for (Due each : due) deliver(each.alarm, each.count);
    }

    private static void deliver(Alarm alarm, int count) {
        try {
            if (alarm.operation != null) {
                Intent fillIn = new Intent().addFlags(Intent.FLAG_FROM_BACKGROUND)
                        .putExtra(Intent.EXTRA_ALARM_COUNT, count);
                alarm.operation.send(ActivityThread.currentApplication(), 0, fillIn);
            } else {
                alarm.listener.doAlarm(new IAlarmCompleteListener.Stub() {
                    @Override
                    public void alarmComplete(IBinder who) {
                    }
                });
            }
            System.err.println(TAG + " delivered " + alarm);
        } catch (PendingIntent.CanceledException cancelled) {
            remove(alarm.operation, null);   // as the service drops a repeating alarm whose sender is gone
            System.err.println(TAG + " dropped " + alarm + ": its PendingIntent was cancelled");
        } catch (Throwable t) {
            System.err.println(TAG + " delivery failed for " + alarm + ": " + t);
        }
    }

    private static AlarmManager.AlarmClockInfo nextAlarmClock() {
        AlarmManager.AlarmClockInfo next = null;
        synchronized (sAlarms) {
            for (Alarm alarm : sAlarms) {
                if (alarm.clock != null && (next == null || alarm.clock.getTriggerTime() < next.getTriggerTime())) {
                    next = alarm.clock;
                }
            }
        }
        return next;
    }
}
