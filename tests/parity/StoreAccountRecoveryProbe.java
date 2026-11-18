import adapter.core.LocalServiceBinders;
import android.accounts.AccountManager;
import android.accounts.IAccountManager;
import android.accounts.IAccountManagerResponse;
import android.content.Intent;
import android.os.Binder;
import android.os.Bundle;
import android.os.DeadObjectException;
import android.os.IBinder;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Supplier;

/** Exercises Store account enrollment with inert account services in a standalone ART process. */
public final class StoreAccountRecoveryProbe {
    private static final class Reply extends IAccountManagerResponse.Stub {
        int results;
        int failures;
        @Override public void onResult(Bundle result) { results++; }
        @Override public void onError(int code, String message) {
            check(code == AccountManager.ERROR_CODE_REMOTE_EXCEPTION, "unexpected error code");
            failures++;
        }
    }

    private static final class Peer extends Binder {
        final List<DeathRecipient> observers = new ArrayList<>();
        final List<IAccountManagerResponse> pending = new ArrayList<>();
        boolean alive = true;
        boolean deny;
        boolean dieDuringDispatch;
        Peer() {
            IAccountManager target = (IAccountManager) Proxy.newProxyInstance(
                    IAccountManager.class.getClassLoader(), new Class<?>[]{IAccountManager.class},
                    (proxy, method, args) -> {
                        if (method.getName().equals("asBinder")) return this;
                        if (deny) throw new SecurityException("fixture denied");
                        if (!method.getName().equals("addAccount") && !method.getName().equals("addAccountAsUser")) {
                            throw new AssertionError(method.getName());
                        }
                        pending.add((IAccountManagerResponse) args[0]);
                        if (dieDuringDispatch) {
                            disconnect();
                            throw new DeadObjectException();
                        }
                        return null;
                    });
            attachInterface(target, IAccountManager.class.getName());
        }
        @Override public boolean isBinderAlive() { return alive; }
        @Override public void linkToDeath(DeathRecipient observer, int flags) { observers.add(observer); }
        @Override public boolean unlinkToDeath(DeathRecipient observer, int flags) { return observers.remove(observer); }
        void disconnect() {
            alive = false;
            for (DeathRecipient observer : new ArrayList<>(observers)) observer.binderDied();
        }
    }

    private static void check(boolean ok, String reason) {
        if (!ok) throw new AssertionError(reason);
    }

    private static void enroll(IAccountManager manager, Reply reply) throws Exception {
        manager.addAccount(reply, "org.westlake.test.account", null, null, true, new Bundle());
    }

    public static String query() throws Exception {
        AtomicReference<IBinder> endpoint = new AtomicReference<>();
        Class<?> type = Class.forName("adapter.core.StoreAccountService");
        Method create = type.getDeclaredMethod("create", Supplier.class, IBinder.class);
        create.setAccessible(true);
        IBinder empty = LocalServiceBinders.get("account");
        IBinder client = (IBinder) create.invoke(null, (Supplier<IBinder>) endpoint::get, empty);
        IAccountManager manager = IAccountManager.Stub.asInterface(client);
        Reply absent = new Reply();
        enroll(manager, absent);
        check(absent.failures == 1 && absent.results == 0, "missing service callback");

        Peer first = new Peer();
        endpoint.set(first);
        Reply interactive = new Reply(), completed = new Reply();
        enroll(manager, interactive);
        manager.addAccountAsUser(completed, "org.westlake.test.account", null, null, false, new Bundle(), 0);
        Bundle intermediate = new Bundle();
        intermediate.putParcelable(AccountManager.KEY_INTENT, new Intent("org.westlake.test.ENROLL"));
        first.pending.get(0).onResult(intermediate);
        first.pending.get(1).onResult(new Bundle());
        check(first.observers.size() == 1, "incorrect number of pending death observers");
        first.disconnect();
        first.pending.get(0).onResult(new Bundle());
        check(interactive.results == 1 && interactive.failures == 1, "interactive request did not fail exactly once");
        check(completed.results == 1 && completed.failures == 0, "completed request got a second callback");
        check(first.observers.isEmpty(), "death observer leaked");

        Peer interrupted = new Peer();
        interrupted.dieDuringDispatch = true;
        endpoint.set(interrupted);
        Reply lost = new Reply();
        enroll(manager, lost);
        check(lost.failures == 1 && lost.results == 0 && interrupted.pending.size() == 1,
                "delivery was replayed or death was reported twice");

        Peer recovered = new Peer();
        endpoint.set(recovered);
        check(recovered.pending.isEmpty(), "unsolicited replay");
        Reply next = new Reply();
        enroll(manager, next);
        recovered.pending.get(0).onResult(new Bundle());
        check(recovered.pending.size() == 1 && next.results == 1 && next.failures == 0,
                "new request did not reach recovered service");

        recovered.deny = true;
        Reply denied = new Reply();
        try {
            enroll(manager, denied);
            throw new AssertionError("backend refusal was swallowed");
        } catch (SecurityException expected) {
            check(denied.failures == 0 && recovered.observers.isEmpty(), "refusal mishandled");
        }
        return "Unavailable, overlapping, interrupted and recovered enrollment checks passed.\n";
    }
}
