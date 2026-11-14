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
 * PackageInstaller sessions (ADR-0006): what an installer writes is staged, verified and published.
 *
 * The genuine Play Store installs through sessions. It creates one before it downloads, writes each APK, its dex
 * metadata and its app metadata into it through openWrite and openWriteAppMetadata (FileBridges: the client sends
 * framed writes, not raw bytes) and commits it with an IntentSender for the result. The sessions live in the
 * installer's own process and stage under its cache directory. Commit checks the APKs as the platform does (one
 * base, every split of the same package, version and signers), refuses to replace the Play packages Westlake pins,
 * a downgrade or an update with other signers, and publishes the package for every Westlake process
 * (InstalledPackages), then reports STATUS_SUCCESS and sends ACTION_PACKAGE_ADDED. Nothing here isolates anything:
 * all Westlake processes share one OS uid. ADR-0006 moves the sessions and the installed packages behind a trusted
 * service with the user's confirmation.
 */
public final class InstallerSessions {
    private static final String TAG = "[WL-INSTALL] ";
    /** PackageInstallerSession's name for the installer's app metadata (PersistableBundle). */
    private static final String APP_METADATA = "app.metadata";
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
                // What the API defines for an installer whose pre-approval request is not available.
                send((IntentSender) args[1], session, PackageInstaller.STATUS_FAILURE_BLOCKED,
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
            case "openWriteAppMetadata":
                // The client writes through a FileBridge here too (PackageInstaller.Session.setAppMetadata).
                return openWrite(session, APP_METADATA, 0, -1);
            case "removeAppMetadata":
                staged(session, APP_METADATA).delete();
                return null;
            case "getAppMetadataFd": {
                File metadata = staged(session, APP_METADATA);
                return metadata.isFile()
                        ? ParcelFileDescriptor.open(metadata, ParcelFileDescriptor.MODE_READ_ONLY) : null;
            }
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
        Verified verified = verify(session);
        System.err.println(TAG + "session " + session.id + " commit (transferred=" + forTransferred + "): "
                + verified.summary);
        if (verified.problem == null) publish(session, verified);
        session.finished = true;
        boolean installed = verified.problem == null;
        if (installed) {
            System.err.println(TAG + "session " + session.id + " installed " + verified.packageName + " v"
                    + verified.versionCode + (verified.replacing ? " (update)" : ""));
            send(statusReceiver, session, PackageInstaller.STATUS_SUCCESS, "INSTALL_SUCCEEDED");
            packageAdded(verified);
        } else {
            System.err.println(TAG + "session " + session.id + " not installed: " + verified.problem);
            send(statusReceiver, session, verified.status, verified.problem);
        }
        for (IPackageInstallerCallback callback : CALLBACKS) {
            try {
                callback.onSessionFinished(session.id, installed);
            } catch (RemoteException ignored) {
            }
        }
    }

    /** What commit found in the staging directory, and what stops it from being installed. */
    private static final class Verified {
        final StringBuilder summary = new StringBuilder();
        String problem;
        int status = PackageInstaller.STATUS_FAILURE_INVALID;
        String packageName;
        long versionCode;
        String signers;
        File base;
        File dexMetadata;
        boolean replacing;
        final List<File> splits = new ArrayList<>();
        final List<String> splitNames = new ArrayList<>();

        void fail(int status, String problem) {
            if (this.problem == null) {
                this.status = status;
                this.problem = problem;
            }
        }
    }

