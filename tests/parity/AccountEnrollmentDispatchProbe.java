import adapter.core.LocalServiceBinders;
import adapter.core.SocketBinder;
import android.accounts.AccountManager;
import android.accounts.IAccountAuthenticator;
import android.accounts.IAccountAuthenticatorResponse;
import android.accounts.IAccountManager;
import android.accounts.IAccountManagerResponse;
import android.app.ActivityThread;
import android.app.Application;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Binder;
import android.os.Bundle;
import android.os.Looper;
import android.os.Process;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;

/** Standalone board ART fixture. Its authenticator has no accounts or external services. */
public final class AccountEnrollmentDispatchProbe extends Application {
    private static final String TYPE = "org.westlake.test.account";
    private final ArrayBlockingQueue<ServiceConnection> binds = new ArrayBlockingQueue<>(2);
    private ComponentName component;

    @Override public String getPackageName() { return "org.westlake.test.authenticator"; }
    @Override public PackageManager getPackageManager() { return null; }

    @Override public boolean bindService(Intent intent, ServiceConnection listener, int flags) {
        component = intent.getComponent();
        binds.add(listener);
        return true;
    }

    private static void request(IAccountManager accounts, IAccountManagerResponse reply, boolean asUser,
            String[] features, Bundle options) throws Exception {
        if (asUser) accounts.addAccountAsUser(reply, TYPE, null, features, false, options, 0);
        else accounts.addAccount(reply, TYPE, null, features, false, options);
    }

    private void completeBind(int expectedUid, int expectedPid) throws Exception {
        ServiceConnection listener = binds.poll(8, TimeUnit.SECONDS);
        if (listener == null) throw new AssertionError("missing authenticator bind");
        ArrayBlockingQueue<Object[]> received = new ArrayBlockingQueue<>(1);
        IAccountAuthenticator fixture = (IAccountAuthenticator) Proxy.newProxyInstance(
                IAccountAuthenticator.class.getClassLoader(), new Class<?>[]{IAccountAuthenticator.class},
                (proxy, method, args) -> {
                    if (method.getName().equals("addAccount")) received.add(args);
                    return null;
                });
        Binder binder = new Binder();
        binder.attachInterface(fixture, IAccountAuthenticator.class.getName());
        listener.onServiceConnected(component, binder);
        Object[] args = received.poll(3, TimeUnit.SECONDS);
        if (args == null) throw new AssertionError("authenticator received no request");
        Bundle options = (Bundle) args[4];
        if (options.getInt(AccountManager.KEY_CALLER_UID, -1) != expectedUid
                || options.getInt(AccountManager.KEY_CALLER_PID, -1) != expectedPid) {
            throw new AssertionError("incorrect caller attribution");
        }
        if (!"original".equals(options.getString("marker")) || !"feature".equals(((String[]) args[3])[0])) {
            throw new AssertionError("enrollment changed after bind");
        }
        ((IAccountAuthenticatorResponse) args[0]).onResult(new Bundle());
    }

    private static AccountEnrollmentDispatchProbe install() throws Exception {
        if (Looper.myLooper() == null) Looper.prepare();
        Constructor<ActivityThread> constructor = ActivityThread.class.getDeclaredConstructor();
        constructor.setAccessible(true);
        ActivityThread thread = constructor.newInstance();
        Field current = ActivityThread.class.getDeclaredField("sCurrentActivityThread");
        Field application = ActivityThread.class.getDeclaredField("mInitialApplication");
        current.setAccessible(true);
        application.setAccessible(true);
        AccountEnrollmentDispatchProbe fixture = new AccountEnrollmentDispatchProbe();
        application.set(thread, fixture);
        current.set(null, thread);
        return fixture;
    }

    private static Bundle options() {
        Bundle options = new Bundle();
        options.putString("marker", "original");
        return options;
    }

