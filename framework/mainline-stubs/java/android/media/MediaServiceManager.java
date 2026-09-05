// android.media.MediaServiceManager — mainline service registry shim.
//
// This class is part of the Android media mainline API surface but is absent from the base
// framework jar used by Westlake.  Keep the Android-side registry behavior intact and delegate
// service publication/lookup to the framework ServiceManager boundary.
package android.media;

import android.os.IBinder;
import android.os.ServiceManager;

public class MediaServiceManager {
    public static final String MEDIA_SESSION_SERVICE = "media_session";
    public static final String MEDIA_TRANSCODING_SERVICE = "media.transcoding";
    public static final String MEDIA_COMMUNICATION_SERVICE = "media_communication";

    public static final class ServiceRegisterer {
        private final String mServiceName;

        public ServiceRegisterer(String serviceName) {
            mServiceName = serviceName;
        }

        public IBinder get() {
            return ServiceManager.getService(mServiceName);
        }

        public IBinder getOrThrow() {
            return get();
        }

        public void register(IBinder service) {
            ServiceManager.addService(mServiceName, service);
        }

        public IBinder tryGet() {
            return get();
        }
    }

    private final ServiceRegisterer mSession = new ServiceRegisterer(MEDIA_SESSION_SERVICE);
    private final ServiceRegisterer mTranscoding =
            new ServiceRegisterer(MEDIA_TRANSCODING_SERVICE);
    private final ServiceRegisterer mCommunication =
            new ServiceRegisterer(MEDIA_COMMUNICATION_SERVICE);

    public MediaServiceManager() {}

    public ServiceRegisterer getMediaSessionServiceRegisterer() {
        return mSession;
    }

    public ServiceRegisterer getMediaTranscodingServiceRegisterer() {
        return mTranscoding;
    }

    public ServiceRegisterer getMediaCommunicationServiceRegisterer() {
        return mCommunication;
    }
}