    /**
     * Checks the staged files as the platform does before installing: one base APK, every split of the same package
     * and version, every APK signed by the same signers (APK signature scheme v1 to v3). One clause per file in the
     * summary: package, version, split, size, digest, signers.
     */
    private static Verified verify(Session session) {
        Verified out = new Verified();
        File[] files = session.dir.listFiles();
        if (files == null || files.length == 0) {
            out.fail(PackageInstaller.STATUS_FAILURE_INVALID, "nothing staged");
            out.summary.append("nothing staged");
            return out;
        }
        java.util.Arrays.sort(files);
        for (File file : files) {
            if (out.summary.length() > 0) out.summary.append("; ");
            out.summary.append(file.getName()).append(' ').append(file.length()).append(" bytes sha256 ")
                    .append(digest(file));
            if (file.getName().equals(APP_METADATA)) continue;
            if (file.getName().endsWith(".dm")) {   // dex metadata, not an APK; splits often have no .apk extension
                out.dexMetadata = file;
                continue;
            }
            ParseResult<ApkLite> lite = ApkLiteParseUtils.parseApkLite(
                    ParseTypeImpl.forParsingWithoutPlatformCompat().reset(), file, 0);
            if (lite.isError()) {
                out.summary.append(" unparsed (").append(lite.getErrorMessage()).append(')');
                out.fail(PackageInstaller.STATUS_FAILURE_INVALID, file.getName() + " is not a valid APK: "
                        + lite.getErrorMessage());
                continue;
            }
            ApkLite apk = lite.getResult();
            out.summary.append(' ').append(apk.getPackageName()).append(" v").append(apk.getLongVersionCode());
            if (apk.getSplitName() != null) out.summary.append(" split ").append(apk.getSplitName());
            ParseResult<SigningDetails> signing = ApkSignatureVerifier.verify(
                    ParseTypeImpl.forParsingWithoutPlatformCompat().reset(), file.getPath(),
                    SigningDetails.SignatureSchemeVersion.JAR);
            String signers;
            if (signing.isError()) {
                out.summary.append(" signature unverified (").append(signing.getErrorMessage()).append(')');
                out.fail(PackageInstaller.STATUS_FAILURE_INVALID, file.getName() + ": no verified signature ("
                        + signing.getErrorMessage() + ")");
                continue;
            }
            List<String> digests = new ArrayList<>();
            for (Signature signer : signing.getResult().getSignatures()) {
                digests.add(sha256(signer.toByteArray()));
            }
            java.util.Collections.sort(digests);
            signers = String.join(",", digests);
            out.summary.append(" signed v").append(signing.getResult().getSignatureSchemeVersion()).append(" by");
            for (String digest : digests) out.summary.append(' ').append(digest, 0, 16);
            if (out.packageName == null) {
                out.packageName = apk.getPackageName();
                out.versionCode = apk.getLongVersionCode();
                out.signers = signers;
            } else if (!out.packageName.equals(apk.getPackageName())
                    || out.versionCode != apk.getLongVersionCode()) {
                out.fail(PackageInstaller.STATUS_FAILURE_INVALID, "split " + file.getName()
                        + " is not of the same package and version");
            } else if (!out.signers.equals(signers)) {
                out.fail(PackageInstaller.STATUS_FAILURE_INVALID, file.getName() + " is signed by other signers");
            }
            if (apk.getSplitName() == null) {
                if (out.base != null) {
                    out.fail(PackageInstaller.STATUS_FAILURE_INVALID, "more than one base APK");
                }
                out.base = file;
            } else {
                out.splits.add(file);
                out.splitNames.add(apk.getSplitName());
            }
        }
        if (out.problem == null && out.base == null) {
            out.fail(PackageInstaller.STATUS_FAILURE_INVALID, "no base APK");
        }
        if (out.problem == null && session.params.appPackageName != null
                && !session.params.appPackageName.equals(out.packageName)) {
            out.fail(PackageInstaller.STATUS_FAILURE_INVALID, "the session is for " + session.params.appPackageName
                    + ", not " + out.packageName);
        }
        return out;
    }

    /** Packages Westlake pins at the versions its framework runs (ADR-0006 decision 6). */
    private static final java.util.Set<String> PINNED = new java.util.HashSet<>(java.util.Arrays.asList(
            "com.google.android.gms", "com.android.vending", "com.google.android.gsf"));

