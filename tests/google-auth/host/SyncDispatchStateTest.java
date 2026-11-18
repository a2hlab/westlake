package adapter.core;

public final class SyncDispatchStateTest {
    private static int checks;
    private static void require(boolean value) { checks++; if (!value) throw new AssertionError("check " + checks); }
    public static void main(String[] args) throws Exception {
        SyncDispatchState state = new SyncDispatchState();
        SyncDispatchState.Ticket initialization = state.begin();
        require(initialization != null);
        require(state.begin() == null);
        require(!state.accept(null));
        require(state.accept(initialization));
        require(!state.accept(initialization));
        SyncDispatchState.Ticket actual = state.begin();
        require(actual != null);
        require(!state.accept(initialization)); // a late initialization reply cannot complete actual sync
        SyncDispatchState other = new SyncDispatchState();
        SyncDispatchState.Ticket foreign = other.begin();
        require(!state.accept(foreign));
        state.cancel();
        require(state.begin() == null);
        require(state.accept(actual)); // cancellation still allows its own completion
        require(state.begin() == null);
        other.close();
        require(!other.accept(foreign));
        require(other.begin() == null);
        // Concurrent duplicate callbacks have one winner.
        SyncDispatchState race = new SyncDispatchState();
        SyncDispatchState.Ticket ticket = race.begin();
        java.util.concurrent.atomic.AtomicInteger accepted = new java.util.concurrent.atomic.AtomicInteger();
        Thread[] threads = new Thread[16];
        for (int i=0;i<threads.length;i++) { threads[i]=new Thread(() -> { if (race.accept(ticket)) accepted.incrementAndGet(); }); threads[i].start(); }
        for (Thread t:threads) t.join();
        require(accepted.get()==1);
        System.out.println("SYNC_DISPATCH_STATE_PASS " + checks);
    }
}
