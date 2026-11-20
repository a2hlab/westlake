package adapter.core;

/**
 * Live asynchronous Authorization API requests (IAuthorizationService.authorize) by verified apps.
 *
 * Play services answers an app's authorization request on a worker after the incoming call has
 * returned, so the request is no longer "being served" when Play services asks which accounts the app
 * may see. A lease stands in for that call: it is opened only by the verified app's own request, and
 * it ends at the request's callback, its caller's death or a deadline. While one is live, the app's
 * signer-pinned grant (not this class) decides which account the app may see.
 */
public final class GoogleAuthorizationLeases {
    public static final long LIFETIME_MS = 100_000L;
    public static final int MAX_LIVE = 16;

    /** Whether the requesting side (its channel and callback) is still alive. */
    public interface Liveness { boolean alive(); }

    public static final class Lease {
        final String owner, signer;
        final long deadline;
        final Liveness liveness;
        private volatile boolean closed;

        Lease(String owner, String signer, long deadline, Liveness liveness) {
            this.owner = owner; this.signer = signer; this.deadline = deadline; this.liveness = liveness;
        }

        public boolean live(long now) {
            if (closed || now >= deadline) return false;
            try { return liveness.alive(); } catch (RuntimeException dead) { return false; }
        }
    }

    private final java.util.List<Lease> leases = new java.util.ArrayList<>();

    public synchronized Lease open(String owner, String signer, long now, Liveness liveness) {
        if (owner == null || owner.isEmpty() || signer == null || signer.isEmpty() || liveness == null)
            throw new SecurityException("invalid authorization request owner");
        leases.removeIf(lease -> !lease.live(now));
        if (leases.size() >= MAX_LIVE) throw new SecurityException("too many authorization requests");
        Lease lease = new Lease(owner, signer, Math.addExact(now, LIFETIME_MS), liveness);
        leases.add(lease);
        return lease;
    }

    /** The signer of owner's live requests; null when none is live or live requests disagree. */
    public synchronized String liveSigner(String owner, long now) {
        if (owner == null) return null;
        String found = null;
        for (Lease lease : leases) {
            if (!owner.equals(lease.owner) || !lease.live(now)) continue;
            if (found != null && !found.equals(lease.signer)) return null;
            found = lease.signer;
        }
        return found;
    }

    public synchronized void close(Lease lease) {
        if (lease == null) return;
        lease.closed = true;
        leases.remove(lease);
    }

    synchronized int size() { return leases.size(); }
}
