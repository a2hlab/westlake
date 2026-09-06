package westlake.tls;

import java.io.FilterInputStream;
import java.io.FilterOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.SocketAddress;
import java.net.SocketException;
import java.nio.channels.SocketChannel;

import javax.net.ssl.HandshakeCompletedListener;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;

/**
 * An SSLSocket that reports the first request line and response status of each
 * connection, then otherwise stays out of the way.
 *
 * The feed is still empty even though the app now completes handshakes to
 * api.toutiaoapi.com and the image CDN, and the app logs nothing useful to stderr
 * (its own logger goes elsewhere).  Since every byte the app sends over HTTPS
 * passes through the factory we install, tapping it here is the only place that
 * can say whether the server is actually answering -- and with what.
 *
 * Deliberately observation-only: bytes are passed through untouched, only the
 * first {@value #PEEK} of each direction are copied aside, and each socket logs
 * at most one request and one response.  HTTP/1.1 keeps both on a plaintext first
 * line inside the TLS session, which is exactly what we need; a gzipped body does
 * not matter because the status line is not compressed.
 *
 * Enabled only when the debug marker is present -- see TlsBootstrap.
 */
final class WlTapSocket extends SSLSocket {

    /** Enough for a request line plus a status line; we never want bodies. */
    private static final int PEEK = 512;

    private final SSLSocket mDelegate;
    private final String mLabel;

    WlTapSocket(SSLSocket delegate, String host, int port) {
        mDelegate = delegate;
        mLabel = (host != null ? host : "?") + ":" + port;
    }

    /* ---------------- the part that exists for a reason ---------------- */

    @Override
    public InputStream getInputStream() throws IOException {
        return new FilterInputStream(mDelegate.getInputStream()) {
            private final StringBuilder head = new StringBuilder();
            private boolean done;

            private void note(int b) {
                if (done || b < 0) return;
                if (b == '\n' || head.length() >= PEEK) {
                    done = true;
                    TlsBootstrap.log("http " + mLabel + " <- " + head.toString().trim());
                    return;
                }
                if (b != '\r') head.append((char) b);
            }

            @Override public int read() throws IOException {
                int b = in.read();
                if (mHttp2) {
                    if (b >= 0) { mOne[0] = (byte) b; observer().feed(mOne, 0, 1); }
                } else {
                    note(b);
                }
                return b;
            }

            @Override public int read(byte[] buf, int off, int len) throws IOException {
                int n = in.read(buf, off, len);
                if (n <= 0) return n;
                if (mHttp2) {
                    // Binary frames: no status line to read, so count instead.
                    observer().feed(buf, off, n);
                } else {
                    for (int i = 0; i < n && !done; i++) note(buf[off + i] & 0xff);
                }
                return n;
            }
        };
    }

    /** Set once the outbound preface identifies this connection as HTTP/2. */
    private volatile boolean mHttp2;
    private volatile H2Observer mObserver;
    private volatile H2Observer mOutObserver;
    private final byte[] mOne = new byte[1];

    /**
     * Bytes written so far, to find where the outbound frame stream begins.
     *
     * The client preface is a fixed 24 bytes ("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n")
     * and is not a frame, so feeding it to the frame parser would desynchronise it.
     */
    private long mOutBytes;
    private static final int H2_PREFACE = 24;

    private H2Observer observer() {
        H2Observer o = mObserver;
        if (o == null) {
            o = new H2Observer(mLabel + " <-");
            mObserver = o;
        }
        return o;
    }

    /**
     * The request side.  This is the half that carries :method and :path, i.e. the
     * only way to tell whether device_register and the feed endpoint are actually
     * being requested -- the inbound side only ever shows the response status.
     */
    private H2Observer outObserver() {
        H2Observer o = mOutObserver;
        if (o == null) {
            o = new H2Observer(mLabel + " ->");
            mOutObserver = o;
        }
        return o;
    }

