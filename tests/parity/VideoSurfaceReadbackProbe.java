import android.graphics.ImageFormat;
import android.media.Image;
import android.media.ImageReader;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import java.nio.ByteBuffer;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.TimeUnit;

/** Hashes the visible Y, U and V pixels delivered by an Android decoder across a Surface change. */
public final class VideoSurfaceReadbackProbe {
    private static void hashPlane(MessageDigest digest, Image.Plane plane, int width, int height) {
        ByteBuffer pixels = plane.getBuffer().duplicate();
        int step = plane.getPixelStride();
        int base = pixels.position();
        byte[] row = new byte[1 + (width - 1) * step];
        byte[] packed = new byte[width];
        for (int y = 0; y < height; y++) {
            pixels.position(base + y * plane.getRowStride());
            pixels.get(row);
            if (step == 1) digest.update(row);
            else {
                for (int x = 0; x < width; x++) packed[x] = row[x * step];
                digest.update(packed);
            }
        }
    }
    public static String query(String library, String path) throws Exception {
        if (!library.isEmpty()) System.load(library);
        MediaExtractor media = new MediaExtractor();
        try {
            media.setDataSource(path);
            int track = 0;
            while (track < media.getTrackCount() && !media.getTrackFormat(track)
                    .getString(MediaFormat.KEY_MIME).startsWith("video/")) track++;
            if (track == media.getTrackCount()) throw new IllegalArgumentException("no video track");
            media.selectTrack(track);
            MediaFormat format = media.getTrackFormat(track);
            int width = format.getInteger(MediaFormat.KEY_WIDTH), height = format.getInteger(MediaFormat.KEY_HEIGHT);
            MediaFormat main = MediaFormat.createVideoFormat("video/hevc", width, height);
            main.setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.HEVCProfileMain);
            main.setInteger(MediaFormat.KEY_LEVEL, MediaCodecInfo.CodecProfileLevel.HEVCMainTierLevel3);
            String mainDecoder = new MediaCodecList(MediaCodecList.REGULAR_CODECS).findDecoderForFormat(main);
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            int submitted = 0, before = 0, after = 0;
            long lastPts = Long.MIN_VALUE;
            try (ImageReader first = ImageReader.newInstance(600, 600, ImageFormat.YUV_420_888, 3);
                 ImageReader second = ImageReader.newInstance(600, 900, ImageFormat.YUV_420_888, 3)) {
                MediaCodec codec = MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME));
                try {
                    codec.configure(format, first.getSurface(), null, 0);
                    codec.start();
                    MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
                    boolean inputEnded = false, outputEnded = false;
                    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(40);
                    while (!outputEnded && System.nanoTime() < deadline) {
                        int index = inputEnded ? -1 : codec.dequeueInputBuffer(1000);
                        if (index >= 0) {
                            int size = media.readSampleData(codec.getInputBuffer(index), 0);
                            if (size < 0) {
                                codec.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                                inputEnded = true;
                            } else {
                                int flags = (media.getSampleFlags() & MediaExtractor.SAMPLE_FLAG_SYNC) != 0
                                        ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0;
                                codec.queueInputBuffer(index, 0, size, media.getSampleTime(), flags);
                                media.advance();
                                submitted++;
                            }
                        }
                        int output = codec.dequeueOutputBuffer(info, 1000);
                        if (output < 0) continue;
                        outputEnded = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                        boolean frame = info.size > 0;
                        if (frame && before == 1) codec.releaseOutputBuffer(output, System.nanoTime());
                        else codec.releaseOutputBuffer(output, frame);
                        if (!frame) continue;
                        if (info.presentationTimeUs < lastPts) throw new IllegalStateException("output timestamp order");
                        lastPts = info.presentationTimeUs;
                        ImageReader current = before < 3 ? first : second;
                        Image image = null;
                        for (int attempt = 0; attempt < 100 && image == null; attempt++) {
                            image = current.acquireNextImage();
                            if (image == null) Thread.sleep(2);
                        }
                        if (image == null) throw new IllegalStateException("surface readback timed out");
                        try {
                            if (image.getWidth() != width || image.getHeight() != height)
                                throw new IllegalStateException("unexpected image dimensions");
                            Image.Plane[] planes = image.getPlanes();
                            for (int plane = 0; plane < 3; plane++)
                                hashPlane(digest, planes[plane], plane == 0 ? width : (width + 1) / 2,
                                        plane == 0 ? height : (height + 1) / 2);
                        } finally { image.close(); }
                        if (current == first) {
                            if (++before == 3) codec.setOutputSurface(second.getSurface());
                        } else after++;
                    }
                    if (!outputEnded) throw new IllegalStateException("decoder timed out");
                    codec.stop();
                } finally { codec.release(); }
            }
            StringBuilder hex = new StringBuilder();
            for (byte value : digest.digest()) hex.append(String.format(Locale.ROOT, "%02x", value & 255));
            return "READBACK samples=" + submitted + " first=" + before + " second=" + after
                    + " listedMain=" + mainDecoder + " width=" + width + " height=" + height + " lastPtsUs=" + lastPts + " sha256=" + hex + "\n";
        } finally { media.release(); }
    }
}
