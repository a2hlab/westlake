package adapter.core;

import android.graphics.ImageFormat;
import android.hardware.ICameraService;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.ICameraDeviceCallbacks;
import android.hardware.camera2.ICameraDeviceUser;
import android.hardware.camera2.impl.CameraMetadataNative;
import android.hardware.camera2.impl.CaptureResultExtras;
import android.hardware.camera2.impl.PhysicalCaptureResultInfo;
import android.hardware.camera2.params.OutputConfiguration;
import android.hardware.camera2.utils.SubmitInfo;
import android.os.Binder;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IInterface;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.util.Range;
import android.util.Size;
import android.view.Surface;
import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * An open camera (ICameraDeviceUser, what CameraManager.openCamera's connectDevice gives the app) over OH's camera
 * framework (camera_natives.c): the app's output streams become outputs of an OH capture session, OH's camera
 * input draws into the app's own Surfaces (OH preview outputs on the OH surface behind each Surface, an OH photo
 * output for a JPEG stream), and the app's capture requests start, repeat and stop that session.
 *
 * Results are made here, as a LEGACY device's are: one per frame of a repeating request (at 30 fps) and one per
 * still capture, with the request's own settings, a timestamp, and auto exposure, focus and white balance reported
 * settled. OH gives no per-frame metadata through its NDK.
 */
final class OhCameraDevice {
    static native long nativeOpen(String ohId);
    static native int nativeConfigure(long handle, Surface[] surfaces, int[] kinds, int[] widths, int[] heights);
    static native int nativeStart(long handle);
    static native int nativeStop(long handle);
    static native int nativeCapture(long handle);
    static native void nativeClose(long handle);

    private static final int HAL_BLOB = 0x21;
    private static final long FRAME_MS = 33;

    private final OhCameraService.Camera mCamera;
    private final CameraMetadataNative mCharacteristics;
    private final ICameraDeviceCallbacks mCallbacks;
    private final Handler mHandler;
    private final Map<Integer, OutputConfiguration> mStreams = new LinkedHashMap<>();
    private long mHandle;
    private int mNextStream;
    private int mNextRequest;
    private long mNextFrame;
    private int mRepeatingId = -1;
    private CaptureRequest mRepeating;
    private final List<Surface> mJpegSurfaces = new ArrayList<>();

    private OhCameraDevice(OhCameraService.Camera camera, CameraMetadataNative characteristics,
            ICameraDeviceCallbacks callbacks) {
        mCamera = camera;
        mCharacteristics = characteristics;
        mCallbacks = callbacks;
        mHandle = nativeOpen(camera.ohId);
        if (mHandle == 0) {
            throw new ServiceSpecificException(ICameraService.ERROR_CAMERA_IN_USE, "OH could not open camera " + camera.ohId);
        }
        HandlerThread thread = new HandlerThread("wl-camera-" + camera.ohId);
        thread.start();
        mHandler = new Handler(thread.getLooper());
        System.err.println("[WL-CAMERA] camera " + camera.ohId + " opened");
    }

    /** connectDevice's answer: the device as the app's ICameraDeviceUser (an in-process interface). */
    static ICameraDeviceUser open(OhCameraService.Camera camera, CameraMetadataNative characteristics,
            ICameraDeviceCallbacks callbacks) {
        final OhCameraDevice device = new OhCameraDevice(camera, characteristics, callbacks);
        final Binder binder = new Binder();
        ICameraDeviceUser local = (ICameraDeviceUser) Proxy.newProxyInstance(OhCameraDevice.class.getClassLoader(),
                new Class<?>[] {ICameraDeviceUser.class}, (self, method, args) -> {
                    if ("asBinder".equals(method.getName())) return binder;
                    synchronized (device) {
                        Object value = device.answer(method.getName(), args);
                        if (value == LocalServiceBinders.DEFAULT) {
                            Class<?> type = method.getReturnType();
                            if (type == boolean.class) return false;
                            if (type == int.class) return 0;
                            if (type == long.class) return 0L;
                            return null;
                        }
                        return value;
                    }
                });
        binder.attachInterface((IInterface) local, "android.hardware.camera2.ICameraDeviceUser");
        return local;
    }

