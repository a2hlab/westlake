package android.os;

import java.util.HashMap;
import java.util.Map;

public final class ServiceManager {
    public static final Map<String, IBinder> sCache = new HashMap<>();
}
