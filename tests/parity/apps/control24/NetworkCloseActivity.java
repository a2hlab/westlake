package org.westlake.gapcontrol;
import android.app.Activity;
import android.os.*;
import android.widget.TextView;
import java.net.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

/** Public-API reference: separate close, reader wakeup, and workload delays. */
public final class NetworkCloseActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state); TextView label=new TextView(this);
        label.setText("Public DatagramSocket close reference");setContentView(label);
        new Thread(()->{
            int failures=0;
            for(int timeout:new int[]{0,1500}) for(int i=0;i<8;i++) {
                try { probe(timeout,i); } catch(Throwable t) {
                    failures++;System.err.println("[CLOSE_REF] FAIL timeout="+timeout+" iteration="+i+" "+t);
                }
            }
            System.err.println("[CLOSE_REF] COMPLETE failures="+failures);
        },"close-reference").start();
    }
    static void probe(int timeout,int iteration)throws Exception {
        DatagramSocket socket=new DatagramSocket(0,InetAddress.getByName("127.0.0.1"));
        socket.setSoTimeout(timeout);
        CountDownLatch entered=new CountDownLatch(1),finished=new CountDownLatch(1);
        AtomicReference<Throwable> result=new AtomicReference<>();AtomicLong returned=new AtomicLong();
        Thread reader=new Thread(()->{entered.countDown();try {
            socket.receive(new DatagramPacket(new byte[8],8));
            result.set(new AssertionError("receive returned data"));
        }catch(Throwable t){result.set(t);}finally{returned.set(SystemClock.elapsedRealtime());finished.countDown();}},"close-reader");
        reader.setDaemon(true);reader.start();
        if(!entered.await(2,TimeUnit.SECONDS))throw new AssertionError("reader not scheduled");
        Thread.sleep(iteration%2==0?100:5);
        long start=SystemClock.elapsedRealtime();socket.close();long closed=SystemClock.elapsedRealtime();
        boolean woke=finished.await(3500,TimeUnit.MILLISECONDS);
        long delay=woke?returned.get()-start:-1;
        System.err.println("[CLOSE_REF] timeout="+timeout+" iteration="+iteration+" close_ms="+(closed-start)+" wake_ms="+delay+" finished="+woke+" reader_state="+reader.getState()+" result="+result.get());
        if(!woke||delay>=1000||!(result.get() instanceof SocketException))throw new AssertionError("prompt close contract");
    }
}
