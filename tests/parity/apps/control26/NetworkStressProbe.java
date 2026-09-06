package org.westlake.gapcontrol;
import android.os.SystemClock;
import android.system.*;
import java.io.FileDescriptor;
import java.net.*;
import java.nio.ByteBuffer;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;

/** Local-only bounded negative and concurrency tests, independent of Internet. */
final class NetworkStressProbe extends NetworkProbe {
    interface Throwing { void run() throws Exception; }
    static void errno(int expected, Throwing call)throws Exception {
        try{call.run();throw new AssertionError("missing errno "+expected);}
        catch(ErrnoException error){require(error.errno==expected,"errno="+error.errno+" expected="+expected);}
    }
    static void exchange(int family, String address, boolean direct, int seed)throws Exception {
        FileDescriptor a=Os.socket(family,OsConstants.SOCK_DGRAM,0),b=Os.socket(family,OsConstants.SOCK_DGRAM,0);
        try{
            InetAddress loop=InetAddress.getByName(address);Os.bind(b,loop,0);timeout(b);
            int port=((InetSocketAddress)Os.getsockname(b)).getPort();
            ByteBuffer tx=direct?ByteBuffer.allocateDirect(512):ByteBuffer.allocate(512);
            ByteBuffer rx=direct?ByteBuffer.allocateDirect(520):ByteBuffer.allocate(520);
            for(int i=0;i<512;i++)tx.put((byte)(seed+i));tx.flip();rx.position(4);rx.limit(516);
            require(Os.sendto(a,tx,0,loop,port)==512,"send count");
            InetSocketAddress sender=new InetSocketAddress(0);
            require(Os.recvfrom(b,rx,0,sender)==512&&rx.position()==516,"receive count/position");
            for(int i=0;i<512;i++)require(rx.get(i+4)==(byte)(seed+i),"payload at "+i);
            require(sender.getAddress().isLoopbackAddress()&&sender.getPort()>0,"source");
        }finally{Os.close(a);Os.close(b);}
    }
    public void run(){
        check("ipv6-heap-direct-loopback",()->{for(int i=0;i<8;i++)exchange(OsConstants.AF_INET6,"::1",i%2==0,i);});
        check("udp-truncation-and-zero-length",()->{
            FileDescriptor a=Os.socket(OsConstants.AF_INET,OsConstants.SOCK_DGRAM,0),b=Os.socket(OsConstants.AF_INET,OsConstants.SOCK_DGRAM,0);
            try{
                InetAddress loop=InetAddress.getByName("127.0.0.1");Os.bind(b,loop,0);timeout(b);int port=((InetSocketAddress)Os.getsockname(b)).getPort();
                byte[] input={1,2,3,4,5},output={9,9,9,9};
                Os.sendto(a,input,0,5,0,loop,port);
                require(Os.recvfrom(b,output,1,2,OsConstants.MSG_TRUNC,null)==5,"wire length");
                require(output[0]==9&&output[1]==1&&output[2]==2&&output[3]==9,"truncate bounds");
                Os.sendto(a,input,0,0,0,loop,port);
                require(Os.recvfrom(b,output,0,4,0,null)==0,"empty datagram is not EOF");
            }finally{Os.close(a);Os.close(b);}
        });
        check("errno-and-poll-deadline",()->{
            final FileDescriptor fd=Os.socket(OsConstants.AF_INET,OsConstants.SOCK_DGRAM,0);
            try{
                Os.bind(fd,InetAddress.getByName("127.0.0.1"),0);
                Os.setsockoptTimeval(fd,OsConstants.SOL_SOCKET,OsConstants.SO_RCVTIMEO,StructTimeval.fromMillis(120));
                long begin=SystemClock.elapsedRealtime();
                errno(OsConstants.EAGAIN,()->Os.recvfrom(fd,new byte[8],0,8,0,null));
                require(SystemClock.elapsedRealtime()-begin<1200,"receive timeout bound");
                StructPollfd poll=new StructPollfd();poll.fd=fd;poll.events=(short)OsConstants.POLLIN;
                begin=SystemClock.elapsedRealtime();require(Os.poll(new StructPollfd[]{poll},80)==0,"poll timeout");
                require(SystemClock.elapsedRealtime()-begin<1000,"poll deadline");
                errno(OsConstants.ENOPROTOOPT,()->Os.setsockoptInt(fd,OsConstants.SOL_SOCKET,0x7fff,1));
            }finally{Os.close(fd);}
            errno(OsConstants.EBADF,()->Os.recvfrom(fd,new byte[8],0,8,0,null));
        });
        check("concurrent-gc-heap-direct-128",()->{
            AtomicReference<Throwable> error=new AtomicReference<>();Thread[] workers=new Thread[4];
            for(int w=0;w<4;w++){final int id=w;workers[w]=new Thread(()->{try{
                for(int i=0;i<32;i++){exchange(OsConstants.AF_INET,"127.0.0.1",i%2==0,id*32+i);if(i%4==0){byte[] garbage=new byte[512*1024];garbage[0]=1;System.gc();}}
            }catch(Throwable t){error.compareAndSet(null,t);}},"network-stress-"+w);workers[w].setDaemon(true);workers[w].start();}
            for(Thread worker:workers){worker.join(20000);require(!worker.isAlive(),"worker timeout");}
            if(error.get()!=null)throw new AssertionError(error.get());
        });
        check("async-close-and-socket-churn-32",()->{
            for(int i=0;i<32;i++){
                final DatagramSocket old=new DatagramSocket(0,InetAddress.getByName("127.0.0.1"));old.setSoTimeout(1500);
                CountDownLatch ready=new CountDownLatch(1);AtomicReference<Throwable> error=new AtomicReference<>();
                Thread reader=new Thread(()->{ready.countDown();try{old.receive(new DatagramPacket(new byte[8],8));error.set(new AssertionError("closed reader consumed data"));}catch(Throwable t){error.set(t);}});
                reader.setDaemon(true);reader.start();require(ready.await(1,TimeUnit.SECONDS),"reader not scheduled");
                if(i%2==0)Thread.sleep(5);long start=SystemClock.elapsedRealtime();old.close();
                // Allocate/send after close, while the old read may still unwind.
                exchange(OsConstants.AF_INET,"127.0.0.1",i%2==0,i);reader.join(1800);
                require(!reader.isAlive()&&SystemClock.elapsedRealtime()-start<1200,"close lost wakeup iteration="+i+" elapsed="+(SystemClock.elapsedRealtime()-start)+" alive="+reader.isAlive()+" result="+error.get());
                require(error.get() instanceof SocketException,"close result="+error.get());
            }
        });
        check("observed-numeric-fd-reuse-32",()->{
            java.lang.reflect.Method number=FileDescriptor.class.getDeclaredMethod("getInt$");number.setAccessible(true);
            java.lang.reflect.Method close=Class.forName("libcore.io.IoBridge").getMethod("closeAndSignalBlockedThreads",FileDescriptor.class);
            int reused=0;
            for(int i=0;i<32;i++){
                final FileDescriptor old=Os.socket(OsConstants.AF_INET,OsConstants.SOCK_DGRAM,0);
                final int oldNumber=(Integer)number.invoke(old);Os.bind(old,InetAddress.getByName("127.0.0.1"),0);timeout(old);
                AtomicReference<Throwable> outcome=new AtomicReference<>();CountDownLatch started=new CountDownLatch(1);
                Thread reader=new Thread(()->{started.countDown();try{
                    StructPollfd p=new StructPollfd();p.fd=old;p.events=(short)OsConstants.POLLIN;
                    Os.poll(new StructPollfd[]{p},1500);Os.recvfrom(old,new byte[8],0,8,0,null);
                    outcome.set(new AssertionError("old descriptor consumed replacement data"));
                }catch(Throwable t){outcome.set(t);}});
                reader.setDaemon(true);reader.start();started.await(1,TimeUnit.SECONDS);Thread.sleep(10);
                long begin=SystemClock.elapsedRealtime();close.invoke(null,old);
                // Never dup2 onto a descriptor another thread could own. Observe
                // actual kernel reuse only among sockets allocated by this test.
                FileDescriptor replacement=Os.socket(OsConstants.AF_INET,OsConstants.SOCK_DGRAM,0);
                try{
                    if((Integer)number.invoke(replacement)==oldNumber)reused++;
                    InetAddress loop=InetAddress.getByName("127.0.0.1");Os.bind(replacement,loop,0);timeout(replacement);
                    int port=((InetSocketAddress)Os.getsockname(replacement)).getPort();
                    FileDescriptor sender=Os.socket(OsConstants.AF_INET,OsConstants.SOCK_DGRAM,0);
                    try{Os.sendto(sender,new byte[]{42},0,1,0,loop,port);}finally{Os.close(sender);}
                    reader.join(1800);require(!reader.isAlive()&&SystemClock.elapsedRealtime()-begin<1200,"old poll remained blocked");
                    require(outcome.get() instanceof ErrnoException||outcome.get() instanceof SocketException,"old close outcome="+outcome.get());
                    byte[] result=new byte[8];require(Os.recvfrom(replacement,result,0,8,0,null)==1&&result[0]==42,"replacement payload preserved");
                }finally{Os.close(replacement);}
            }
            require(reused>=16,"insufficient observed reuse="+reused);System.err.println("[GAP_NET_STRESS] numeric_fd_reused="+reused+"/32");
        });
        System.err.println("[GAP_NET_STRESS] COMPLETE");
    }
}
