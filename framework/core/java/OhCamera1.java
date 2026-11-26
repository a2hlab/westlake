package adapter.core;

import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;
import android.hardware.Camera;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Size;
import android.view.Surface;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

/**
 * android.hardware.Camera (Camera1) over OH's camera framework. Android's Camera1 natives talk to the camera service
 * through libcamera_client, which Westlake does not have; camera_natives.c binds them to these methods instead,
 * which run the same OH capture session as OhCameraDevice (camera_natives.c's device natives):
 * - preview goes to the app's preview display or texture as an OH preview output;
 * - takePicture takes an OH photo into a private JPEG ImageReader and gives the app the JPEG bytes
 *   (CAMERA_MSG_SHUTTER, then CAMERA_MSG_COMPRESSED_IMAGE, as Android posts them);
 * - autoFocus reports success at once (the cameras are described fixed-focus);
 * - parameters are Camera1's flattened "key=value;..." string, from OH's preview and photo sizes.
 * Preview callbacks (YUV frames to the app) are not carried: an app that asks gets none.
 */
public final class OhCamera1 {
    private OhCamera1() {}

    private static final int CAMERA_MSG_SHUTTER = 0x002;
    private static final int CAMERA_MSG_FOCUS = 0x004;
    private static final int CAMERA_MSG_COMPRESSED_IMAGE = 0x100;

    private static final class State {
        final OhCameraService.Camera camera;
        final Object cameraRef;       // the WeakReference Camera.postEventFromNative takes
        final Handler handler;
        long handle;
        Surface preview;
        boolean previewing;
        ImageReader jpeg;
        final Map<String, String> parameters = new LinkedHashMap<>();

        State(OhCameraService.Camera camera, Object cameraRef) {
            this.camera = camera;
            this.cameraRef = cameraRef;
            HandlerThread t = new HandlerThread("wl-camera1-" + camera.ohId);
            t.start();
            handler = new Handler(t.getLooper());
        }
    }

    private static final Map<Long, State> STATES = new HashMap<>();
    private static final AtomicLong NEXT = new AtomicLong(1);
    private static Field sContext;
    private static Method sPost;

    private static Field context() throws ReflectiveOperationException {
        if (sContext == null) {
            sContext = Camera.class.getDeclaredField("mNativeContext");
            sContext.setAccessible(true);
        }
        return sContext;
    }

    private static State state(Object thiz) {
        try {
            synchronized (STATES) {
                return STATES.get(context().getLong(thiz));
            }
        } catch (ReflectiveOperationException e) {
            return null;
        }
    }

    private static void post(State s, int what, int arg1, int arg2, Object obj) {
        try {
            if (sPost == null) {
                sPost = Camera.class.getDeclaredMethod("postEventFromNative", Object.class, int.class, int.class,
                        int.class, Object.class);
                sPost.setAccessible(true);
            }
            sPost.invoke(null, s.cameraRef, what, arg1, arg2, obj);
        } catch (ReflectiveOperationException e) {
            System.err.println("[WL-CAMERA] Camera1 event " + what + " not delivered: " + e);
        }
    }

    // ---- the natives' bodies (camera_natives.c forwards each Camera1 native here) ----------------------------

    public static int getNumberOfCameras() {
        return OhCameraService.cameras().size();
    }

    public static void getCameraInfo(int id, Camera.CameraInfo info) {
        List<OhCameraService.Camera> all = OhCameraService.cameras();
        if (id < 0 || id >= all.size()) throw new RuntimeException("Unknown camera ID");
        OhCameraService.Camera c = all.get(id);
        info.facing = c.position == 2 ? Camera.CameraInfo.CAMERA_FACING_FRONT : Camera.CameraInfo.CAMERA_FACING_BACK;
        info.orientation = c.orientation;
        info.canDisableShutterSound = true;
    }

    /** native_setup: 0, or -EACCES (-13) / -ENODEV (-19) as Camera.open turns into its exceptions. */
    public static int setup(Object thiz, Object cameraRef, int id) {
        List<OhCameraService.Camera> all = OhCameraService.cameras();
        if (id < 0 || id >= all.size()) return -19;
        State s = new State(all.get(id), cameraRef);
        s.handle = OhCameraDevice.nativeOpen(s.camera.ohId);
        if (s.handle == 0) {
            s.handler.getLooper().quitSafely();
            return -13;
        }
        defaults(s);
        long key = NEXT.getAndIncrement();
        try {
            context().setLong(thiz, key);
        } catch (ReflectiveOperationException e) {
            OhCameraDevice.nativeClose(s.handle);
            return -13;
        }
        synchronized (STATES) {
            STATES.put(key, s);
        }
        System.err.println("[WL-CAMERA] Camera1 camera " + s.camera.ohId + " opened");
        return 0;
    }

