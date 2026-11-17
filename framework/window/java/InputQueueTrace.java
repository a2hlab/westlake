package adapter.window;

import android.os.Looper;
import android.os.Message;
import android.os.MessageQueue;
import android.os.SystemClock;
import java.lang.reflect.Field;
import java.util.concurrent.atomic.AtomicInteger;

/** Bounded, opt-in diagnostics; never inspect message payloads or view contents. */
final class InputQueueTrace {
    private static final AtomicInteger SAMPLES = new AtomicInteger();
    private static final AtomicInteger RELEASES = new AtomicInteger();
    private static final AtomicInteger PRESSES = new AtomicInteger();
    private static final java.util.concurrent.atomic.AtomicBoolean METHOD_TRACE =
            new java.util.concurrent.atomic.AtomicBoolean();

    private InputQueueTrace() {}

    static void beforePress(int traceId, android.view.View root, android.view.MotionEvent event) {
        if (!enabled(traceId)) return;
        int press = PRESSES.incrementAndGet();
        if (press <= 8) {
            try {
                // getGlobalVisibleRect is relative to the root view, not the display.
                float x = event.getX(), y = event.getY();
                System.err.println("[WL-INPUT-HIT] id=" + traceId + " action=" + event.getActionMasked()
                        + " pointers=" + event.getPointerCount() + " source=" + event.getSource());
                walk(traceId, root, x, y, 0, new int[1]);
            } catch (RuntimeException unavailable) {
                System.err.println("[WL-INPUT-HIT] id=" + traceId + " unavailable=true");
            }
        }
        if (Integer.toString(press).equals(System.getenv("WESTLAKE_INPUT_METHOD_TRACE_PRESS")))
            startMethodTrace(traceId);
    }

    /** Structural hit candidates only. Does not dispatch, invoke listeners or read text. */
    private static void walk(int id, android.view.View view, float x, float y, int depth, int[] count) {
        if (view == null || depth > 32 || count[0] >= 256 || view.getVisibility() != android.view.View.VISIBLE) return;
        count[0]++;
        android.graphics.Rect bounds = new android.graphics.Rect();
        boolean visible = view.getGlobalVisibleRect(bounds);
        if (visible && bounds.contains((int) x, (int) y)) {
            System.err.println("[WL-INPUT-HIT] id=" + id + " depth=" + depth + " class=" + type(view)
                    + " view_id=" + Integer.toHexString(view.getId())
                    + " enabled=" + view.isEnabled() + " clickable=" + view.isClickable()
                    + " listener=" + view.hasOnClickListeners()
                    + " pressed=" + view.isPressed() + " matrix_identity=" + view.hasIdentityMatrix()
                    + " width=" + view.getWidth() + " height=" + view.getHeight());
        }
        if (view instanceof android.view.ViewGroup) {
            android.view.ViewGroup group = (android.view.ViewGroup) view;
            for (int i = 0; i < group.getChildCount() && count[0] < 256; i++)
                walk(id, group.getChildAt(i), x, y, depth + 1, count);
        }
    }

    static void beforeRelease(int traceId) {
        if (!enabled(traceId)) return;
        int release = RELEASES.incrementAndGet();
        // One explicitly selected UP event per process; ART tracing perturbs timing.
        if (!Integer.toString(release).equals(System.getenv("WESTLAKE_INPUT_METHOD_TRACE_RELEASE"))) return;
        startMethodTrace(traceId);
    }

    private static void startMethodTrace(int traceId) {
        if (!METHOD_TRACE.compareAndSet(false, true)) return;
        try {
            java.io.File directory = new java.io.File(
                    android.app.ActivityThread.currentApplication().getFilesDir(), "westlake-input-diagnostic");
            if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException();
            android.system.Os.chmod(directory.getPath(), 0700);
            String name = "release-" + SystemClock.uptimeMillis() + ".trace";
            java.io.File file = new java.io.File(directory, name);
            if (file.exists()) throw new IllegalStateException();
            android.os.Debug.startMethodTracing(file.getPath(), 64 * 1024 * 1024, 0);
            System.err.println("[WL-INPUT-QUEUE] id=" + traceId + " method_trace_started=true file=" + name);
            Thread stopper = new Thread(() -> {
                try { Thread.sleep(3_000); }
                catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); }
                try {
                    android.os.Debug.stopMethodTracing();
                    android.system.Os.chmod(file.getPath(), 0600);
                    System.err.println("[WL-INPUT-QUEUE] id=" + traceId + " method_trace_written=true file=" + name);
                } catch (Exception unavailable) {
                    System.err.println("[WL-INPUT-QUEUE] id=" + traceId + " method_trace_stop_failed=true");
                }
            }, "input-method-diagnostic");
            stopper.setDaemon(true);
            stopper.start();
        } catch (Exception unavailable) {
            System.err.println("[WL-INPUT-QUEUE] id=" + traceId + " method_trace_unavailable=true");
        }
    }

    static void afterRelease(int traceId) {
        if (!enabled(traceId) || SAMPLES.incrementAndGet() > 8) return;
        snapshot(traceId, "immediate");
        Thread reader = new Thread(() -> {
            try {
                Thread.sleep(2_000);
                snapshot(traceId, "two_seconds");
                Thread.sleep(3_000);
                snapshot(traceId, "five_seconds");
            } catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); }
        }, "input-queue-diagnostic");
        reader.setDaemon(true);
        reader.start();
    }

    private static boolean enabled(int traceId) {
        return traceId != 0 && "1".equals(System.getenv("WESTLAKE_INPUT_QUEUE_TRACE"))
                && "1".equals(System.getenv("WESTLAKE_GOOGLE_PRIVATE_LOGS"));
    }

    private static String type(Object value) {
        if (value == null) return "none";
        String name = value.getClass().getName();
        return name.length() <= 200 && name.matches("[A-Za-z0-9_.$]+") ? name : "other";
    }

    private static void snapshot(int traceId, String phase) {
        try {
            Looper main = Looper.getMainLooper();
            if (main == null) return;
            MessageQueue queue = main.getQueue();
            Field messages = MessageQueue.class.getDeclaredField("mMessages");
            Field next = Message.class.getDeclaredField("next");
            messages.setAccessible(true);
            next.setAccessible(true);
            String prefix = "[WL-INPUT-QUEUE] id=" + traceId + " phase=" + phase;
            StringBuilder output = new StringBuilder(prefix);
            long now = SystemClock.uptimeMillis();
            synchronized (queue) {
                Message message = (Message) messages.get(queue);
                output.append(" head_barrier=").append(message != null && message.getTarget() == null);
                int count = 0;
                for (; message != null && count < 32; count++, message = (Message) next.get(message)) {
                    output.append('\n').append(prefix).append(" index=").append(count)
                            .append(" target=").append(type(message.getTarget()))
                            .append(" callback=").append(type(message.getCallback()))
                            .append(" what=").append(message.what)
                            .append(" async=").append(message.isAsynchronous())
                            .append(" overdue_ms=").append(now - message.getWhen());
                }
                output.append('\n').append(prefix).append(" sampled=").append(count)
                        .append(" truncated=").append(message != null);
            }
            System.err.println(output);
        } catch (ReflectiveOperationException | RuntimeException unavailable) {
            System.err.println("[WL-INPUT-QUEUE] id=" + traceId + " unavailable=true");
        }
    }
}
