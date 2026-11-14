package adapter.packagemanager;

import android.app.ActivityThread;
import android.app.Application;
import android.content.Intent;
import android.content.IntentSender;
import android.content.pm.IPackageInstallerCallback;
import android.content.pm.IPackageInstallerSession;
import android.content.pm.PackageInstaller;
import android.content.pm.ParceledListSlice;
import android.content.pm.Signature;
import android.content.pm.SigningDetails;
import android.content.pm.parsing.ApkLite;
import android.content.pm.parsing.ApkLiteParseUtils;
import android.content.pm.parsing.result.ParseResult;
import android.content.pm.parsing.result.ParseTypeImpl;
import android.os.Binder;
import android.os.FileBridge;
import android.os.IInterface;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.apk.ApkSignatureVerifier;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Proxy;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * PackageInstaller sessions, first step of ADR-0006: what an installer writes is staged and verified, and nothing is
 * published yet.
 *
 * The genuine Play Store installs through sessions. It creates one before it downloads, writes each APK into it
 * through openWrite (a FileBridge: the client sends framed writes, not raw bytes) and commits it with an IntentSender
 * for the result. Westlake had no sessions: createSession threw, and the Store said "Can't install". Here the
 * sessions live in the installer's own process and stage under its cache directory, so this step isolates nothing;
 * ADR-0006 moves them into a trusted service with the user's confirmation and a package registry. At commit the
 * staged APKs are verified (package, version, split, signing certificates) and logged ([WL-INSTALL]), and the
 * installer is told STATUS_FAILURE with that reason, so it sees an honest failure rather than a silent success.
 */
public final class InstallerSessions {
    private static final String TAG = "[WL-INSTALL] ";
    /** The answer for an IPackageInstaller call this class does not take: the caller's default. */
    public static final Object UNANSWERED = new Object();
    private static final Map<Integer, Session> SESSIONS = new ConcurrentHashMap<>();
    private static final List<IPackageInstallerCallback> CALLBACKS = new CopyOnWriteArrayList<>();
    private static final SecureRandom RANDOM = new SecureRandom();

    private InstallerSessions() {}

    private static final class Session {
        final int id;
        final PackageInstaller.SessionParams params;
        final String installer;
        final int userId;
        final File dir;
        final List<FileBridge> bridges = new CopyOnWriteArrayList<>();
        volatile float clientProgress;
        volatile boolean sealed;
        volatile boolean finished;
        IPackageInstallerSession binder;

        Session(int id, PackageInstaller.SessionParams params, String installer, int userId, File dir) {
            this.id = id;
            this.params = params;
            this.installer = installer;
            this.userId = userId;
            this.dir = dir;
        }
    }

    /** IPackageInstaller's calls that sessions need; UNANSWERED for the rest. */
    public static Object installer(String method, Object[] args) throws IOException {
        switch (method) {
            case "createSession":
                return createSession((PackageInstaller.SessionParams) args[0], (String) args[1], (Integer) args[3]);
            case "openSession":
                return known((Integer) args[0]).binder;
            case "getSessionInfo": {
                Session session = SESSIONS.get((Integer) args[0]);
                return session == null ? null : info(session);
            }
            case "getAllSessions":
            case "getMySessions": {
                List<PackageInstaller.SessionInfo> infos = new ArrayList<>();
                for (Session session : SESSIONS.values()) {
                    if (!session.finished) infos.add(info(session));
                }
                return new ParceledListSlice<>(infos);
            }
            case "getStagedSessions":
                return new ParceledListSlice<>(new ArrayList<PackageInstaller.SessionInfo>());
            case "abandonSession":
                abandon(known((Integer) args[0]));
                return null;
            case "registerCallback":
                CALLBACKS.add((IPackageInstallerCallback) args[0]);
                return null;
            case "unregisterCallback":
                CALLBACKS.remove(args[0]);
                return null;
            case "updateSessionAppLabel":
                System.err.println(TAG + "session " + args[0] + " is labelled \"" + args[1] + "\"");
                return null;
            case "updateSessionAppIcon":
                return null;
            default:
                return UNANSWERED;
        }
    }

    private static Session known(int id) {
        Session session = SESSIONS.get(id);
        if (session == null) {
            // Sessions live in the process that created them for now; another process of the installer asking
            // for one is something this step cannot answer, and the log says so.
            System.err.println(TAG + "session " + id + " is not one this process created");
            throw new SecurityException("Caller has no access to session " + id);
        }
        return session;
    }

