package adapter.core;

/** A periodic window persisted as wall time so a process restart retains its phase. */
public final class PeriodicSyncSchedule {
    public static final long MIN_PERIOD_SECONDS = 15 * 60;
    public static final long MIN_FLEX_SECONDS = 5 * 60;
    public final long periodSeconds, flexSeconds, nextAtMillis;

    private PeriodicSyncSchedule(long period, long flex, long next) {
        if (period < MIN_PERIOD_SECONDS || period > Long.MAX_VALUE / 1000
                || flex < MIN_FLEX_SECONDS || flex > period || next < 0)
            throw new IllegalArgumentException("invalid periodic sync schedule");
        periodSeconds = period; flexSeconds = flex; nextAtMillis = next;
    }

    public static PeriodicSyncSchedule create(long period, long flex, long now) {
        if (now < 0 || period < 0 || period > Long.MAX_VALUE / 1000 || flex < 0)
            throw new IllegalArgumentException("invalid periodic sync interval");
        period = Math.max(MIN_PERIOD_SECONDS, period);
        flex = Math.min(period, Math.max(MIN_FLEX_SECONDS, flex));
        return new PeriodicSyncSchedule(period, flex, plus(now, (period - flex) * 1000));
    }

    public static PeriodicSyncSchedule restore(long period, long flex, long next) {
        return new PeriodicSyncSchedule(period, flex, next);
    }

    public boolean due(long now) { return now >= 0 && now >= nextAtMillis; }

    /** Skip missed windows instead of issuing a burst of catch-up jobs. */
    public PeriodicSyncSchedule advance(long now) {
        if (!due(now)) return this;
        long period = periodSeconds * 1000;
        long windows = (now - nextAtMillis) / period + 1;
        long delta = windows > Long.MAX_VALUE / period ? Long.MAX_VALUE : windows * period;
        return new PeriodicSyncSchedule(periodSeconds, flexSeconds, plus(nextAtMillis, delta));
    }

    private static long plus(long a, long b) { return a > Long.MAX_VALUE - b ? Long.MAX_VALUE : a + b; }
}
