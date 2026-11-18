package adapter.core;

import android.accounts.AccountManager;
import android.accounts.IAccountManager;
import android.accounts.IAccountManagerResponse;
import android.content.Intent;
import android.os.Binder;
import android.os.Bundle;
import android.os.DeadObjectException;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.function.Supplier;

/** The Store keeps its AccountManager while the process providing the account service can restart. */
final class StoreAccountService implements InvocationHandler {
    private final Binder local = new Binder();
    private final Supplier<IBinder> connect;
    private final IAccountManager empty;
    private IAccountManager service;

    private StoreAccountService(Supplier<IBinder> connect, IBinder empty) {
        this.connect = connect;
        this.empty = IAccountManager.Stub.asInterface(empty);
    }

    static IBinder create(Supplier<IBinder> connect, IBinder empty) {
        StoreAccountService handler = new StoreAccountService(connect, empty);
        IAccountManager manager = (IAccountManager) Proxy.newProxyInstance(
                IAccountManager.class.getClassLoader(), new Class<?>[]{IAccountManager.class}, handler);
        // Only the Store's in-process callers can use this interface.
        handler.local.attachInterface(manager, IAccountManager.class.getName());
        return handler.local;
    }

    private synchronized IAccountManager currentService() {
        if (service == null || !service.asBinder().isBinderAlive()) {
            service = IAccountManager.Stub.asInterface(connect.get());
        }
        return service;
    }

    @Override public Object invoke(Object self, Method method, Object[] args) throws Throwable {
        String name = method.getName();
        if (method.getDeclaringClass() == Object.class) {
            if (name.equals("equals")) return self == args[0];
            if (name.equals("hashCode")) return System.identityHashCode(self);
            return "StoreAccountService";
        }
        if (name.equals("asBinder")) return local;
        IAccountManager target = currentService();
        Enrollment enrollment = null;
        if (target != null && (name.equals("addAccount") || name.equals("addAccountAsUser")) && args[0] != null) {
            enrollment = new Enrollment(target.asBinder(), (IAccountManagerResponse) args[0], (Boolean) args[4]);
            args[0] = enrollment;
        }
        try {
            if (enrollment != null) enrollment.watch();
            try {
                return method.invoke(target == null ? empty : target, args);
            } catch (InvocationTargetException e) {
                throw e.getCause();
            }
        } catch (DeadObjectException e) {
            // Delivery is ambiguous. Only a new caller request may start another enrollment.
            synchronized (this) { if (service == target) service = null; }
            if (enrollment == null) throw e;
            enrollment.binderDied();
            return null;
        } catch (Throwable e) {
            if (enrollment != null) enrollment.finish();
            throw e;
        }
    }

    private static final class Enrollment extends IAccountManagerResponse.Stub implements IBinder.DeathRecipient {
        final IBinder peer;
        final IAccountManagerResponse caller;
        final boolean expectsActivity;
        private boolean done;
        private boolean linked;

        Enrollment(IBinder peer, IAccountManagerResponse caller, boolean expectsActivity) {
            this.peer = peer;
            this.caller = caller;
            this.expectsActivity = expectsActivity;
        }

        synchronized void watch() throws RemoteException {
            peer.linkToDeath(this, 0);
            linked = true;
        }

        synchronized boolean finish() {
            if (done) return false;
            done = true;
            if (linked) peer.unlinkToDeath(this, 0);
            return true;
        }

        @Override public synchronized void onResult(Bundle result) throws RemoteException {
            if (done) return;
            // Android can send the login UI first, then complete the same request later.
            boolean interactive = expectsActivity && result != null
                    && result.getParcelable(AccountManager.KEY_INTENT, Intent.class) != null;
            if (!interactive) finish();
            try {
                caller.onResult(result);
            } catch (RemoteException | RuntimeException e) {
                finish();
                throw e;
            }
        }

        @Override public void onError(int code, String message) throws RemoteException {
            if (finish()) caller.onError(code, message);
        }

        @Override public void binderDied() {
            if (finish()) LocalAccounts.authenticatorUnavailable(caller);
        }
    }
}
