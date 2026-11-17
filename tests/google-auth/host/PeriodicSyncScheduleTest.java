package adapter.core;

public final class PeriodicSyncScheduleTest {
    private static int checks;
    private static void yes(boolean value) { checks++; if (!value) throw new AssertionError("check " + checks); }
    private static void denied(Runnable call) {
        checks++;
        try { call.run(); throw new AssertionError("accepted at " + checks); }
        catch (IllegalArgumentException expected) { }
    }
    public static void main(String[] args) {
        long now = 1_800_000_000_000L;
        PeriodicSyncSchedule p = PeriodicSyncSchedule.create(60, 0, now);
        yes(p.periodSeconds == 900); yes(p.flexSeconds == 300); yes(p.nextAtMillis == now + 600_000);
        yes(!p.due(now)); yes(!p.due(now + 599_999)); yes(p.due(now + 600_000));
        yes(p.advance(now) == p);
        PeriodicSyncSchedule next = p.advance(now + 600_000);
        yes(next.nextAtMillis == now + 1_500_000); yes(!next.due(now + 600_000));
        PeriodicSyncSchedule restored = PeriodicSyncSchedule.restore(next.periodSeconds, next.flexSeconds, next.nextAtMillis);
        yes(restored.nextAtMillis == next.nextAtMillis); yes(!restored.due(now));
        yes(restored.advance(now + 3_600_000).nextAtMillis == now + 4_200_000);
        PeriodicSyncSchedule immediate = PeriodicSyncSchedule.create(900, 900, now);
        yes(immediate.due(now)); yes(immediate.advance(now).nextAtMillis == now + 900_000);
        yes(PeriodicSyncSchedule.create(900, 9999, now).flexSeconds == 900);
        denied(() -> PeriodicSyncSchedule.create(-1, 0, now));
        denied(() -> PeriodicSyncSchedule.create(900, -1, now));
        denied(() -> PeriodicSyncSchedule.create(Long.MAX_VALUE, 0, now));
        denied(() -> PeriodicSyncSchedule.create(900, 0, -1));
        denied(() -> PeriodicSyncSchedule.restore(899, 300, now));
        denied(() -> PeriodicSyncSchedule.restore(900, 299, now));
        denied(() -> PeriodicSyncSchedule.restore(900, 901, now));
        denied(() -> PeriodicSyncSchedule.restore(900, 300, -1));
        PeriodicSyncSchedule huge = PeriodicSyncSchedule.create(Long.MAX_VALUE / 1000, 300, now);
        yes(huge.nextAtMillis == Long.MAX_VALUE); yes(huge.advance(Long.MAX_VALUE).nextAtMillis == Long.MAX_VALUE);
        yes(!p.due(-1));
        System.out.println("PERIODIC_SYNC_SCHEDULE_PASS " + checks);
    }
}