    public static void release(Object thiz) {
        State s;
        try {
            synchronized (STATES) {
                long key = context().getLong(thiz);
                s = STATES.remove(key);
                context().setLong(thiz, 0);
            }
        } catch (ReflectiveOperationException e) {
            return;
        }
        if (s == null) return;
        if (s.handle != 0) {
            OhCameraDevice.nativeStop(s.handle);
            OhCameraDevice.nativeClose(s.handle);
            s.handle = 0;
        }
        if (s.jpeg != null) s.jpeg.close();
        s.handler.getLooper().quitSafely();
        System.err.println("[WL-CAMERA] Camera1 camera " + s.camera.ohId + " closed");
    }

    public static void setPreviewSurface(Object thiz, Surface surface) {
        State s = state(thiz);
        if (s != null) s.preview = surface;
    }

    public static void setPreviewTexture(Object thiz, SurfaceTexture texture) {
        State s = state(thiz);
        if (s != null) s.preview = texture != null ? new Surface(texture) : null;
    }

    public static void startPreview(Object thiz) {
        State s = state(thiz);
        if (s == null) throw new RuntimeException("Camera is released");
        if (s.previewing) return;
        Size preview = size(s.parameters.get("preview-size"));
        Size picture = size(s.parameters.get("picture-size"));
        if (s.jpeg == null || s.jpeg.getWidth() != picture.getWidth() || s.jpeg.getHeight() != picture.getHeight()) {
            if (s.jpeg != null) s.jpeg.close();
            s.jpeg = ImageReader.newInstance(picture.getWidth(), picture.getHeight(), ImageFormat.JPEG, 2);
        }
        Surface[] surfaces = s.preview != null ? new Surface[] {s.preview, s.jpeg.getSurface()}
                : new Surface[] {s.jpeg.getSurface()};
        int n = surfaces.length;
        int[] kinds = new int[n], widths = new int[n], heights = new int[n];
        for (int i = 0; i < n; i++) {
            boolean jpeg = surfaces[i] == s.jpeg.getSurface();
            kinds[i] = jpeg ? 1 : 0;
            widths[i] = jpeg ? picture.getWidth() : preview.getWidth();
            heights[i] = jpeg ? picture.getHeight() : preview.getHeight();
        }
        int rc = OhCameraDevice.nativeConfigure(s.handle, surfaces, kinds, widths, heights);
        if (rc == 0) rc = OhCameraDevice.nativeStart(s.handle);
        System.err.println("[WL-CAMERA] Camera1 preview " + preview + " picture " + picture + ": OH " + rc);
        if (rc != 0) throw new RuntimeException("startPreview failed");
        s.previewing = true;
    }

    public static void stopPreview(Object thiz) {
        State s = state(thiz);
        if (s == null || !s.previewing) return;
        OhCameraDevice.nativeStop(s.handle);
        s.previewing = false;
    }

    public static boolean previewEnabled(Object thiz) {
        State s = state(thiz);
        return s != null && s.previewing;
    }

    public static void autoFocus(Object thiz) {
        State s = state(thiz);
        if (s != null) s.handler.post(() -> post(s, CAMERA_MSG_FOCUS, 1, 0, null));
    }

    public static void takePicture(Object thiz, int msgType) {
        final State s = state(thiz);
        if (s == null || !s.previewing) throw new RuntimeException("takePicture failed");
        s.jpeg.setOnImageAvailableListener(reader -> {
            byte[] bytes = null;
            try (Image image = reader.acquireNextImage()) {
                ByteBuffer b = image.getPlanes()[0].getBuffer();
                bytes = new byte[b.remaining()];
                b.get(bytes);
            } catch (RuntimeException e) {
                System.err.println("[WL-CAMERA] Camera1 picture unreadable: " + e);
            }
            System.err.println("[WL-CAMERA] Camera1 picture " + (bytes != null ? bytes.length : 0) + " bytes");
            // Android stops the preview for a picture; the app starts it again.
            OhCameraDevice.nativeStop(s.handle);
            s.previewing = false;
            if ((msgType & CAMERA_MSG_COMPRESSED_IMAGE) != 0) post(s, CAMERA_MSG_COMPRESSED_IMAGE, 0, 0, bytes);
        }, s.handler);
        int rc = OhCameraDevice.nativeCapture(s.handle);
        System.err.println("[WL-CAMERA] Camera1 takePicture: OH " + rc);
        if (rc != 0) throw new RuntimeException("takePicture failed");
        if ((msgType & CAMERA_MSG_SHUTTER) != 0) s.handler.post(() -> post(s, CAMERA_MSG_SHUTTER, 0, 0, null));
    }

