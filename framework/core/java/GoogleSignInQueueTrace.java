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

    private GoogleSignInQueueTrace() {}

    static void schedule() {
        if (!"1".equals(System.getenv("WESTLAKE_GOOGLE_SIGNIN_QUEUE_TRACE"))
                || !"1".equals(System.getenv("WESTLAKE_GOOGLE_PRIVATE_LOGS"))
                || SAMPLES.incrementAndGet() > 8) return;
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