    private static int createSession(PackageInstaller.SessionParams params, String installer, int userId)
            throws IOException {
        Application app = ActivityThread.currentApplication();
        if (app == null) throw new IllegalStateException("no application to stage a session in");
        int id;
        do {
            id = RANDOM.nextInt(Integer.MAX_VALUE - 1) + 1;
        } while (SESSIONS.containsKey(id));
        File dir = new File(app.getCacheDir(), "wl-install/" + id);
        if (!dir.mkdirs() && !dir.isDirectory()) throw new IOException("cannot create " + dir);
        Session session = new Session(id, params, installer, userId, dir);
        session.binder = sessionBinder(session);
        SESSIONS.put(id, session);
        System.err.println(TAG + "createSession " + id + " by " + installer + " for " + params.appPackageName
                + ": mode=" + params.mode + " flags=0x" + Integer.toHexString(params.installFlags)
                + " size=" + params.sizeBytes + " reason=" + params.installReason
                + " scenario=" + params.installScenario + " source=" + params.packageSource
                + " requireUserAction=" + params.requireUserAction + " multiPackage=" + params.isMultiPackage
                + " staged=" + params.isStaged + " dataLoader=" + (params.dataLoaderParams != null)
                + " location=" + params.installLocation + " user=" + userId);
        for (IPackageInstallerCallback callback : CALLBACKS) {
            try {
                callback.onSessionCreated(id);
            } catch (RemoteException ignored) {
            }
        }
        return id;
    }

    private static PackageInstaller.SessionInfo info(Session session) {
        PackageInstaller.SessionInfo info = new PackageInstaller.SessionInfo();
        info.sessionId = session.id;
        info.userId = session.userId;
        info.installerPackageName = session.installer;
        info.progress = session.clientProgress;
        info.sealed = session.sealed;
        info.active = !session.finished;
        info.mode = session.params.mode;
        info.installReason = session.params.installReason;
        info.sizeBytes = session.params.sizeBytes;
        info.appPackageName = session.params.appPackageName;
        info.appLabel = session.params.appLabel;
        info.installFlags = session.params.installFlags;
        info.isMultiPackage = session.params.isMultiPackage;
        info.isStaged = session.params.isStaged;
        info.parentSessionId = PackageInstaller.SessionInfo.INVALID_ID;
        info.childSessionIds = new int[0];
        return info;
    }

    /** The session's IPackageInstallerSession, in this process like the installer it serves. */
    private static IPackageInstallerSession sessionBinder(Session session) {
        Binder binder = new Binder();
        IPackageInstallerSession local = (IPackageInstallerSession) Proxy.newProxyInstance(
                InstallerSessions.class.getClassLoader(), new Class<?>[] {IPackageInstallerSession.class},
                (self, method, args) -> {
                    if ("asBinder".equals(method.getName())) return binder;
                    Object answer;
                    try {
                        answer = sessionCall(session, method.getName(), args);
                    } catch (IOException e) {
                        throw new android.os.ParcelableException(e);   // as the platform's session wraps it
                    }
                    if (answer != UNANSWERED) return answer;
                    System.err.println(TAG + "session " + session.id + "." + method.getName() + " is not taken");
                    Class<?> type = method.getReturnType();
                    if (type == boolean.class) return false;
                    if (type == int.class) return 0;
                    if (type == long.class) return 0L;
                    if (type == float.class) return 0f;
                    return null;
                });
        binder.attachInterface((IInterface) local, "android.content.pm.IPackageInstallerSession");
        return local;
    }