    public static void setParameters(Object thiz, String flattened) {
        State s = state(thiz);
        if (s == null || flattened == null) return;
        for (String pair : flattened.split(";")) {
            int eq = pair.indexOf('=');
            if (eq > 0) s.parameters.put(pair.substring(0, eq), pair.substring(eq + 1));
        }
    }

    public static String getParameters(Object thiz) {
        State s = state(thiz);
        if (s == null) return "";
        StringBuilder b = new StringBuilder();
        for (Map.Entry<String, String> e : s.parameters.entrySet()) {
            if (b.length() > 0) b.append(';');
            b.append(e.getKey()).append('=').append(e.getValue());
        }
        return b.toString();
    }

    // ---- parameters --------------------------------------------------------------------------------------

    private static Size size(String value) {
        try {
            String[] p = value.split("x");
            return new Size(Integer.parseInt(p[0]), Integer.parseInt(p[1]));
        } catch (RuntimeException e) {
            return new Size(640, 480);
        }
    }

    private static String sizes(List<Size> list) {
        StringBuilder b = new StringBuilder();
        for (Size s : list) {
            if (b.length() > 0) b.append(',');
            b.append(s.getWidth()).append('x').append(s.getHeight());
        }
        return b.toString();
    }

    private static Size largest(List<Size> list) {
        Size best = list.get(0);
        for (Size s : list) if ((long) s.getWidth() * s.getHeight() > (long) best.getWidth() * best.getHeight()) best = s;
        return best;
    }

    private static void defaults(State s) {
        List<Size> preview = s.camera.preview;
        List<Size> picture = s.camera.photo.isEmpty() ? preview : s.camera.photo;
        Map<String, String> p = s.parameters;
        p.put("preview-size", sizes(java.util.Collections.singletonList(largest(preview))));
        p.put("preview-size-values", sizes(preview));
        p.put("preview-format", "yuv420sp");
        p.put("preview-format-values", "yuv420sp");
        p.put("preview-frame-rate", "30");
        p.put("preview-frame-rate-values", "15,30");
        p.put("preview-fps-range", "15000,30000");
        p.put("preview-fps-range-values", "(15000,30000),(30000,30000)");
        p.put("picture-size", sizes(java.util.Collections.singletonList(largest(picture))));
        p.put("picture-size-values", sizes(picture));
        p.put("picture-format", "jpeg");
        p.put("picture-format-values", "jpeg");
        p.put("jpeg-quality", "95");
        p.put("jpeg-thumbnail-size-values", "0x0,160x120");
        p.put("jpeg-thumbnail-width", "160");
        p.put("jpeg-thumbnail-height", "120");
        p.put("jpeg-thumbnail-quality", "85");
        p.put("rotation", "0");
        p.put("focus-mode", "fixed");
        p.put("focus-mode-values", "fixed");
        p.put("focal-length", "3.5");
        p.put("horizontal-view-angle", "60");
        p.put("vertical-view-angle", "45");
        p.put("focus-distances", "Infinity,Infinity,Infinity");
        p.put("whitebalance", "auto");
        p.put("whitebalance-values", "auto");
        p.put("effect", "none");
        p.put("effect-values", "none");
        p.put("antibanding", "auto");
        p.put("antibanding-values", "auto");
        p.put("scene-mode", "auto");
        p.put("scene-mode-values", "auto");
        p.put("exposure-compensation", "0");
        p.put("max-exposure-compensation", "0");
        p.put("min-exposure-compensation", "0");
        p.put("exposure-compensation-step", "1");
        p.put("zoom-supported", "false");
        p.put("smooth-zoom-supported", "false");
        p.put("video-stabilization-supported", "false");
        p.put("auto-exposure-lock-supported", "false");
        p.put("auto-whitebalance-lock-supported", "false");
        p.put("max-num-focus-areas", "0");
        p.put("max-num-metering-areas", "0");
        p.put("max-num-detected-faces-hw", "0");
        p.put("max-num-detected-faces-sw", "0");
        p.put("recording-hint", "false");
        p.put("video-snapshot-supported", "false");
    }
}
