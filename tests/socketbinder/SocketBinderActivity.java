package org.westlake.socketbinderprobe;

import adapter.core.SocketBinder;
import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import java.io.IOException;

/**
 * Runs SocketBinderProbe in a Westlake process. Start the same app twice: the first process to take
 * the socket name serves, the second runs the checks. Results go to stdout, which
 * Westlake sends to the process's stderr log.
 */
public final class SocketBinderActivity extends Activity {
    private static final String SOCKET = "wl.socketbinder.probe";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        TextView text = new TextView(this);
        text.setText("SocketBinder probe, pid " + android.os.Process.myPid());
        setContentView(text);
        new Thread(this::runProbe, "probe").start();
    }

    private void runProbe() {
        try {
            System.loadLibrary("socketbinder");
            int listener = -1;
            try {
                listener = SocketBinder.listen(SOCKET);
            } catch (IOException taken) {
                // Another process serves, so this one is the client.
            }
            String role = listener >= 0 ? "server" : "client";
            System.out.println("SOCKETBINDER role=" + role + " pid=" + android.os.Process.myPid());
            if (listener >= 0) {
                SocketBinderProbe.serve(listener);
            } else {
                SocketBinderProbe.main2(new String[] {"client", SOCKET});
            }
        } catch (Throwable t) {
            System.err.println("SOCKETBINDER failed: " + t);
            t.printStackTrace();
        }
    }
}
