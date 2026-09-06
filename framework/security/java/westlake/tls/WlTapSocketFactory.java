package westlake.tls;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;

import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * Wraps the real socket factory so each connection reports its HTTP request line
 * and response status.  See {@link WlTapSocket}; enabled only under the debug
 * marker, since it adds a per-byte check until the first newline of each stream.
 */
final class WlTapSocketFactory extends SSLSocketFactory {

    private final SSLSocketFactory mDelegate;

    WlTapSocketFactory(SSLSocketFactory delegate) {
        mDelegate = delegate;
    }

    private Socket tap(Socket s, String host, int port) {
        boolean ssl = s instanceof SSLSocket;
        TlsBootstrap.log("tap createSocket " + host + ":" + port + " ssl=" + ssl);
        return ssl ? new WlTapSocket((SSLSocket) s, host, port) : s;
    }

    @Override public String[] getDefaultCipherSuites() { return mDelegate.getDefaultCipherSuites(); }
    @Override public String[] getSupportedCipherSuites() { return mDelegate.getSupportedCipherSuites(); }

    @Override
    public Socket createSocket(Socket s, String host, int port, boolean autoClose)
            throws IOException {
        return tap(mDelegate.createSocket(s, host, port, autoClose), host, port);
    }

    @Override
    public Socket createSocket(String host, int port) throws IOException {
        return tap(mDelegate.createSocket(host, port), host, port);
    }

    @Override
    public Socket createSocket(String host, int port, InetAddress lh, int lp) throws IOException {
        return tap(mDelegate.createSocket(host, port, lh, lp), host, port);
    }

    @Override
    public Socket createSocket(InetAddress host, int port) throws IOException {
        return tap(mDelegate.createSocket(host, port), host.getHostAddress(), port);
    }

    @Override
    public Socket createSocket(InetAddress host, int port, InetAddress lh, int lp)
            throws IOException {
        return tap(mDelegate.createSocket(host, port, lh, lp), host.getHostAddress(), port);
    }

    @Override
    public Socket createSocket() throws IOException {
        // No peer name yet; the tap still reports the HTTP lines.
        return tap(mDelegate.createSocket(), null, 0);
    }
}
