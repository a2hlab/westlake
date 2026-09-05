package org.westlake.imenetcontrol;

import android.app.Activity;
import android.os.Bundle;
import android.graphics.Color;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Button;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.EditorInfo;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.net.DatagramSocket;
import java.net.DatagramPacket;
import java.net.InetAddress;
import java.net.URL;
import java.security.KeyStore;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

public final class MainActivity extends Activity {
    private TextView results;
    private void report(String text) {
        System.err.println("[IME_NET_CONTROL] " + text);
        runOnUiThread(() -> results.append(text + "\n"));
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 24, 24, 24);
        layout.setBackgroundColor(Color.WHITE);
        EditText editor = new EditText(this);
        editor.setSingleLine(true);
        editor.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        editor.setHint("Tap here, type ASCII and Chinese, then Search");
        editor.setTextColor(Color.BLACK);
        editor.setOnEditorActionListener((view, action, event) -> {
            report("EDITOR_ACTION=" + action + " TEXT=" + view.getText());
            return true;
        });
        layout.addView(editor, new LinearLayout.LayoutParams(-1, 100));
        Button show = new Button(this);
        show.setText("Show via Android IMM");
        show.setOnClickListener(view -> {
            editor.requestFocus();
            report("IMM_SHOW=" + ((InputMethodManager) getSystemService(INPUT_METHOD_SERVICE))
                    .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
                    + " WINDOW_FOCUS=" + editor.hasWindowFocus());
        });
        layout.addView(show);
        Button direct = new Button(this);
        direct.setText("Diagnostic: test OHOS helper directly");
        direct.setOnClickListener(view -> {
            editor.requestFocus();
            try { report("HELPER_SHOW=" + Class.forName("adapter.window.OhImeBridge")
                    .getMethod("show").invoke(null)); }
            catch (Throwable failure) { report("HELPER_FAIL=" + failure); }
        });
        layout.addView(direct);
        results = new TextView(this);
        results.setTextSize(15);
        results.setTextColor(Color.BLACK);
        layout.addView(results, new LinearLayout.LayoutParams(-1, -1));
        setContentView(layout);
        new Thread(this::checks, "ime-net-checks").start();
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (results != null) report("WINDOW_FOCUS=" + focused);
    }
    private X509Certificate cert(String name) throws Exception {
        try (InputStream in = getAssets().open(name)) {
            return (X509Certificate) CertificateFactory.getInstance("X.509").generateCertificate(in);
        }
    }
    private void checks() {
        try {
            Class<?> factory = Class.forName("com.android.org.bouncycastle.crypto.digests.AndroidDigestFactory");
            Object digest = factory.getMethod("getSHA256").invoke(null);
            Class<?> api = Class.forName("com.android.org.bouncycastle.crypto.Digest");
            byte[] input = new byte[]{97, 98, 99}, output = new byte[32];
            api.getMethod("update", byte[].class, int.class, int.class).invoke(digest, input, 0, input.length);
            api.getMethod("doFinal", byte[].class, int.class).invoke(digest, output, 0);
            StringBuilder hex = new StringBuilder();
            for (byte b : output) hex.append(String.format("%02x", b & 255));
            report("SHA256=" + hex + " PASS=" + hex.toString().equals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
            report("JAVA_VENDOR=" + System.getProperty("java.vendor"));
            TrustManagerFactory tmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
            tmf.init((KeyStore) null);
            X509TrustManager system = (X509TrustManager) tmf.getTrustManagers()[0];
            report("SYSTEM_TRUST=" + system.getClass().getName() + " ROOTS=" + system.getAcceptedIssuers().length);
            KeyStore store = KeyStore.getInstance("BKS");
            store.load(null, null);
            store.setCertificateEntry("test-root", cert("root.pem"));
            X509TrustManager custom = (X509TrustManager) Class.forName("adapter.security.OhSystemTrustManager")
                    .getConstructor(KeyStore.class).newInstance(store);
            custom.checkServerTrusted(new X509Certificate[]{cert("valid.pem")}, "RSA");
            report("VALID_CHAIN=PASS");
            for (String name : new String[]{"untrusted.pem", "expired.pem"}) {
                try {
                    custom.checkServerTrusted(new X509Certificate[]{cert(name)}, "RSA");
                    report(name + " REJECT=FAIL");
                } catch (CertificateException expected) { report(name + " REJECT=PASS"); }
            }
        } catch (Throwable failure) { report("CRYPTO_FAIL=" + failure); failure.printStackTrace(); }
        for (String name : new String[]{"IPPROTO_IP", "IP_MULTICAST_ALL", "IPPROTO_IPV6", "IPV6_MULTICAST_ALL"}) {
            try { report("CONST " + name + "=" + Class.forName("android.system.OsConstants").getField(name).getInt(null)); }
            catch (Throwable failure) { report("CONST " + name + "=" + failure); }
        }
        try (DatagramSocket socket = new DatagramSocket()) {
            report("UDP_CREATE=PASS port=" + socket.getLocalPort());
        } catch (Throwable failure) { report("UDP_CREATE=FAIL " + failure); failure.printStackTrace(); }
        for (String loopback : new String[]{"127.0.0.1", "::1"}) {
            try (DatagramSocket receiver = new DatagramSocket(0, InetAddress.getByName(loopback));
                 DatagramSocket sender = new DatagramSocket()) {
                receiver.setSoTimeout(2000);
                byte[] message = new byte[]{10, 20, 30, 40};
                sender.send(new DatagramPacket(message, 1, 2, InetAddress.getByName(loopback), receiver.getLocalPort()));
                byte[] received = new byte[8];
                DatagramPacket packet = new DatagramPacket(received, 2, 4);
                receiver.receive(packet);
                boolean ok = packet.getLength() == 2 && received[2] == 20 && received[3] == 30
                        && received[0] == 0 && packet.getPort() == sender.getLocalPort();
                report("UDP_LOOPBACK " + loopback + " PASS=" + ok + " peer=" + packet.getAddress());
                receiver.setSoTimeout(100);
                try { receiver.receive(packet); report("UDP_TIMEOUT=FAIL"); }
                catch (java.net.SocketTimeoutException expected) { report("UDP_TIMEOUT=PASS"); }
            } catch (Throwable failure) { report("UDP_LOOPBACK " + loopback + " FAIL=" + failure); failure.printStackTrace(); }
        }
        try { report("DNS=PASS " + InetAddress.getByName("example.com")); }
        catch (Throwable failure) { report("DNS=FAIL " + failure); }
        HttpsURLConnection connection = null;
        try {
            connection = (HttpsURLConnection) new URL("https://example.com/").openConnection();
            connection.setConnectTimeout(15000);
            connection.setReadTimeout(15000);
            report("HTTPS_STATUS=" + connection.getResponseCode());
        } catch (Throwable failure) { report("HTTPS_FAIL=" + failure); failure.printStackTrace(); }
        finally { if (connection != null) connection.disconnect(); }
    }
}
