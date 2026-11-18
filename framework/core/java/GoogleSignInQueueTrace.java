package adapter.core;

import android.os.Looper;
import android.os.Message;
import android.os.MessageQueue;
import android.os.SystemClock;
import java.lang.reflect.Field;
import java.util.concurrent.atomic.AtomicInteger;

/** Opt-in read-only diagnosis of a callback received without a subsequent UI transition. */
final class GoogleSignInQueueTrace {
    private static final AtomicInteger SAMPLES = new AtomicInteger();
    private static final java.util.concurrent.atomic.AtomicBoolean METHOD_TRACE =
            new java.util.concurrent.atomic.AtomicBoolean();

    private GoogleSignInQueueTrace() {}

    static void schedule(String descriptor, int code, Object target) {
        if (!"1".equals(System.getenv("WESTLAKE_GOOGLE_SIGNIN_QUEUE_TRACE"))
                || !"1".equals(System.getenv("WESTLAKE_GOOGLE_PRIVATE_LOGS"))
                || SAMPLES.incrementAndGet() > 8) return;
        System.err.println("[WL-SIGNIN-QUEUE] callback_target=" + type(target) + " code=" + code);
        if (code == 101 && "com.google.android.gms.auth.api.signin.internal.ISignInCallbacks".equals(descriptor))
            startMethodTrace();
        Thread reader = new Thread(() -> {
            try {
                Thread.sleep(2_000);
                snapshot();
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
            } catch (ReflectiveOperationException | RuntimeException unavailable) {
                System.err.println("[WL-SIGNIN-QUEUE] unavailable=true");
            }
        }, "signin-queue-diagnostic");
        reader.setDaemon(true);
        reader.start();
    }

    /** Short ART instrumentation trace for callback control flow; opt-in and timing-perturbing. */
    private static void startMethodTrace() {
        if (!"1".equals(System.getenv("WESTLAKE_GOOGLE_SIGNIN_METHOD_TRACE"))
                || !METHOD_TRACE.compareAndSet(false, true)) return;
        try {
            java.io.File directory = new java.io.File(
                    android.app.ActivityThread.currentApplication().getFilesDir(), "westlake-signin-diagnostic");
            if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException();
            android.system.Os.chmod(directory.getPath(), 0700);
            String name = "callback-" + SystemClock.uptimeMillis() + ".trace";
            java.io.File file = new java.io.File(directory, name);
            if (file.exists()) throw new IllegalStateException();
            android.os.Debug.startMethodTracing(file.getPath(), 64 * 1024 * 1024, 0);
            System.err.println("[WL-SIGNIN-QUEUE] method_trace_started=true file=" + name);
            Thread stopper = new Thread(() -> {
                try { Thread.sleep(3_000); }
                catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); }
                try {
                    android.os.Debug.stopMethodTracing();
                    android.system.Os.chmod(file.getPath(), 0600);
                    System.err.println("[WL-SIGNIN-QUEUE] method_trace_written=true file=" + name);
                } catch (Exception unavailable) {
                    System.err.println("[WL-SIGNIN-QUEUE] method_trace_stop_failed=true");
                }
            }, "signin-method-diagnostic");
            stopper.setDaemon(true);
            stopper.start();
        } catch (Exception unavailable) {
            System.err.println("[WL-SIGNIN-QUEUE] method_trace_unavailable=true");
        }
    }

    private static String type(Object value) {
        if (value == null) return "none";
        String name = value.getClass().getName();
        return name.length() <= 200 && name.matches("[A-Za-z0-9_.$]+") ? name : "other";
    }

    private static void snapshot() throws ReflectiveOperationException {
        Looper main = Looper.getMainLooper();
        if (main == null) return;
        MessageQueue queue = main.getQueue();
        Field messages = MessageQueue.class.getDeclaredField("mMessages");
        Field next = Message.class.getDeclaredField("next");
        messages.setAccessible(true);
        next.setAccessible(true);
        StringBuilder output = new StringBuilder();
        long now = SystemClock.uptimeMillis();
        synchronized (queue) {
            Message message = (Message) messages.get(queue);
            output.append("[WL-SIGNIN-QUEUE] time=").append(System.currentTimeMillis())
                    .append(" head_barrier=").append(message != null && message.getTarget() == null);
            int count = 0;
            for (; message != null && count < 16; count++, message = (Message) next.get(message)) {
                output.append("\n[WL-SIGNIN-QUEUE] index=").append(count)
                        .append(" target=").append(type(message.getTarget()))
                        .append(" callback=").append(type(message.getCallback()))
                        .append(" what=").append(message.what)
                        .append(" async=").append(message.isAsynchronous())
                        .append(" overdue_ms=").append(now - message.getWhen());
            }
            output.append("\n[WL-SIGNIN-QUEUE] sampled=").append(count)
                    .append(" truncated=").append(message != null);
        }
        // Never inspect Message.obj, bundles, arguments, Intent contents or callback fields.
        System.err.println(output);
    }
}