    private static Object sessionCall(Session session, String method, Object[] args) throws IOException {
        switch (method) {
            case "setClientProgress":
                progress(session, (Float) args[0]);
                return null;
            case "addClientProgress":
                progress(session, session.clientProgress + (Float) args[0]);
                return null;
            case "getNames": {
                String[] names = session.dir.list();
                return names != null ? names : new String[0];
            }
            case "openWrite":
                return openWrite(session, (String) args[0], (Long) args[1], (Long) args[2]);
            case "write":
                write(session, (String) args[0], (Long) args[1], (Long) args[2], (ParcelFileDescriptor) args[3]);
                return null;
            case "openRead":
                return ParcelFileDescriptor.open(staged(session, (String) args[0]),
                        ParcelFileDescriptor.MODE_READ_ONLY);
            case "removeSplit":
                System.err.println(TAG + "session " + session.id + " removeSplit " + args[0] + " (not supported)");
                throw new UnsupportedOperationException("removing a split is not supported yet");
            case "close":
                return null;
            case "seal":
                session.sealed = true;
                return null;
            case "commit":
                commit(session, (IntentSender) args[0], (Boolean) args[1]);
                return null;
            case "abandon":
                abandon(session);
                return null;
            case "requestUserPreapproval":
                System.err.println(TAG + "session " + session.id + " asks for the user's pre-approval");
                send((IntentSender) args[1], session, PackageInstaller.STATUS_FAILURE,
                        "user pre-approval is not supported yet");
                return null;
            case "isMultiPackage":
                return session.params.isMultiPackage;
            case "isStaged":
                return session.params.isStaged;
            case "getInstallFlags":
                return session.params.installFlags;
            case "getChildSessionIds":
                return new int[0];
            case "getParentSessionId":
                return PackageInstaller.SessionInfo.INVALID_ID;
            case "isApplicationEnabledSettingPersistent":
                return session.params.applicationEnabledSettingPersistent;
            case "isRequestUpdateOwnership":
                return (session.params.installFlags & 0x02000000 /* INSTALL_REQUEST_UPDATE_OWNERSHIP */) != 0;
            case "setChecksums":
                System.err.println(TAG + "session " + session.id + " checksums for " + args[0]);
                return null;
            default:
                return UNANSWERED;
        }
    }

    private static void progress(Session session, float progress) {
        float clamped = Math.max(0f, Math.min(1f, progress));
        float before = session.clientProgress;
        session.clientProgress = clamped;
        // A step of at least 10%, so a 50 MB download logs ten lines rather than thousands.
        if ((int) (clamped * 10) != (int) (before * 10)) {
            System.err.println(TAG + "session " + session.id + " progress " + (int) (clamped * 100) + "%");
        }
        for (IPackageInstallerCallback callback : CALLBACKS) {
            try {
                callback.onSessionProgressChanged(session.id, clamped);
            } catch (RemoteException ignored) {
            }
        }
    }

    /** A staged file's path; the name must be a plain file name. */
    private static File staged(Session session, String name) {
        if (name == null || name.isEmpty() || name.contains("/") || name.equals(".") || name.equals("..")) {
            throw new IllegalArgumentException("Invalid name: " + name);
        }
        return new File(session.dir, name);
    }

    /** As PackageInstallerSession does without revocable descriptors: a FileBridge into the staged file. */
    private static ParcelFileDescriptor openWrite(Session session, String name, long offset, long length)
            throws IOException {
        if (session.sealed || session.finished) throw new SecurityException("Session " + session.id + " is sealed");
        File target = staged(session, name);
        ParcelFileDescriptor out = ParcelFileDescriptor.open(target,
                ParcelFileDescriptor.MODE_WRITE_ONLY | ParcelFileDescriptor.MODE_CREATE);
        try {
            if (offset > 0) android.system.Os.lseek(out.getFileDescriptor(), offset, android.system.OsConstants.SEEK_SET);
        } catch (android.system.ErrnoException e) {
            out.close();
            throw new IOException(e);
        }
        FileBridge bridge = new FileBridge();
        bridge.setTargetFile(out);
        bridge.start();
        session.bridges.add(bridge);
        System.err.println(TAG + "session " + session.id + " openWrite " + name + " offset=" + offset
                + " length=" + length);
        return bridge.getClientSocket();
    }

    private static void write(Session session, String name, long offset, long length, ParcelFileDescriptor in)
            throws IOException {
        if (session.sealed || session.finished) throw new SecurityException("Session " + session.id + " is sealed");
        File target = staged(session, name);
        System.err.println(TAG + "session " + session.id + " write " + name + " offset=" + offset
                + " length=" + length);
        try (InputStream input = new ParcelFileDescriptor.AutoCloseInputStream(in);
                FileOutputStream output = new FileOutputStream(target, offset > 0)) {
            byte[] buffer = new byte[64 * 1024];
            long left = length >= 0 ? length : Long.MAX_VALUE;
            while (left > 0) {
                int n = input.read(buffer, 0, (int) Math.min(buffer.length, left));
                if (n < 0) break;
                output.write(buffer, 0, n);
                left -= n;
            }
            output.getFD().sync();
        }
    }

