package adapter.activity;

import android.os.SystemClock;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.UUID;
import org.json.JSONObject;

/** Opt-in development-host handoff. The registered HAP retains the OH ability token. */
final class RemoteHostWindows {
    private RemoteHostWindows() {}

    static boolean launch(String targetPackage) { return request(targetPackage, "launch"); }
    static void returnTo(String callerPackage) { request(callerPackage, "result"); }

    static boolean isOwnAppSettings(android.content.Intent intent) {
        if (!android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS.equals(intent.getAction())
                || intent.getComponent() != null || intent.getPackage() != null || intent.getSelector() != null
                || System.getenv("WESTLAKE_OWN_SETTINGS_DIR") == null) return false;
        android.net.Uri data = intent.getData();
        String own = adapter.packagemanager.SourcePackageRegistry.packageName();
        return own != null && data != null && ("package:" + own).equals(data.toString());
    }

    static boolean openOwnAppSettings() {
        return send(System.getenv("WESTLAKE_OWN_SETTINGS_DIR"), "app-settings", "app-settings");
    }

    private static boolean request(String packageName, String reason) {
        if (!"1".equals(System.getenv("WESTLAKE_REMOTE_HOST_WINDOWS"))) return true;
        if (!"1".equals(System.getenv("ASX_DIRECT_LAUNCH"))) return false;
        if (packageName.equals(android.app.ActivityThread.currentPackageName())) return true;
        String routes = System.getenv("WESTLAKE_REMOTE_HOST_ROUTES");
        String target = null;
        if (routes != null) for (String route : routes.split(",")) {
            String[] pair = route.split(":", -1);
            if (pair.length == 2 && pair[0].equals(packageName)) {
                if (target != null) return false;
                target = pair[1];
            }
        }
        String directory = System.getenv("WESTLAKE_HOST_CONTROL_DIR");
        if (directory == null || !validTarget(target, packageName, reason)) {
            event(reason, "unconfigured");
            return false;
        }
        return send(directory, target, reason);
    }

    static boolean validTarget(String target, String packageName, String reason) {
        if (target == null) return false;
        if (target.matches("org\\.westlake\\.[A-Za-z0-9_]+")) return true;
        // A dedicated result route must describe this exact Android caller.
        // The native service host separately checks who opened its current window.
        return "result".equals(reason) && target.equals("org.westlake.apk." + packageName)
                && packageName.matches("[A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z][A-Za-z0-9_]*)+");
    }

    private static boolean send(String directory, String target, String reason) {
        // No token, account, Intent or result payload is written here. Each request has
        // its own filename so independent Android processes cannot overwrite one another.
        String id = UUID.randomUUID().toString();
        File root = new File(directory);
        File temporary = new File(root, "temp-" + id);
        File request = new File(root, "request-" + id + ".json");
        File reply = new File(root, "reply-" + id + ".json");
        try {
            String[] pending = root.list();
            if (pending == null || pending.length >= 32) { event(reason, "queue_full"); return false; }
            JSONObject body = new JSONObject().put("id", id).put("target", target)
                    .put("created", System.currentTimeMillis());
            Files.write(temporary.toPath(), body.toString().getBytes(StandardCharsets.UTF_8));
            Files.move(temporary.toPath(), request.toPath(), StandardCopyOption.ATOMIC_MOVE);
            long deadline = SystemClock.uptimeMillis() + 1500;
            while (SystemClock.uptimeMillis() < deadline) {
                if (reply.isFile() && reply.length() <= 1024) {
                    JSONObject result = new JSONObject(new String(Files.readAllBytes(reply.toPath()), StandardCharsets.UTF_8));
                    boolean accepted = id.equals(result.optString("id")) && result.optBoolean("accepted", false);
                    event(reason, accepted ? "accepted" : "denied");
                    return accepted;
                }
                SystemClock.sleep(20);
            }
            event(reason, "timeout");
        } catch (Exception error) { event(reason, "error_" + error.getClass().getSimpleName()); }
        finally { temporary.delete(); request.delete(); reply.delete(); }
        return false;
    }

    private static void event(String reason, String result) {
        System.err.println("[WL-REMOTE-HOST] reason=" + reason + " status=" + result);
    }
}
