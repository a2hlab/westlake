import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;

import android.media.MediaCodec;
import android.media.MediaExtractor;
import java.nio.ByteBuffer;
import java.util.zip.CRC32;
import static android.media.MediaCodecInfo.CodecProfileLevel.*;

/** Runs in an Android test process. Fixture directory and explicit shim loading are optional. */
public final class VideoDecoderProfileProbe {
    public static void main(String[] args) {
        System.out.print(query(args.length > 0 ? args[0] : "", args.length > 1 ? args[1] : null));
    }

    public static String query(String library, String fixtures) {
        if (!library.isEmpty()) System.load(library);
        StringBuilder result = new StringBuilder();
        for (MediaCodecInfo codec : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()) {
            if (codec.isEncoder() || !codec.getName().startsWith("OH.video.")) continue;
            for (String mime : codec.getSupportedTypes()) {
                boolean avc = "video/avc".equals(mime);
                if (!avc && !"video/hevc".equals(mime)) continue;
                MediaCodecInfo.CodecCapabilities caps = codec.getCapabilitiesForType(mime);
                result.append("DECODER ").append(mime).append(" ").append(codec.getName()).append("\n");
                for (MediaCodecInfo.CodecProfileLevel pair : caps.profileLevels) {
                    result.append("PAIR ").append(mime).append(" ").append(pair.profile)
                            .append(" ").append(pair.level).append("\n");
                }
                for (int profile : avc ? new int[] {AVCProfileBaseline, AVCProfileMain, AVCProfileHigh}
                        : new int[] {HEVCProfileMain, HEVCProfileMain10}) {
                    for (int level : avc ? new int[] {AVCLevel51}
                            : new int[] {HEVCMainTierLevel5, HEVCMainTierLevel51}) {
                        MediaFormat format = MediaFormat.createVideoFormat(mime, 640, 360);
                        format.setInteger(MediaFormat.KEY_PROFILE, profile);
                        format.setInteger(MediaFormat.KEY_LEVEL, level);
                        result.append("FORMAT ").append(mime).append(" ").append(profile)
                                .append(" ").append(level).append(" ")
                                .append(caps.isFormatSupported(format)).append("\n");
                    }
                }
            }
        }
        if (result.length() == 0) throw new AssertionError("No Westlake video decoders");
        if (fixtures != null) {
            for (String p : new String[] {"baseline", "main", "high", "hevc-main", "main10"}) {
                result.append("DECODE ").append(p).append(" ")
                        .append(decode(fixtures + "/" + p + ".mp4")).append("\n");
            }
        }
        return result.toString();
    }

    private static String decode(String path) {
        String step = "open";
        MediaExtractor input = new MediaExtractor();
        MediaCodec decoder = null;
        try {
            input.setDataSource(path);
            input.selectTrack(0);
            MediaFormat format = input.getTrackFormat(0);
            step = "create";
            decoder = MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME));
            String name = decoder.getName();
            step = "configure";
            decoder.configure(format, null, null, 0);
            step = "start";
            decoder.start();
            step = "drain";
            MediaCodec.BufferInfo out = new MediaCodec.BufferInfo();
            int frames = 0, samples = 0;
            long bytes = 0;
            boolean eos = false, sent = false;
            CRC32 checksum = new CRC32();
            long deadline = System.nanoTime() + 6_000_000_000L;
            byte[] slice = new byte[4096];
            while (!eos && System.nanoTime() < deadline) {
                if (!sent) {
                    int i = decoder.dequeueInputBuffer(10000);
                    if (i >= 0) {
                        int size = input.readSampleData(decoder.getInputBuffer(i), 0);
                        if (size < 0) {
                            decoder.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            sent = true;
                        } else {
                            decoder.queueInputBuffer(i, 0, size, input.getSampleTime(), 0);
                            samples++;
                            input.advance();
                        }
                    }
                }
                int i = decoder.dequeueOutputBuffer(out, 10000);
                if (i < 0) continue;
                if (out.size > 0 && (out.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                    ByteBuffer frame = decoder.getOutputBuffer(i);
                    if (frame == null) throw new IllegalStateException("no pixel buffer");
                    frame.position(out.offset);
                    frame.limit(out.offset + out.size);
                    while (frame.hasRemaining()) {
                        int n = Math.min(slice.length, frame.remaining());
                        frame.get(slice, 0, n);
                        checksum.update(slice, 0, n);
                    }
                    bytes += out.size;
                    frames++;
                }
                eos = (out.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                decoder.releaseOutputBuffer(i, false);
            }
            return name + " samples=" + samples + " frames=" + frames + " bytes=" + bytes
                    + " crc=" + checksum.getValue() + " eos=" + eos;
        } catch (Exception ex) {
            return "error=" + ex.getClass().getSimpleName() + " step=" + step;
        } finally {
            if (decoder != null) decoder.release();
            input.release();
        }
    }
}