    /**
     * Publishes a verified package under InstalledPackages.DIR, the way Android lays out /data/app/<package>: copied
     * into a new directory, record last, then put in place of any older version in one rename. An update keeps its
     * signers and never goes down in version. Sets the problem when it cannot.
     */
    private static void publish(Session session, Verified verified) {
        String packageName = verified.packageName;
        if (PINNED.contains(packageName)) {
            verified.fail(PackageInstaller.STATUS_FAILURE_BLOCKED, packageName
                    + " is pinned at the version Westlake runs; the Store may not replace it");
            return;
        }
        String installedVersion = InstalledPackages.recorded(packageName, "versionCode");
        String installedSigners = InstalledPackages.recorded(packageName, "signers");
        String firstInstall = InstalledPackages.recorded(packageName, "firstInstallTime");
        if (installedVersion != null) {
            verified.replacing = true;
            if (Long.parseLong(installedVersion) > verified.versionCode) {
                verified.fail(PackageInstaller.STATUS_FAILURE_CONFLICT, "INSTALL_FAILED_VERSION_DOWNGRADE: installed "
                        + installedVersion + ", offered " + verified.versionCode);
                return;
            }
            if (installedSigners != null && !installedSigners.equals(verified.signers)) {
                verified.fail(PackageInstaller.STATUS_FAILURE_CONFLICT,
                        "INSTALL_FAILED_UPDATE_INCOMPATIBLE: signed by other signers than the installed version");
                return;
            }
        }
        File root = InstalledPackages.DIR;
        File staging = new File(root, ".session-" + session.id);
        try {
            if (!root.isDirectory() && !root.mkdirs()) throw new IOException("cannot create " + root);
            deleteTree(staging);
            if (!staging.mkdirs()) throw new IOException("cannot create " + staging);
            place(verified.base, new File(staging, "base.apk"));
            for (int i = 0; i < verified.splits.size(); i++) {
                place(verified.splits.get(i), new File(staging, "split_" + verified.splitNames.get(i) + ".apk"));
            }
            if (verified.dexMetadata != null) place(verified.dexMetadata, new File(staging, "base.dm"));
            // The platform's parser must take the package as installed, or no process could answer for it.
            new android.content.pm.PackageParser().parsePackage(staging, 0);
            long now = System.currentTimeMillis();
            java.util.Properties record = new java.util.Properties();
            record.setProperty("packageName", packageName);
            record.setProperty("versionCode", Long.toString(verified.versionCode));
            record.setProperty("signers", verified.signers);
            record.setProperty("installer", String.valueOf(session.installer));
            record.setProperty("installReason", Integer.toString(session.params.installReason));
            record.setProperty("packageSource", Integer.toString(session.params.packageSource));
            record.setProperty("sessionId", Integer.toString(session.id));
            record.setProperty("firstInstallTime", firstInstall != null ? firstInstall : Long.toString(now));
            record.setProperty("lastUpdateTime", Long.toString(now));
            try (FileOutputStream output = new FileOutputStream(new File(staging, InstalledPackages.RECORD))) {
                record.store(output, "installed by " + session.installer + " through Westlake's session "
                        + session.id);
                output.getFD().sync();
            }
            File target = new File(root, packageName);
            File old = new File(root, ".old-" + session.id);
            if (target.exists() && !target.renameTo(old)) throw new IOException("cannot move the installed version");
            if (!staging.renameTo(target)) {
                if (old.exists()) old.renameTo(target);
                throw new IOException("cannot put " + packageName + " in place");
            }
            deleteTree(old);
        } catch (IOException | android.content.pm.PackageParser.PackageParserException | RuntimeException e) {
            deleteTree(staging);
            int status = e instanceof android.content.pm.PackageParser.PackageParserException
                    ? PackageInstaller.STATUS_FAILURE_INCOMPATIBLE : PackageInstaller.STATUS_FAILURE_STORAGE;
            verified.fail(status, "not published: " + e.getMessage());
        }
    }

    /** A hard link when the two directories share a file system, otherwise a copy; fsynced either way. */
    private static void place(File from, File to) throws IOException {
        try {
            android.system.Os.link(from.getPath(), to.getPath());
            return;
        } catch (android.system.ErrnoException e) {
            // another file system or mount: copy
        }
        try (InputStream input = new FileInputStream(from); FileOutputStream output = new FileOutputStream(to)) {
            byte[] buffer = new byte[256 * 1024];
            for (int n; (n = input.read(buffer)) > 0; ) output.write(buffer, 0, n);
            output.getFD().sync();
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }

    /** ACTION_PACKAGE_ADDED (or REPLACED) for the installed package, as the package manager sends it. */
    private static void packageAdded(Verified verified) {
        Application app = ActivityThread.currentApplication();
        if (app == null) return;
        try {
            int uid = RemotePackages.installedUid(verified.packageName);
            android.net.Uri data = android.net.Uri.fromParts("package", verified.packageName, null);
            Intent added = new Intent(verified.replacing ? Intent.ACTION_PACKAGE_REPLACED : Intent.ACTION_PACKAGE_ADDED,
                    data);
            added.putExtra(Intent.EXTRA_UID, uid);
            if (verified.replacing) added.putExtra(Intent.EXTRA_REPLACING, true);
            app.sendBroadcast(added);
        } catch (RuntimeException e) {
            System.err.println(TAG + "package-added broadcast for " + verified.packageName + " not sent: " + e);
        }
    }

    private static void abandon(Session session) {
        boolean wasFinished = session.finished;
        session.finished = true;
        for (FileBridge bridge : session.bridges) bridge.forceClose();
        File[] files = session.dir.listFiles();
        if (files != null) {
            for (File file : files) file.delete();
        }
        session.dir.delete();
        SESSIONS.remove(session.id);
        if (wasFinished) {
            // As PackageInstallerSession.abandon returns for a session in a terminal state: the Play Store abandons
            // its session after a successful install, which must not read as a second, failed outcome.
            System.err.println(TAG + "session " + session.id + " closed after it finished");
            return;
        }
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
