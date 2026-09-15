package android.app;

import java.util.HashMap;
import java.util.Map;

public final class SystemServiceRegistry {
    public interface ServiceFetcher {
        Object getService(Object context);
    }

    public static final Map<String, Object> SYSTEM_SERVICE_FETCHERS = new HashMap<>();
}
