package adapter.core;

/** Completion belongs to one dispatch, including initialization followed by a real sync. */
public final class SyncDispatchState {
    public static final class Ticket { private Ticket() {} }
    private Ticket current;
    private boolean cancelled, closed;

    public synchronized Ticket begin() {
        if (cancelled || closed || current != null) return null;
        return current = new Ticket();
    }
    public synchronized boolean accept(Ticket ticket) {
        if (closed || ticket == null || current != ticket) return false;
        current = null;
        return true;
    }
    /** A cancellation can still receive its current callback, but cannot start another dispatch. */
    public synchronized void cancel() { cancelled = true; }
    public synchronized void close() { closed = true; current = null; }
}