    private Object answer(String method, Object[] args) {
        switch (method) {
            case "beginConfigure":
            case "tearDown":
            case "updateOutputConfiguration":
            case "finalizeOutputConfigurations":
            case "setCameraAudioRestriction":
                return LocalServiceBinders.DEFAULT;
            case "createStream": {
                int id = mNextStream++;
                mStreams.put(id, (OutputConfiguration) args[0]);
                return id;
            }
            case "deleteStream":
                mStreams.remove((Integer) args[0]);
                return LocalServiceBinders.DEFAULT;
            case "endConfigure":
                configure();
                return new int[0];
            case "isSessionConfigurationSupported":
                return Boolean.TRUE;
            case "createDefaultRequest":
                return defaultRequest((Integer) args[0]);
            case "getCameraInfo":
                return new CameraMetadataNative(mCharacteristics);
            case "submitRequest":
                return submit(new CaptureRequest[] {(CaptureRequest) args[0]}, (Boolean) args[1]);
            case "submitRequestList":
                return submit((CaptureRequest[]) args[0], (Boolean) args[1]);
            case "cancelRequest":
                return cancel((Integer) args[0]);
            case "flush":
            case "waitUntilIdle": {
                long last = stopRepeating();
                idle();
                return "flush".equals(method) ? last : LocalServiceBinders.DEFAULT;
            }
            case "prepare":
            case "prepare2": {
                final int stream = (Integer) args[args.length - 1];
                mHandler.post(() -> {
                    try {
                        mCallbacks.onPrepared(stream);
                    } catch (RemoteException ignored) {
                        // the app went away
                    }
                });
                return LocalServiceBinders.DEFAULT;
            }
            case "disconnect":
                close();
                return LocalServiceBinders.DEFAULT;
            case "createInputStream":
            case "getInputSurface":
            case "switchToOffline":
                throw new ServiceSpecificException(ICameraService.ERROR_ILLEGAL_ARGUMENT, method + " is not supported");
            default:
                return LocalServiceBinders.DEFAULT;
        }
    }

    private static int configuredFormat(OutputConfiguration configuration) {
        try {
            Field f = OutputConfiguration.class.getDeclaredField("mConfiguredFormat");
            f.setAccessible(true);
            return f.getInt(configuration);
        } catch (ReflectiveOperationException e) {
            return 0;
        }
    }

    private void configure() {
        List<Surface> surfaces = new ArrayList<>();
        List<int[]> shapes = new ArrayList<>();
        mJpegSurfaces.clear();
        for (OutputConfiguration configuration : mStreams.values()) {
            int format = configuredFormat(configuration);
            boolean jpeg = format == HAL_BLOB || format == ImageFormat.JPEG;
            Size size = configuration.getConfiguredSize();
            for (Surface surface : configuration.getSurfaces()) {
                if (surface == null || size == null) continue;
                surfaces.add(surface);
                shapes.add(new int[] {jpeg ? 1 : 0, size.getWidth(), size.getHeight()});
                if (jpeg) mJpegSurfaces.add(surface);
            }
        }
        int n = surfaces.size();
        int[] kinds = new int[n], widths = new int[n], heights = new int[n];
        for (int i = 0; i < n; i++) {
            kinds[i] = shapes.get(i)[0];
            widths[i] = shapes.get(i)[1];
            heights[i] = shapes.get(i)[2];
        }
        int rc = nativeConfigure(mHandle, surfaces.toArray(new Surface[0]), kinds, widths, heights);
        System.err.println("[WL-CAMERA] camera " + mCamera.ohId + ": " + n + " output(s) configured, OH " + rc);
        if (rc != 0) {
            throw new ServiceSpecificException(ICameraService.ERROR_ILLEGAL_ARGUMENT,
                    "OH refused the session's outputs (" + rc + ")");
        }
    }

