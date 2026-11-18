import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.os.Handler;
import android.os.HandlerThread;

import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Reports the asynchronous terminal result of the first unencrypted video or audio track on the target device. */
public final class VideoDecoderErrorProbe {
    public static String query(String library, String path) throws Exception {
        if (!library.isEmpty()) System.load(library);
        HandlerThread thread = new HandlerThread("codec-result-probe");
        thread.start();
        Handler handler = new Handler(thread.getLooper());
        CountDownLatch finished = new CountDownLatch(1);
        AtomicReference<String> result = new AtomicReference<>("timeout");
        AtomicInteger inputs = new AtomicInteger();
        AtomicInteger outputs = new AtomicInteger();
        AtomicInteger formats = new AtomicInteger();
        AtomicInteger wrongThread = new AtomicInteger();
        MediaExtractor media = new MediaExtractor();
        MediaCodec codec = null;
        try {
            media.setDataSource(path);
            int track = 0;
            while (track < media.getTrackCount()) {
                String mime = media.getTrackFormat(track).getString(MediaFormat.KEY_MIME);
                if (mime.startsWith("video/") || mime.startsWith("audio/")) break;
                track++;
            }
            if (track == media.getTrackCount()) throw new IllegalArgumentException("no media track");
            media.selectTrack(track);
            MediaFormat format = media.getTrackFormat(track);
            FutureTask<MediaCodec> create = new FutureTask<>(
                    () -> MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME)));
            handler.post(create);
            codec = create.get(10, TimeUnit.SECONDS);
            codec.setCallback(new MediaCodec.Callback() {
                private boolean inputEnded;

                private void count(AtomicInteger events) {
                    events.incrementAndGet();
                    if (Thread.currentThread() != thread) wrongThread.incrementAndGet();
                }

                @Override public synchronized void onInputBufferAvailable(MediaCodec decoder, int index) {
                    count(inputs);
                    if (inputEnded) return;
                    ByteBuffer input = decoder.getInputBuffer(index);
                    int bytes = media.readSampleData(input, 0);
                    if (bytes < 0) {
                        inputEnded = true;
                        decoder.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                    } else {
                        int flags = (media.getSampleFlags() & MediaExtractor.SAMPLE_FLAG_SYNC) != 0
                                ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0;
                        decoder.queueInputBuffer(index, 0, bytes, media.getSampleTime(), flags);
                        media.advance();
                    }
                }

                @Override public void onOutputBufferAvailable(MediaCodec decoder, int index, MediaCodec.BufferInfo info) {
                    count(outputs);
                    decoder.releaseOutputBuffer(index, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                        result.set("eos");
                        finished.countDown();
                    }
                }

                @Override public void onError(MediaCodec decoder, MediaCodec.CodecException error) {
                    result.set("error onRequestedHandler=" + (Thread.currentThread() == thread)
                            + " transient=" + error.isTransient() + " recoverable=" + error.isRecoverable()
                            + " code=" + error.getErrorCode());
                    finished.countDown();
                }

                @Override public void onOutputFormatChanged(MediaCodec decoder, MediaFormat newFormat) {
                    count(formats);
                }
            }, handler);
            codec.configure(format, null, null, 0);
            codec.start();
            boolean rejectedInput = false;
            boolean rejectedOutput = false;
            try { codec.dequeueInputBuffer(0); }
            catch (IllegalStateException expected) { rejectedInput = true; }
            try { codec.dequeueOutputBuffer(new MediaCodec.BufferInfo(), 0); }
            catch (IllegalStateException expected) { rejectedOutput = true; }
            finished.await(12, TimeUnit.SECONDS);
            return "CALLBACK " + result.get() + "\nHANDLER inputs=" + inputs.get() + " outputs=" + outputs.get()
                    + " formats=" + formats.get() + " wrongThread=" + wrongThread.get()
                    + " dequeueRejected=" + rejectedInput + "," + rejectedOutput + "\n";
        } finally {
            if (codec != null) codec.release();
            media.release();
            thread.quitSafely();
            thread.join();
        }
    }
}
