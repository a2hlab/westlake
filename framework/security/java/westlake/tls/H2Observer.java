package westlake.tls;

/**
 * Enough HTTP/2 to answer "is the server actually giving us the feed?".
 *
 * The feed API negotiates h2, so the line-oriented tap sees only the connection
 * preface and then binary frames.  Full decoding would mean HPACK with dynamic
 * table state, which is not worth building -- the questions that matter can all be
 * answered from the 9-byte frame headers, which are not compressed:
 *
 *   - how many DATA bytes came back (is there a payload at all?)
 *   - did the server RST_STREAM us, and with what error code (refused? cancelled?
 *     ENHANCE_YOUR_CALM, i.e. rate limited?)
 *   - did it send GOAWAY
 *
 * Frame header layout (RFC 7540 §4.1): 24-bit length, 8-bit type, 8-bit flags,
 * 1 reserved bit + 31-bit stream id.
 *
 * Payload is skipped in bulk rather than byte by byte: on an interpreted runtime a
 * per-byte loop over a multi-megabyte response would be its own performance
 * problem, and would risk perturbing the very timings we are trying to observe.
 */
final class H2Observer {

    private static final int DATA = 0, HEADERS = 1, RST_STREAM = 3;
    private static final int SETTINGS = 4, GOAWAY = 7, CONTINUATION = 9;

    private static final String[] ERRORS = {
        "NO_ERROR", "PROTOCOL_ERROR", "INTERNAL_ERROR", "FLOW_CONTROL_ERROR",
        "SETTINGS_TIMEOUT", "STREAM_CLOSED", "FRAME_SIZE_ERROR", "REFUSED_STREAM",
        "CANCEL", "COMPRESSION_ERROR", "CONNECT_ERROR", "ENHANCE_YOUR_CALM",
        "INADEQUATE_SECURITY", "HTTP_1_1_REQUIRED",
    };

    private final String mLabel;

    private final byte[] mHeader = new byte[9];
    private int mHeaderLen;
    private long mPayloadLeft;
    private int mType;
    private int mStream;

    /** RST_STREAM and GOAWAY payloads are tiny; keep just enough for the code. */
    private final byte[] mSmall = new byte[8];
    private int mSmallLen;

    private long mDataBytes;
    private int mHeadersFrames, mDataFrames, mSettingsFrames;
    private boolean mReported;

    /**
     * First slice of the response body, echoed to the log as base64.
     *
     * The app receives 27 KB from the feed API and still renders nothing, so the
     * question is what that payload actually says -- a real article list, or an
     * error telling us the device is not registered.  The body cannot be read here
     * (it is very likely gzipped, and the Content-Encoding header is HPACK-encoded
     * which we deliberately do not decode), so ship the raw bytes out through the
     * log and deal with them on the host.
     *
     * base64 to stderr rather than a file: the app uid cannot write to
     * /data/local/tmp -- that is what defeated the truststore export earlier -- and
     * stderr is already a proven channel out of this process.
     */
    private static final int CAPTURE = 1536;
    private final byte[] mCapture = new byte[CAPTURE];
    private int mCaptureLen;
    private boolean mCaptureEmitted;

    /**
     * Raw HEADERS/CONTINUATION payloads, emitted as base64 for offline decoding.
     *
     * The request :path and response :status are the two things worth knowing --
     * they answer whether device_register and the feed endpoint are actually being
     * requested -- but both live in HPACK, which is compressed, so reading the
     * first plaintext line of the stream (as the HTTP/1.1 side of the tap does)
     * shows nothing.  Mistaking that blind spot for "the request was never made"
     * is exactly the error recorded in §2.38.
     *
     * Decoding happens on the host rather than here: HPACK needs the static table,
     * a Huffman decoder and a per-connection dynamic table, and none of that is
     * worth running on an interpreted runtime inside the app's start-up path.
     * Frames are emitted in arrival order so the dynamic table can be replayed
     * faithfully offline.
     */
    private static final int HDR_CAPTURE = 16384;
    private final byte[] mHdr = new byte[HDR_CAPTURE];
    private int mHdrLen;
    private int mHdrEmitted;

    H2Observer(String label) {
        mLabel = label;
    }

    /** Feed bytes read from the peer. */
    void feed(byte[] buf, int off, int len) {
        int i = off;
        final int end = off + len;
        while (i < end) {
            if (mPayloadLeft > 0) {
                int skip = (int) Math.min(mPayloadLeft, (long) (end - i));
                if (mType == DATA) {
                    mDataBytes += skip;
                    if (mCaptureLen < CAPTURE) {
                        int take = Math.min(skip, CAPTURE - mCaptureLen);
                        System.arraycopy(buf, i, mCapture, mCaptureLen, take);
                        mCaptureLen += take;
                    }
                }
                if (mType == RST_STREAM || mType == GOAWAY) {
                    int take = Math.min(skip, mSmall.length - mSmallLen);
                    System.arraycopy(buf, i, mSmall, mSmallLen, take);
                    mSmallLen += take;
                }
                if ((mType == HEADERS || mType == CONTINUATION) && mHdrLen < HDR_CAPTURE) {
                    int take = Math.min(skip, HDR_CAPTURE - mHdrLen);
                    System.arraycopy(buf, i, mHdr, mHdrLen, take);
                    mHdrLen += take;
                }
                mPayloadLeft -= skip;
                i += skip;
                if (mPayloadLeft == 0) frameComplete();
                continue;
            }
            int take = Math.min(9 - mHeaderLen, end - i);
            System.arraycopy(buf, i, mHeader, mHeaderLen, take);
            mHeaderLen += take;
            i += take;
            if (mHeaderLen == 9) {
                mHeaderLen = 0;
                mPayloadLeft = ((mHeader[0] & 0xffL) << 16)
                             | ((mHeader[1] & 0xffL) << 8)
                             |  (mHeader[2] & 0xffL);
                mType = mHeader[3] & 0xff;
                mStream = ((mHeader[5] & 0x7f) << 24) | ((mHeader[6] & 0xff) << 16)
                        | ((mHeader[7] & 0xff) << 8) | (mHeader[8] & 0xff);
                mSmallLen = 0;
                if (mType == HEADERS) {
                    mHeadersFrames++;
                    // Report the first response headers as they arrive: most
                    // connections are kept alive, so waiting for close() would
                    // never show the ones that matter.
                    if (mHeadersFrames == 1) {
                        TlsBootstrap.log("h2 " + mLabel + " <- HEADERS stream="
                                + mStream + " len=" + mPayloadLeft);
                    }
                } else if (mType == DATA) {
                    mDataFrames++;
                    if (mDataFrames == 1) {
                        TlsBootstrap.log("h2 " + mLabel + " <- first DATA stream="
                                + mStream + " len=" + mPayloadLeft);
                    }
                    maybeEmitCapture();
                } else if (mType == SETTINGS) {
                    mSettingsFrames++;
                }
                if (mPayloadLeft == 0) frameComplete();
            }
        }
    }