    private CameraMetadataNative defaultRequest(int template) {
        CameraMetadataNative m = new CameraMetadataNative();
        int intent;
        switch (template) {
            case ICameraDeviceUser.TEMPLATE_STILL_CAPTURE: intent = CameraMetadata.CONTROL_CAPTURE_INTENT_STILL_CAPTURE; break;
            case ICameraDeviceUser.TEMPLATE_RECORD: intent = CameraMetadata.CONTROL_CAPTURE_INTENT_VIDEO_RECORD; break;
            case ICameraDeviceUser.TEMPLATE_VIDEO_SNAPSHOT: intent = CameraMetadata.CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT; break;
            case ICameraDeviceUser.TEMPLATE_PREVIEW: intent = CameraMetadata.CONTROL_CAPTURE_INTENT_PREVIEW; break;
            default:
                throw new ServiceSpecificException(ICameraService.ERROR_ILLEGAL_ARGUMENT, "Unsupported template " + template);
        }
        m.set(CaptureRequest.CONTROL_CAPTURE_INTENT, intent);
        m.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO);
        m.set(CaptureRequest.CONTROL_AE_MODE, CameraMetadata.CONTROL_AE_MODE_ON);
        m.set(CaptureRequest.CONTROL_AF_MODE, CameraMetadata.CONTROL_AF_MODE_OFF);
        m.set(CaptureRequest.CONTROL_AWB_MODE, CameraMetadata.CONTROL_AWB_MODE_AUTO);
        m.set(CaptureRequest.CONTROL_AE_ANTIBANDING_MODE, CameraMetadata.CONTROL_AE_ANTIBANDING_MODE_AUTO);
        m.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, 0);
        m.set(CaptureRequest.CONTROL_AE_LOCK, false);
        m.set(CaptureRequest.CONTROL_AWB_LOCK, false);
        m.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, new Range<>(15, 30));
        m.set(CaptureRequest.CONTROL_AE_PRECAPTURE_TRIGGER, CameraMetadata.CONTROL_AE_PRECAPTURE_TRIGGER_IDLE);
        m.set(CaptureRequest.CONTROL_AF_TRIGGER, CameraMetadata.CONTROL_AF_TRIGGER_IDLE);
        m.set(CaptureRequest.CONTROL_EFFECT_MODE, CameraMetadata.CONTROL_EFFECT_MODE_OFF);
        m.set(CaptureRequest.CONTROL_SCENE_MODE, CameraMetadata.CONTROL_SCENE_MODE_DISABLED);
        m.set(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CameraMetadata.CONTROL_VIDEO_STABILIZATION_MODE_OFF);
        m.set(CaptureRequest.FLASH_MODE, CameraMetadata.FLASH_MODE_OFF);
        m.set(CaptureRequest.JPEG_ORIENTATION, 0);
        m.set(CaptureRequest.JPEG_QUALITY, (byte) 95);
        m.set(CaptureRequest.JPEG_THUMBNAIL_QUALITY, (byte) 85);
        m.set(CaptureRequest.JPEG_THUMBNAIL_SIZE, new Size(160, 120));
        m.set(CaptureRequest.SCALER_CROP_REGION, mCharacteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE));
        m.set(CaptureRequest.STATISTICS_FACE_DETECT_MODE, CameraMetadata.STATISTICS_FACE_DETECT_MODE_OFF);
        m.set(CaptureRequest.NOISE_REDUCTION_MODE, CameraMetadata.NOISE_REDUCTION_MODE_OFF);
        m.set(CaptureRequest.EDGE_MODE, CameraMetadata.EDGE_MODE_OFF);
        m.set(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CameraMetadata.LENS_OPTICAL_STABILIZATION_MODE_OFF);
        return m;
    }

    private SubmitInfo submit(CaptureRequest[] requests, boolean streaming) {
        int id = mNextRequest++;
        long lastFrame = ICameraDeviceUser.NO_IN_FLIGHT_REPEATING_FRAMES;
        if (streaming) {
            lastFrame = stopRepeating();
            mRepeatingId = id;
            mRepeating = requests[requests.length - 1];
            int rc = nativeStart(mHandle);
            if (rc != 0) throw new ServiceSpecificException(ICameraService.ERROR_DISCONNECTED, "OH did not start (" + rc + ")");
            mHandler.post(new Runnable() {
                @Override public void run() {
                    synchronized (OhCameraDevice.this) {
                        if (mRepeatingId != id || mHandle == 0) return;
                        deliver(id, mRepeating);
                    }
                    mHandler.postDelayed(this, FRAME_MS);
                }
            });
        } else {
            for (final CaptureRequest request : requests) {
                boolean still = false;
                Collection<Surface> targets = request.getTargets();
                for (Surface target : targets) if (mJpegSurfaces.contains(target)) still = true;
                if (still) {
                    int rc = nativeCapture(mHandle);
                    System.err.println("[WL-CAMERA] still capture: OH " + rc);
                }
                mHandler.postDelayed(() -> {
                    synchronized (OhCameraDevice.this) {
                        if (mHandle != 0) deliver(id, request);
                    }
                }, still ? 300 : FRAME_MS);
            }
        }
        return new SubmitInfo(id, lastFrame);
    }

    /** One frame's start and result, as the request asked for it. */
    private void deliver(int requestId, CaptureRequest request) {
        long frame = mNextFrame++;
        long timestamp = System.nanoTime();
        CaptureResultExtras extras = new CaptureResultExtras(requestId, 0, 0, 0, frame, 1, -1, null,
                frame - 1, -1, -1, false, 0);
        CameraMetadataNative result = new CameraMetadataNative(request.getNativeCopy());
        result.set(CaptureResult.SENSOR_TIMESTAMP, timestamp);
        result.set(CaptureResult.CONTROL_AE_STATE, CameraMetadata.CONTROL_AE_STATE_CONVERGED);
        result.set(CaptureResult.CONTROL_AF_STATE, CameraMetadata.CONTROL_AF_STATE_INACTIVE);
        result.set(CaptureResult.CONTROL_AWB_STATE, CameraMetadata.CONTROL_AWB_STATE_CONVERGED);
        result.set(CaptureResult.FLASH_STATE, CameraMetadata.FLASH_STATE_UNAVAILABLE);
        result.set(CaptureResult.LENS_STATE, CameraMetadata.LENS_STATE_STATIONARY);
        result.set(CaptureResult.REQUEST_PIPELINE_DEPTH, (byte) 1);
        try {
            mCallbacks.onCaptureStarted(extras, timestamp);
            mCallbacks.onResultReceived(result, extras, new PhysicalCaptureResultInfo[0]);
        } catch (RemoteException ignored) {
            // the app went away
        }
    }

    private long stopRepeating() {
        mRepeatingId = -1;
        mRepeating = null;
        return mNextFrame - 1;
    }

    private long cancel(int requestId) {
        if (requestId != mRepeatingId) {
            throw new ServiceSpecificException(ICameraService.ERROR_ILLEGAL_ARGUMENT, "Request " + requestId + " is not repeating");
        }
        long last = stopRepeating();
        idle();
        return last;
    }

    private void idle() {
        mHandler.post(() -> {
            try {
                mCallbacks.onDeviceIdle();
            } catch (RemoteException ignored) {
                // the app went away
            }
        });
    }

    private void close() {
        stopRepeating();
        if (mHandle != 0) {
            nativeStop(mHandle);
            nativeClose(mHandle);
            mHandle = 0;
            System.err.println("[WL-CAMERA] camera " + mCamera.ohId + " closed");
        }
        mHandler.getLooper().quitSafely();
    }
}
