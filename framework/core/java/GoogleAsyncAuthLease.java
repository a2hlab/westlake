package adapter.core;

import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicBoolean;

/** An asynchronous API request is live only until its callback, death or deadline. */
public final class GoogleAsyncAuthLease {
    public final String owner;
    private final String signer, legacySigner, requestedAccount, epoch;
    private final GoogleAuthDelegationPolicy requested;
    private final Path directory;
    private final long deadline;
    private final AtomicBoolean active = new AtomicBoolean(true);
    private String boundAccount;

    public GoogleAsyncAuthLease(Path directory, String owner, String signer, String legacySigner,
            String requestedAccount, String service, long now) throws Exception {
        this.directory = directory; this.owner = owner; this.signer = signer; this.legacySigner = legacySigner;
        this.requestedAccount = requestedAccount;
        requested = GoogleAuthDelegationPolicy.parse(owner + ":" + legacySigner + ":" + service);
        GoogleAuthSessions.requireReusable(requested);
        if (requested.clientId != null || !requested.options.isEmpty()) throw new SecurityException("silent options unsupported");
        epoch = GoogleAuthSessions.generation(directory, owner);
        deadline = Math.addExact(now, 100_000L);
    }

    public boolean live(long now, boolean callerLive) { return active.get() && callerLive && now < deadline; }
    public void close() { active.set(false); }

    public synchronized boolean allows(String type, String name, GoogleAuthDelegationPolicy read,
            long now, boolean callerLive) {
        try {
            if (!live(now, callerLive) || !"com.google".equals(type) || name == null || name.isEmpty()
                    || !owner.equals(read.owner) || !legacySigner.equals(read.signer)
                    || read.clientId != null || !read.options.isEmpty()
                    || !requested.scopes.containsAll(read.scopes)
                    || requestedAccount != null && !requestedAccount.equals(name)
                    || boundAccount != null && !boundAccount.equals(name)
                    || !GoogleAuthSessions.allows(directory, owner, signer, legacySigner,
                            type, name, read, epoch)) return false;
            boundAccount = name;
            return active.get();
        } catch (Exception denied) { return false; }
    }
}