    /**
     * Emit whatever header bytes have arrived since the last flush.
     *
     * HPACK is one stateful stream per connection and per direction, so the
     * concatenation of header block fragments in arrival order is what the offline
     * decoder needs; emitting incrementally keeps that order intact even if the
     * connection is never closed.
     */
    private void flushHeaders() {
        if (mHdrLen <= mHdrEmitted) return;
        byte[] slice = new byte[mHdrLen - mHdrEmitted];
        System.arraycopy(mHdr, mHdrEmitted, slice, 0, slice.length);
        mHdrEmitted = mHdrLen;
        String b64 = base64(slice);
        final int chunk = 512;
        for (int i = 0; i < b64.length(); i += chunk) {
            TlsBootstrap.log("h2hdr " + mLabel + " " + mStream + " "
                    + b64.substring(i, Math.min(b64.length(), i + chunk)));
        }
    }

    private void frameComplete() {
        if (mType == HEADERS || mType == CONTINUATION) flushHeaders();
        if (mType == RST_STREAM && mSmallLen >= 4) {
            TlsBootstrap.log("h2 " + mLabel + " RST_STREAM stream=" + mStream
                    + " error=" + errorName(readInt(0)));
        } else if (mType == GOAWAY && mSmallLen >= 8) {
            TlsBootstrap.log("h2 " + mLabel + " GOAWAY lastStream=" + readInt(0)
                    + " error=" + errorName(readInt(4)));
        }
    }

    private int readInt(int at) {
        return ((mSmall[at] & 0xff) << 24) | ((mSmall[at + 1] & 0xff) << 16)
             | ((mSmall[at + 2] & 0xff) << 8) | (mSmall[at + 3] & 0xff);
    }

    private static String errorName(int code) {
        return (code >= 0 && code < ERRORS.length)
                ? ERRORS[code] + "(" + code + ")" : "UNKNOWN(" + code + ")";
    }

    /** Emit the captured body slice once we have enough of it. */
    private void maybeEmitCapture() {
        if (mCaptureEmitted || mCaptureLen < CAPTURE) return;
        emitCapture();
    }

    private void emitCapture() {
        if (mCaptureEmitted || mCaptureLen == 0) return;
        mCaptureEmitted = true;
        byte[] slice = new byte[mCaptureLen];
        System.arraycopy(mCapture, 0, slice, 0, mCaptureLen);
        // Chunked: a single multi-kilobyte log line risks being truncated.
        String b64 = base64(slice);
        final int chunk = 512;
        for (int i = 0, part = 0; i < b64.length(); i += chunk, part++) {
            TlsBootstrap.log("h2body " + mLabel + " #" + part + " "
                    + b64.substring(i, Math.min(b64.length(), i + chunk)));
        }
        TlsBootstrap.log("h2body " + mLabel + " end bytes=" + mCaptureLen);
    }

    private static final char[] B64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".toCharArray();

    /** java.util.Base64 is API 26+; hand-rolled keeps this buildable at --release 8. */
    private static String base64(byte[] in) {
        StringBuilder sb = new StringBuilder(((in.length + 2) / 3) * 4);
        int i = 0;
        while (i + 2 < in.length) {
            int n = ((in[i] & 0xff) << 16) | ((in[i + 1] & 0xff) << 8) | (in[i + 2] & 0xff);
            sb.append(B64[(n >>> 18) & 63]).append(B64[(n >>> 12) & 63])
              .append(B64[(n >>> 6) & 63]).append(B64[n & 63]);
            i += 3;
        }
        int rem = in.length - i;
        if (rem == 1) {
            int n = (in[i] & 0xff) << 16;
            sb.append(B64[(n >>> 18) & 63]).append(B64[(n >>> 12) & 63]).append("==");
        } else if (rem == 2) {
            int n = ((in[i] & 0xff) << 16) | ((in[i + 1] & 0xff) << 8);
            sb.append(B64[(n >>> 18) & 63]).append(B64[(n >>> 12) & 63])
              .append(B64[(n >>> 6) & 63]).append('=');
        }
        return sb.toString();
    }

    /** One line per connection, emitted when it closes. */
    void report() {
        if (mReported) return;
        mReported = true;
        emitCapture();   // flush a short body that never reached the cap
        TlsBootstrap.log("h2 " + mLabel + " settings=" + mSettingsFrames
                + " headers=" + mHeadersFrames + " dataFrames=" + mDataFrames
                + " dataBytes=" + mDataBytes);
    }
}