    private static void commit(Session session, IntentSender statusReceiver, boolean forTransferred) {
        session.sealed = true;
        for (FileBridge bridge : session.bridges) {
            if (!bridge.isClosed()) {
                System.err.println(TAG + "session " + session.id + " committed with a write still open");
            }
        }
        String verdict = verify(session);
        System.err.println(TAG + "session " + session.id + " commit (transferred=" + forTransferred + "): "
                + verdict + "; not published: installing is not supported yet (ADR-0006)");
        session.finished = true;
        send(statusReceiver, session, PackageInstaller.STATUS_FAILURE,
                "Westlake staged and checked the package (" + verdict + ") but cannot install it yet");
        for (IPackageInstallerCallback callback : CALLBACKS) {
            try {
                callback.onSessionFinished(session.id, false);
            } catch (RemoteException ignored) {
            }
        }
    }

    /** What the staged APKs are, one clause per file: package, version, split, size, digest, signers. */
    private static String verify(Session session) {
        File[] files = session.dir.listFiles();
        if (files == null || files.length == 0) return "nothing staged";
        StringBuilder out = new StringBuilder();
        for (File file : files) {
            if (out.length() > 0) out.append("; ");
            out.append(file.getName()).append(' ').append(file.length()).append(" bytes sha256 ")
                    .append(digest(file));
            ParseResult<ApkLite> lite = ApkLiteParseUtils.parseApkLite(
                    ParseTypeImpl.forParsingWithoutPlatformCompat().reset(), file, 0);
            if (lite.isError()) {
                out.append(" unparsed (").append(lite.getErrorMessage()).append(')');
            } else {
                ApkLite apk = lite.getResult();
                out.append(' ').append(apk.getPackageName()).append(" v").append(apk.getLongVersionCode());
                if (apk.getSplitName() != null) out.append(" split ").append(apk.getSplitName());
            }
            ParseResult<SigningDetails> signing = ApkSignatureVerifier.verify(
                    ParseTypeImpl.forParsingWithoutPlatformCompat().reset(), file.getPath(),
                    SigningDetails.SignatureSchemeVersion.JAR);
            if (signing.isError()) {
                out.append(" signature unverified (").append(signing.getErrorMessage()).append(')');
            } else {
                out.append(" signed v").append(signing.getResult().getSignatureSchemeVersion()).append(" by");
                for (Signature signer : signing.getResult().getSignatures()) {
                    out.append(' ').append(sha256(signer.toByteArray()).substring(0, 16));
                }
            }
        }
        return out.toString();
    }

    private static void abandon(Session session) {
        session.finished = true;
        for (FileBridge bridge : session.bridges) bridge.forceClose();
        File[] files = session.dir.listFiles();
        if (files != null) {
            for (File file : files) file.delete();
        }
        session.dir.delete();
        SESSIONS.remove(session.id);
        System.err.println(TAG + "session " + session.id + " abandoned");
        for (IPackageInstallerCallback callback : CALLBACKS) {
            try {
                callback.onSessionFinished(session.id, false);
            } catch (RemoteException ignored) {
            }
        }
    }

    private static void send(IntentSender receiver, Session session, int status, String message) {
        if (receiver == null) return;
        Intent fillIn = new Intent();
        fillIn.putExtra(PackageInstaller.EXTRA_SESSION_ID, session.id);
        fillIn.putExtra(PackageInstaller.EXTRA_STATUS, status);
        fillIn.putExtra(PackageInstaller.EXTRA_STATUS_MESSAGE, message);
        if (session.params.appPackageName != null) {
            fillIn.putExtra(PackageInstaller.EXTRA_PACKAGE_NAME, session.params.appPackageName);
        }
        try {
            receiver.sendIntent(ActivityThread.currentApplication(), 0, fillIn, null, null);
        } catch (IntentSender.SendIntentException | RuntimeException e) {
            System.err.println(TAG + "session " + session.id + " result not delivered: " + e);
        }
    }

    private static String digest(File file) {
        try (InputStream input = new FileInputStream(file)) {
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[64 * 1024];
            for (int n; (n = input.read(buffer)) > 0; ) sha.update(buffer, 0, n);
            return hex(sha.digest()).substring(0, 16);
        } catch (IOException | NoSuchAlgorithmException e) {
            return "unread";
        }
    }

    private static String sha256(byte[] bytes) {
        try {
            return hex(MessageDigest.getInstance("SHA-256").digest(bytes));
        } catch (NoSuchAlgorithmException e) {
            return "unknown";
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder();
        for (byte b : bytes) out.append(String.format("%02x", b & 0xff));
        return out.toString();
    }
}
