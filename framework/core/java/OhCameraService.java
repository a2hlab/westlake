package adapter.core;

import android.graphics.Rect;
import android.hardware.CameraInfo;
import android.hardware.CameraStatus;
import android.hardware.ICameraService;
import android.hardware.ICameraServiceListener;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.impl.CameraMetadataNative;
import android.hardware.camera2.params.StreamConfiguration;
import android.hardware.camera2.params.StreamConfigurationDuration;
import android.os.ServiceSpecificException;
import android.util.Range;
import android.util.Rational;
import android.util.Size;
import android.util.SizeF;
import java.util.ArrayList;
import java.util.List;

/**
 * "media.camera": Android's camera service for this app's process, describing OH's cameras as OH's camera NDK
 * lists them (camera_natives.c). Westlake had none, so CameraManager listed no camera although the device has
 * two (the D600's lcam001 back and lcam002 front), and every Camera2, CameraX and Camera1 app found none.
 *
 * Each OH camera is an Android camera, "0", "1" in OH's order, described at the LEGACY hardware level: what OH's
 * output profiles say (preview sizes as YUV_420_888 and PRIVATE outputs, photo sizes as JPEG), facing and sensor
 * orientation from OH, and the fixed controls a LEGACY device reports (auto exposure and white balance, no manual
 * sensor, no flash, fixed focus). connectDevice opens the OH camera (OhCameraDevice); Camera1's connect answers
 * ERROR_DISCONNECTED, as for a camera that went away.
 */
public final class OhCameraService {
    private OhCameraService() {}

    private static native String nativeCameraList();

    // HAL pixel formats, as stream configurations name them (StreamConfigurationMap maps them to ImageFormat).
    private static final int HAL_BLOB = 0x21;                    // JPEG
    private static final int HAL_IMPLEMENTATION_DEFINED = 0x22;  // PRIVATE (SurfaceTexture, SurfaceView)
    private static final int HAL_YCBCR_420_888 = 0x23;           // YUV_420_888
    // OH Camera_Format values.
    private static final int OH_YUV_420_SP = 1003;
    private static final int OH_JPEG = 2000;
    private static final long FRAME_NS = 33_333_333L;            // 30 fps

    static final class Camera {
        final String ohId;
        final int position;       // OH: 1 back, 2 front
        final int orientation;
        final List<Size> preview = new ArrayList<>();
        final List<Size> photo = new ArrayList<>();
        CameraMetadataNative characteristics;

        Camera(String ohId, int position, int orientation) {
            this.ohId = ohId;
            this.position = position;
            this.orientation = orientation;
        }

        int facing() {
            return position == 2 ? CameraMetadata.LENS_FACING_FRONT : CameraMetadata.LENS_FACING_BACK;
        }
    }

    private static List<Camera> sCameras;

    static synchronized List<Camera> cameras() {
        if (sCameras != null) return sCameras;
        List<Camera> found = new ArrayList<>();
        String list;
        try {
            list = nativeCameraList();
        } catch (UnsatisfiedLinkError e) {
            System.err.println("[WL-CAMERA] no camera natives: " + e.getMessage());
            list = "";
        }
        for (String line : list.split("\n")) {
            String[] f = line.split("\t", -1);
            if (f.length < 5 || f[0].isEmpty()) continue;
            Camera c = new Camera(f[0], parse(f[1]), parse(f[2]));
            sizes(f[3], c.preview, OH_YUV_420_SP);
            sizes(f[4], c.photo, OH_JPEG);
            if (c.preview.isEmpty()) continue;   // nothing to show: not an Android camera
            found.add(c);
        }
        System.err.println("[WL-CAMERA] " + found.size() + " camera(s) from OH");
        sCameras = found;
        return found;
    }