    public static String query() throws Exception {
        String role = System.getenv("ENROLLMENT_TEST_ROLE");
        if ("client".equals(role)) return remoteClient();
        AccountEnrollmentDispatchProbe fixture = install();
        if ("server".equals(role)) return fixture.serve();
        IAccountManager accounts = IAccountManager.Stub.asInterface(LocalServiceBinders.get("account"));
        for (boolean asUser : new boolean[]{false, true}) {
            Bundle options = options();
            String[] features = {"feature"};
            request(accounts, null, asUser, features, options);
            // The caller can reuse these objects as soon as the method returns.
            options.putString("marker", "changed");
            features[0] = "changed";
            fixture.completeBind(Process.myUid(), Process.myPid());
            if (options.containsKey(AccountManager.KEY_CALLER_PID) || options.containsKey(AccountManager.KEY_CALLER_UID)) {
                throw new AssertionError("caller's bundle was modified");
            }
        }
        return "Both enrollment methods retained their caller and original request.\n";
    }

    private String serve() throws Exception {
        Path directory = Paths.get(System.getenv("ENROLLMENT_TEST_DIR"));
        SocketBinder.installCallingIdentity();
        int socket = SocketBinder.listen(System.getenv("ENROLLMENT_TEST_SOCKET"));
        Files.createFile(directory.resolve("ready"));
        int fd = SocketBinder.accept(socket);
        int peerPid = SocketBinder.peerPid(fd);
        // The launcher records its child PID privately before allowing it to connect.
        String launchedPid = new String(Files.readAllBytes(directory.resolve("client.pid")), StandardCharsets.US_ASCII).trim();
        if (peerPid != Integer.parseInt(launchedPid) || peerPid == Process.myPid()) {
            throw new AssertionError("unexpected socket peer");
        }
        String uidLine = Files.readAllLines(Paths.get("/proc/" + peerPid + "/status")).stream()
                .filter(line -> line.startsWith("Uid:")).findFirst().get();
        int peerUid = Integer.parseInt(uidLine.trim().split("\\s+")[2]);
        if (peerUid == 0 || peerUid == Process.myUid()) throw new AssertionError("expected distinct native UIDs");
        SocketBinder channel = new SocketBinder(fd, LocalServiceBinders.get("account"), "enrollment-fixture");
        channel.setCaller(peerUid, peerPid);
        Files.createFile(directory.resolve("accepted"));
        completeBind(peerUid, peerPid);
        completeBind(peerUid, peerPid);
        return "Both enrollment methods delivered the kernel-verified peer UID and PID.\n";
    }

    private static String remoteClient() throws Exception {
        Path directory = Paths.get(System.getenv("ENROLLMENT_TEST_DIR"));
        awaitFile(directory.resolve("client.pid"));
        SocketBinder channel = new SocketBinder(SocketBinder.connect(System.getenv("ENROLLMENT_TEST_SOCKET")),
                null, "enrollment-fixture");
        awaitFile(directory.resolve("accepted"));
        IAccountManager accounts = IAccountManager.Stub.asInterface(channel.getRemoteRoot());
        ArrayBlockingQueue<Boolean> results = new ArrayBlockingQueue<>(2);
        IAccountManagerResponse reply = new IAccountManagerResponse.Stub() {
            @Override public void onResult(Bundle result) { results.add(true); }
            @Override public void onError(int code, String message) { results.add(false); }
        };
        for (boolean asUser : new boolean[]{false, true}) {
            request(accounts, reply, asUser, new String[]{"feature"}, options());
            if (!Boolean.TRUE.equals(results.poll(6, TimeUnit.SECONDS))) throw new AssertionError("missing result");
        }
        return "Both remote enrollment callbacks completed.\n";
    }

    private static void awaitFile(Path path) throws Exception {
        for (int i = 0; i < 150; i++) {
            if (Files.exists(path)) return;
            Thread.sleep(50);
        }
        throw new AssertionError("fixture rendezvous timed out");
    }
}