    /** Feed outbound bytes to the frame parser, skipping the fixed preface. */
    private void feedOut(byte[] buf, int off, int len) {
        long start = mOutBytes;
        mOutBytes += len;
        if (start + len <= H2_PREFACE) return;
        int skip = (start < H2_PREFACE) ? (int) (H2_PREFACE - start) : 0;
        outObserver().feed(buf, off + skip, len - skip);
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
        return new FilterOutputStream(mDelegate.getOutputStream()) {
            private final StringBuilder head = new StringBuilder();
            private boolean done;

            private void note(int b) {
                if (done) return;
                if (b == '\n' || head.length() >= PEEK) {
                    done = true;
                    String line = head.toString().trim();
                    TlsBootstrap.log("http " + mLabel + " -> " + line);
                    // okhttp writes "PRI * HTTP/2.0" as the h2 connection preface;
                    // everything after it on this socket is binary frames, so switch
                    // the read side over to frame counting.
                    if (line.startsWith("PRI ")) mHttp2 = true;
                    return;
                }
                if (b != '\r') head.append((char) b);
            }

            @Override public void write(int b) throws IOException {
                out.write(b);
                note(b & 0xff);
            }

            @Override public void write(byte[] buf, int off, int len) throws IOException {
                // FilterOutputStream's default write(byte[],int,int) goes byte by
                // byte through write(int), which would be needlessly slow here.
                out.write(buf, off, len);
                if (mHttp2) { feedOut(buf, off, len); return; }
                for (int i = 0; i < len && !done; i++) note(buf[off + i] & 0xff);
                if (mHttp2) {
                    // The preface completed inside this write; account for the
                    // bytes it consumed so frame parsing starts at the right place.
                    feedOut(buf, off, len);
                }
            }
        };
    }

    /* ---------------- pure delegation below this line ---------------- */

    @Override public String[] getSupportedCipherSuites() { return mDelegate.getSupportedCipherSuites(); }
    @Override public String[] getEnabledCipherSuites() { return mDelegate.getEnabledCipherSuites(); }
    @Override public void setEnabledCipherSuites(String[] s) { mDelegate.setEnabledCipherSuites(s); }
    @Override public String[] getSupportedProtocols() { return mDelegate.getSupportedProtocols(); }
    @Override public String[] getEnabledProtocols() { return mDelegate.getEnabledProtocols(); }
    @Override public void setEnabledProtocols(String[] p) { mDelegate.setEnabledProtocols(p); }
    /**
     * okhttp reads the session immediately after startHandshake() -- Handshake.get()
     * then the hostname verifier then the certificate pinner -- and any failure
     * there lands in connectTls()'s finally-block, which closes the socket without
     * a trace.  Since every one of those reads comes through here, report what the
     * session actually says.
     */
    @Override public SSLSession getSession() {
        SSLSession s = mDelegate.getSession();
        if (!mSessionLogged) {
            mSessionLogged = true;
            String certs;
            try {
                certs = String.valueOf(s.getPeerCertificates().length);
            } catch (Throwable t) {
                certs = "THREW " + t;
            }
            TlsBootstrap.log("session " + mLabel
                    + " proto=" + s.getProtocol()
                    + " cipher=" + s.getCipherSuite()
                    + " peerCerts=" + certs);
        }
        return s;
    }

    private volatile boolean mSessionLogged;

    /*
     * ALPN accessors, added in Java 9 / API 29.
     *
     * These are NOT marked @Override because this class is compiled with
     * --release 8, where javax.net.ssl.SSLSocket does not declare them; at runtime
     * on the board they do override, which is the whole point.  Without them we
     * inherit SSLSocket's base implementation, which throws
     * UnsupportedOperationException -- and okhttp's Android10Platform calls
     * getSelectedProtocol(sslSocket) -> sslSocket.getApplicationProtocol()
     * immediately after startHandshake() and before it creates the streams.  That
     * would make every connection fail inside connectTls() and be closed by its
     * finally-block, i.e. the tap would manufacture the exact symptom it was built
     * to investigate.
     */
    public String getApplicationProtocol() {
        return delegateString("getApplicationProtocol");
    }

    public String getHandshakeApplicationProtocol() {
        return delegateString("getHandshakeApplicationProtocol");
    }

    private String delegateString(String method) {
        try {
            return (String) mDelegate.getClass().getMethod(method).invoke(mDelegate);
        } catch (Throwable t) {
            // Match the platform's behaviour when the delegate cannot answer.
            throw new UnsupportedOperationException(method, t);
        }
    }
    @Override public SSLSession getHandshakeSession() { return mDelegate.getHandshakeSession(); }
    @Override public void addHandshakeCompletedListener(HandshakeCompletedListener l) {
        mDelegate.addHandshakeCompletedListener(l);
    }
    @Override public void removeHandshakeCompletedListener(HandshakeCompletedListener l) {
        mDelegate.removeHandshakeCompletedListener(l);
    }
    @Override public void startHandshake() throws IOException { mDelegate.startHandshake(); }
    @Override public void setUseClientMode(boolean m) { mDelegate.setUseClientMode(m); }
    @Override public boolean getUseClientMode() { return mDelegate.getUseClientMode(); }
    @Override public void setNeedClientAuth(boolean n) { mDelegate.setNeedClientAuth(n); }
    @Override public boolean getNeedClientAuth() { return mDelegate.getNeedClientAuth(); }
    @Override public void setWantClientAuth(boolean w) { mDelegate.setWantClientAuth(w); }
    @Override public boolean getWantClientAuth() { return mDelegate.getWantClientAuth(); }
    @Override public void setEnableSessionCreation(boolean f) { mDelegate.setEnableSessionCreation(f); }
    @Override public boolean getEnableSessionCreation() { return mDelegate.getEnableSessionCreation(); }
    @Override public SSLParameters getSSLParameters() { return mDelegate.getSSLParameters(); }
    @Override public void setSSLParameters(SSLParameters p) { mDelegate.setSSLParameters(p); }