    private static int parse(String s) {
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private static void sizes(String field, List<Size> into, int format) {
        for (String p : field.split(",")) {
            String[] q = p.split(":");
            if (q.length != 3 || parse(q[0]) != format) continue;
            Size s = new Size(parse(q[1]), parse(q[2]));
            if (s.getWidth() > 0 && s.getHeight() > 0 && !into.contains(s)) into.add(s);
        }
    }

    /**
     * The camera features this device has, as PackageManager reports them: android.hardware.camera for a back camera,
     * camera.front for a front one, camera.any for either, from the cameras the service lists (so the two always
     * agree: CameraX compares them). No autofocus, flash or manual sensor: the LEGACY description says none.
     */
    public static synchronized List<String> claimedFeatures() {
        List<String> features = new ArrayList<>();
        boolean back = false, front = false;
        for (Camera c : cameras()) {
            if (c.position == 2) front = true;
            else back = true;
        }
        if (back) features.add("android.hardware.camera");
        if (front) features.add("android.hardware.camera.front");
        if (back || front) features.add("android.hardware.camera.any");
        return features;
    }

    static Camera camera(String id) {
        List<Camera> all = cameras();
        int index = parse(id);
        if (!String.valueOf(index).equals(id) || index < 0 || index >= all.size()) {
            throw new ServiceSpecificException(ICameraService.ERROR_ILLEGAL_ARGUMENT, "Unknown camera ID " + id);
        }
        return all.get(index);
    }

    /** The service's answers, by AIDL method (LocalServiceBinders' proxy). */
    static Object answer(String method, Object[] args) {
        switch (method) {
            case "getNumberOfCameras":
                return cameras().size();
            case "getCameraInfo": {
                Camera c = camera(String.valueOf((Integer) args[0]));
                CameraInfo info = new CameraInfo();
                info.info.facing = c.position == 2 ? android.hardware.Camera.CameraInfo.CAMERA_FACING_FRONT
                        : android.hardware.Camera.CameraInfo.CAMERA_FACING_BACK;
                info.info.orientation = c.orientation;
                return info;
            }
            case "addListener": {
                List<Camera> all = cameras();
                CameraStatus[] status = new CameraStatus[all.size()];
                for (int i = 0; i < status.length; i++) {
                    status[i] = new CameraStatus();
                    status[i].cameraId = String.valueOf(i);
                    status[i].status = ICameraServiceListener.STATUS_PRESENT;
                    status[i].unavailablePhysicalCameras = new String[0];
                }
                return status;
            }
            case "getCameraCharacteristics":
                return new CameraMetadataNative(characteristics(camera((String) args[0])));
            case "getConcurrentCameraIds":
                return new android.hardware.camera2.utils.ConcurrentCameraIdCombination[0];
            case "supportsCameraApi":
                return Boolean.TRUE;
            case "isHiddenPhysicalCamera":
                return Boolean.FALSE;
            case "connectDevice": {
                Camera c = camera((String) args[1]);
                return OhCameraDevice.open(c, characteristics(c),
                        (android.hardware.camera2.ICameraDeviceCallbacks) args[0]);
            }
            case "connect":   // Camera1 (android.hardware.Camera.open): not carried to OH yet
                throw new ServiceSpecificException(ICameraService.ERROR_DISCONNECTED,
                        "Camera1 is not carried to OH yet");
            default:
                return LocalServiceBinders.DEFAULT;
        }
    }

    private static synchronized CameraMetadataNative characteristics(Camera c) {
        if (c.characteristics != null) return c.characteristics;
        CameraMetadataNative m = new CameraMetadataNative();
        m.set(CameraCharacteristics.LENS_FACING, c.facing());
        m.set(CameraCharacteristics.SENSOR_ORIENTATION, c.orientation);
        m.set(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL,
                CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY);
        m.set(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES,
                new int[] {CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE});

        List<StreamConfiguration> configurations = new ArrayList<>();
        List<StreamConfigurationDuration> minimum = new ArrayList<>();
        List<StreamConfigurationDuration> stall = new ArrayList<>();
        Size largest = new Size(0, 0);
        for (Size s : c.preview) {
            for (int format : new int[] {HAL_IMPLEMENTATION_DEFINED, HAL_YCBCR_420_888}) {
                configurations.add(new StreamConfiguration(format, s.getWidth(), s.getHeight(), false));
                minimum.add(new StreamConfigurationDuration(format, s.getWidth(), s.getHeight(), FRAME_NS));
                stall.add(new StreamConfigurationDuration(format, s.getWidth(), s.getHeight(), 0));
            }
            if ((long) s.getWidth() * s.getHeight() > (long) largest.getWidth() * largest.getHeight()) largest = s;
        }
        List<Size> jpeg = c.photo.isEmpty() ? c.preview : c.photo;
        for (Size s : jpeg) {
            configurations.add(new StreamConfiguration(HAL_BLOB, s.getWidth(), s.getHeight(), false));
            minimum.add(new StreamConfigurationDuration(HAL_BLOB, s.getWidth(), s.getHeight(), FRAME_NS));
            stall.add(new StreamConfigurationDuration(HAL_BLOB, s.getWidth(), s.getHeight(), FRAME_NS));
            if ((long) s.getWidth() * s.getHeight() > (long) largest.getWidth() * largest.getHeight()) largest = s;
        }
        m.set(CameraCharacteristics.SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                configurations.toArray(new StreamConfiguration[0]));
        m.set(CameraCharacteristics.SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                minimum.toArray(new StreamConfigurationDuration[0]));
        m.set(CameraCharacteristics.SCALER_AVAILABLE_STALL_DURATIONS,
                stall.toArray(new StreamConfigurationDuration[0]));

        Rect active = new Rect(0, 0, largest.getWidth(), largest.getHeight());
        m.set(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE, active);
        m.set(CameraCharacteristics.SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, active);
        m.set(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE, largest);
        m.set(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE, new SizeF(4.8f, 3.6f));
        m.set(CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE,
                CameraMetadata.SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN);
        m.set(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS, new float[] {3.5f});
        m.set(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE, 0f);   // fixed focus
        m.set(CameraCharacteristics.LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                new int[] {CameraMetadata.LENS_OPTICAL_STABILIZATION_MODE_OFF});

        m.set(CameraCharacteristics.CONTROL_AVAILABLE_MODES, new int[] {CameraMetadata.CONTROL_MODE_AUTO});
        m.set(CameraCharacteristics.CONTROL_AE_AVAILABLE_MODES, new int[] {CameraMetadata.CONTROL_AE_MODE_ON});
        m.set(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES, new int[] {CameraMetadata.CONTROL_AF_MODE_OFF});
        m.set(CameraCharacteristics.CONTROL_AWB_AVAILABLE_MODES, new int[] {CameraMetadata.CONTROL_AWB_MODE_AUTO});
        m.set(CameraCharacteristics.CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
                new int[] {CameraMetadata.CONTROL_AE_ANTIBANDING_MODE_AUTO});
        m.set(CameraCharacteristics.CONTROL_AVAILABLE_EFFECTS, new int[] {CameraMetadata.CONTROL_EFFECT_MODE_OFF});
        m.set(CameraCharacteristics.CONTROL_AVAILABLE_SCENE_MODES,
                new int[] {CameraMetadata.CONTROL_SCENE_MODE_DISABLED});
        m.set(CameraCharacteristics.CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
                new int[] {CameraMetadata.CONTROL_VIDEO_STABILIZATION_MODE_OFF});
        m.set(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
                new Range[] {new Range<>(15, 30), new Range<>(30, 30)});
        m.set(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE, new Range<>(0, 0));
        m.set(CameraCharacteristics.CONTROL_AE_COMPENSATION_STEP, new Rational(1, 1));
        m.set(CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE, false);
        m.set(CameraCharacteristics.CONTROL_AWB_LOCK_AVAILABLE, false);
        m.set(CameraCharacteristics.CONTROL_MAX_REGIONS, new int[] {0, 0, 0});
        m.set(CameraCharacteristics.FLASH_INFO_AVAILABLE, false);
        m.set(CameraCharacteristics.JPEG_AVAILABLE_THUMBNAIL_SIZES, new Size[] {new Size(0, 0), new Size(160, 120)});
        m.set(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, 1f);
        m.set(CameraCharacteristics.SCALER_CROPPING_TYPE, CameraMetadata.SCALER_CROPPING_TYPE_CENTER_ONLY);
        m.set(CameraCharacteristics.STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
                new int[] {CameraMetadata.STATISTICS_FACE_DETECT_MODE_OFF});
        m.set(CameraCharacteristics.STATISTICS_INFO_MAX_FACE_COUNT, 0);
        m.set(CameraCharacteristics.NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
                new int[] {CameraMetadata.NOISE_REDUCTION_MODE_OFF});
        m.set(CameraCharacteristics.EDGE_AVAILABLE_EDGE_MODES, new int[] {CameraMetadata.EDGE_MODE_OFF});
        m.set(CameraCharacteristics.COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
                new int[] {CameraMetadata.COLOR_CORRECTION_ABERRATION_MODE_OFF});
        m.set(CameraCharacteristics.SYNC_MAX_LATENCY, CameraMetadata.SYNC_MAX_LATENCY_UNKNOWN);
        m.set(CameraCharacteristics.REQUEST_PARTIAL_RESULT_COUNT, 1);
        m.set(CameraCharacteristics.REQUEST_PIPELINE_MAX_DEPTH, (byte) 4);
        m.set(CameraCharacteristics.REQUEST_MAX_NUM_OUTPUT_STREAMS, new int[] {0, 3, 1});

        // The keys apps may set and read, by tag, as a HAL lists them.
        m.set(CameraCharacteristics.REQUEST_AVAILABLE_REQUEST_KEYS, tags(REQUEST_KEYS));
        m.set(CameraCharacteristics.REQUEST_AVAILABLE_RESULT_KEYS, tags(RESULT_KEYS));
        List<String> listed = new ArrayList<>();
        for (CameraCharacteristics.Key<?> key : CHARACTERISTICS_KEYS) listed.add(key.getName());
        listed.add(CameraCharacteristics.REQUEST_AVAILABLE_REQUEST_KEYS.getName());
        listed.add(CameraCharacteristics.REQUEST_AVAILABLE_RESULT_KEYS.getName());
        listed.add(CameraCharacteristics.REQUEST_AVAILABLE_CHARACTERISTICS_KEYS.getName());
        m.set(CameraCharacteristics.REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, tags(listed.toArray(new String[0])));
        System.err.println("[WL-CAMERA] camera " + c.ohId + " facing=" + c.facing() + " orientation=" + c.orientation
                + " preview=" + c.preview + " jpeg=" + jpeg);
        c.characteristics = m;
        return m;
    }

    private static final String[] REQUEST_KEYS = {
            "android.control.mode", "android.control.aeMode", "android.control.afMode", "android.control.awbMode",
            "android.control.aeAntibandingMode", "android.control.aeExposureCompensation", "android.control.aeLock",
            "android.control.awbLock", "android.control.aeTargetFpsRange", "android.control.aePrecaptureTrigger",
            "android.control.afTrigger", "android.control.captureIntent", "android.control.effectMode",
            "android.control.sceneMode", "android.control.videoStabilizationMode", "android.flash.mode",
            "android.jpeg.orientation", "android.jpeg.quality", "android.jpeg.thumbnailQuality",
            "android.jpeg.thumbnailSize", "android.jpeg.gpsCoordinates", "android.jpeg.gpsProcessingMethod",
            "android.jpeg.gpsTimestamp", "android.scaler.cropRegion",
            "android.statistics.faceDetectMode", "android.noiseReduction.mode", "android.edge.mode",
            "android.lens.opticalStabilizationMode",
    };

    private static final String[] RESULT_KEYS = {
            "android.control.mode", "android.control.aeMode", "android.control.afMode", "android.control.awbMode",
            "android.control.aeState", "android.control.afState", "android.control.awbState",
            "android.control.aeLock", "android.control.awbLock", "android.flash.mode", "android.flash.state",
            "android.jpeg.orientation", "android.jpeg.quality", "android.scaler.cropRegion",
            "android.sensor.timestamp", "android.statistics.faceDetectMode", "android.lens.state",
            "android.request.pipelineDepth",
    };

    private static final CameraCharacteristics.Key<?>[] CHARACTERISTICS_KEYS = {
            CameraCharacteristics.LENS_FACING, CameraCharacteristics.SENSOR_ORIENTATION,
            CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL, CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES,
            CameraCharacteristics.SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            CameraCharacteristics.SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
            CameraCharacteristics.SCALER_AVAILABLE_STALL_DURATIONS, CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE,
            CameraCharacteristics.SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
            CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE, CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE,
            CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE, CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
            CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE,
            CameraCharacteristics.LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, CameraCharacteristics.CONTROL_AVAILABLE_MODES,
            CameraCharacteristics.CONTROL_AE_AVAILABLE_MODES, CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES,
            CameraCharacteristics.CONTROL_AWB_AVAILABLE_MODES, CameraCharacteristics.CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
            CameraCharacteristics.CONTROL_AVAILABLE_EFFECTS, CameraCharacteristics.CONTROL_AVAILABLE_SCENE_MODES,
            CameraCharacteristics.CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
            CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE,
            CameraCharacteristics.CONTROL_AE_COMPENSATION_STEP, CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE,
            CameraCharacteristics.CONTROL_AWB_LOCK_AVAILABLE, CameraCharacteristics.CONTROL_MAX_REGIONS,
            CameraCharacteristics.FLASH_INFO_AVAILABLE, CameraCharacteristics.JPEG_AVAILABLE_THUMBNAIL_SIZES,
            CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, CameraCharacteristics.SCALER_CROPPING_TYPE,
            CameraCharacteristics.STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
            CameraCharacteristics.STATISTICS_INFO_MAX_FACE_COUNT,
            CameraCharacteristics.NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
            CameraCharacteristics.EDGE_AVAILABLE_EDGE_MODES,
            CameraCharacteristics.COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, CameraCharacteristics.SYNC_MAX_LATENCY,
            CameraCharacteristics.REQUEST_PARTIAL_RESULT_COUNT, CameraCharacteristics.REQUEST_PIPELINE_MAX_DEPTH,
            CameraCharacteristics.REQUEST_MAX_NUM_OUTPUT_STREAMS,
    };

    /*
     * The native tags of these keys. A synthetic key (one CameraMetadataNative composes from others, such as
     * android.jpeg.gpsLocation) has no tag and is left out, as a HAL's key lists never name one: its lookup threw,
     * and getCameraCharacteristics with it, so the probe and Open Camera saw no camera at all.
     */
    private static int[] tags(String[] keys) {
        int[] out = new int[keys.length];
        int n = 0;
        for (String key : keys) {
            try {
                out[n++] = CameraMetadataNative.getTag(key);
            } catch (IllegalArgumentException e) {
                n--;
                System.err.println("[WL-CAMERA] no native tag for " + key + ": left out of the key lists");
            }
        }
        return java.util.Arrays.copyOf(out, n);
    }
}
