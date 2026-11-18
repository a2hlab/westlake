package adapter.activity;

public final class RemoteHostWindowRouteTest {
    private static void check(boolean expected, String host, String app, String direction) {
        if (RemoteHostWindows.validTarget(host, app, direction) != expected) {
            throw new AssertionError(direction + " " + app + " " + host);
        }
    }

    public static void main(String[] args) {
        String reader = "org.example.reader";
        String own = "org.westlake.apk." + reader;
        check(true, own, reader, "result");
        check(false, own, reader, "launch");
        check(false, own, "org.example.maps", "result");
        check(false, "org.example.reader", reader, "result");
        check(false, "org.westlake.apk.", "", "result");
        check(false, null, reader, "result");
        check(true, "org.westlake.authservicehost", "org.example.service", "launch");
        check(true, "org.westlake.authclienthost", reader, "result");
        System.out.println("REMOTE_HOST_WINDOW_ROUTES_PASS");
    }
}