    @Override public void connect(SocketAddress a) throws IOException { mDelegate.connect(a); }
    @Override public void connect(SocketAddress a, int t) throws IOException { mDelegate.connect(a, t); }
    @Override public void bind(SocketAddress a) throws IOException { mDelegate.bind(a); }
    @Override public InetAddress getInetAddress() { return mDelegate.getInetAddress(); }
    @Override public InetAddress getLocalAddress() { return mDelegate.getLocalAddress(); }
    @Override public int getPort() { return mDelegate.getPort(); }
    @Override public int getLocalPort() { return mDelegate.getLocalPort(); }
    @Override public SocketAddress getRemoteSocketAddress() { return mDelegate.getRemoteSocketAddress(); }
    @Override public SocketAddress getLocalSocketAddress() { return mDelegate.getLocalSocketAddress(); }
    @Override public SocketChannel getChannel() { return mDelegate.getChannel(); }
    @Override public void setTcpNoDelay(boolean on) throws SocketException { mDelegate.setTcpNoDelay(on); }
    @Override public boolean getTcpNoDelay() throws SocketException { return mDelegate.getTcpNoDelay(); }
    @Override public void setSoLinger(boolean on, int l) throws SocketException { mDelegate.setSoLinger(on, l); }
    @Override public int getSoLinger() throws SocketException { return mDelegate.getSoLinger(); }
    @Override public void sendUrgentData(int d) throws IOException { mDelegate.sendUrgentData(d); }
    @Override public void setOOBInline(boolean on) throws SocketException { mDelegate.setOOBInline(on); }
    @Override public boolean getOOBInline() throws SocketException { return mDelegate.getOOBInline(); }
    @Override public void setSoTimeout(int t) throws SocketException { mDelegate.setSoTimeout(t); }
    @Override public int getSoTimeout() throws SocketException { return mDelegate.getSoTimeout(); }
    @Override public void setSendBufferSize(int s) throws SocketException { mDelegate.setSendBufferSize(s); }
    @Override public int getSendBufferSize() throws SocketException { return mDelegate.getSendBufferSize(); }
    @Override public void setReceiveBufferSize(int s) throws SocketException { mDelegate.setReceiveBufferSize(s); }
    @Override public int getReceiveBufferSize() throws SocketException { return mDelegate.getReceiveBufferSize(); }
    @Override public void setKeepAlive(boolean on) throws SocketException { mDelegate.setKeepAlive(on); }
    @Override public boolean getKeepAlive() throws SocketException { return mDelegate.getKeepAlive(); }
    @Override public void setTrafficClass(int tc) throws SocketException { mDelegate.setTrafficClass(tc); }
    @Override public int getTrafficClass() throws SocketException { return mDelegate.getTrafficClass(); }
    @Override public void setReuseAddress(boolean on) throws SocketException { mDelegate.setReuseAddress(on); }
    @Override public boolean getReuseAddress() throws SocketException { return mDelegate.getReuseAddress(); }
    @Override public void close() throws IOException {
        H2Observer o = mObserver;
        if (o != null) o.report();
        mDelegate.close();
    }
    @Override public void shutdownInput() throws IOException { mDelegate.shutdownInput(); }
    @Override public void shutdownOutput() throws IOException { mDelegate.shutdownOutput(); }
    @Override public boolean isConnected() { return mDelegate.isConnected(); }
    @Override public boolean isBound() { return mDelegate.isBound(); }
    @Override public boolean isClosed() { return mDelegate.isClosed(); }
    @Override public boolean isInputShutdown() { return mDelegate.isInputShutdown(); }
    @Override public boolean isOutputShutdown() { return mDelegate.isOutputShutdown(); }
    @Override public void setPerformancePreferences(int c, int l, int b) {
        mDelegate.setPerformancePreferences(c, l, b);
    }
    @Override public String toString() { return "WlTap[" + mDelegate + "]"; }
}
