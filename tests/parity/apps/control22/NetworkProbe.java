package org.westlake.gapcontrol;
import android.os.SystemClock;
import android.system.*;
import java.io.FileDescriptor;
import java.net.*;
import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

class NetworkProbe implements Runnable {
    interface Check { void run() throws Exception; }
    void check(String name, Check c) {
        long t=SystemClock.elapsedRealtime();
        try { c.run();System.err.println("[GAP_NET] PASS "+name+" ms="+(SystemClock.elapsedRealtime()-t)); }
        catch(Throwable e){System.err.println("[GAP_NET] FAIL "+name+" "+e);e.printStackTrace();}
    }
    static void require(boolean ok,String detail){if(!ok)throw new AssertionError(detail);}
    static void timeout(FileDescriptor f)throws Exception{Os.setsockoptTimeval(f,OsConstants.SOL_SOCKET,OsConstants.SO_RCVTIMEO,StructTimeval.fromMillis(3000));}
    public void run() {
        check("udp-direct-offset",new Check(){public void run()throws Exception{
            FileDescriptor a=Os.socket(OsConstants.AF_INET,OsConstants.SOCK_DGRAM,0);
            FileDescriptor b=Os.socket(OsConstants.AF_INET,OsConstants.SOCK_DGRAM,0);
            try{
                InetAddress loop=InetAddress.getByName("127.0.0.1");Os.bind(b,loop,0);timeout(b);
                int port=((InetSocketAddress)Os.getsockname(b)).getPort();
                ByteBuffer sent=ByteBuffer.allocateDirect(12);sent.put(new byte[]{8,8,11,22,33,44,8});sent.position(2);sent.limit(6);
                require(Os.sendto(a,sent,0,loop,port)==4&&sent.position()==6,"send offset/position");
                ByteBuffer received=ByteBuffer.allocateDirect(12);for(int i=0;i<12;i++)received.put(i,(byte)9);received.position(3);received.limit(9);
                InetSocketAddress source=new InetSocketAddress(0);
                require(Os.recvfrom(b,received,0,source)==4&&received.position()==7,"receive position");
                require(received.get(2)==9&&received.get(3)==11&&received.get(6)==44&&received.get(7)==9,"bounds/payload");
                require(source.getPort()>0&&source.getAddress().isLoopbackAddress(),"source metadata");
            }finally{Os.close(a);Os.close(b);}
        }});
        check("unix-abstract",new Check(){public void run()throws Exception{
            FileDescriptor a=Os.socket(OsConstants.AF_UNIX,OsConstants.SOCK_DGRAM,0);
            FileDescriptor b=Os.socket(OsConstants.AF_UNIX,OsConstants.SOCK_DGRAM,0);
            try{
                Class<?> addressClass=Class.forName("android.system.UnixSocketAddress");
                SocketAddress target=(SocketAddress)addressClass.getMethod("createAbstract",String.class).invoke(null,"westlake-gap-"+android.os.Process.myPid());
                Os.class.getMethod("bind",FileDescriptor.class,SocketAddress.class).invoke(null,b,target);timeout(b);
                byte[] payload={4,5,6};
                Object n=Os.class.getMethod("sendto",FileDescriptor.class,byte[].class,int.class,int.class,int.class,SocketAddress.class).invoke(null,a,payload,0,3,0,target);
                byte[] output=new byte[5];require(((Integer)n)==3&&Os.recvfrom(b,output,1,3,0,null)==3&&output[1]==4&&output[3]==6,"unix payload");
            }finally{Os.close(a);Os.close(b);}
        }});
        check("multicast-loopback",new Check(){public void run()throws Exception{
            InetAddress loop=InetAddress.getByName("127.0.0.1");NetworkInterface iface=NetworkInterface.getByInetAddress(loop);
            InetAddress group=InetAddress.getByName("239.255.77.77");
            try(MulticastSocket s=new MulticastSocket(0)){
                s.setSoTimeout(2500);s.setNetworkInterface(iface);s.setTimeToLive(0);s.setLoopbackMode(false);
                InetSocketAddress membership=new InetSocketAddress(group,s.getLocalPort());s.joinGroup(membership,iface);
                try{s.send(new DatagramPacket(new byte[]{31,32},2,group,s.getLocalPort()));DatagramPacket received=new DatagramPacket(new byte[16],16);s.receive(received);require(received.getLength()==2&&received.getData()[0]==31,"multicast payload");}
                finally{s.leaveGroup(membership,iface);}
            }
        }});
        check("datagram-api-async-close",new Check(){public void run()throws Exception{
            final DatagramSocket socket=new DatagramSocket(0,InetAddress.getByName("127.0.0.1"));socket.setSoTimeout(3000);
            final CountDownLatch ready=new CountDownLatch(1);final Throwable[] result={null};
            Thread blocked=new Thread(new Runnable(){public void run(){ready.countDown();try{socket.receive(new DatagramPacket(new byte[8],8));}catch(Throwable t){result[0]=t;}}},"close-control");
            blocked.start();ready.await(1,TimeUnit.SECONDS);Thread.sleep(100);long t=SystemClock.elapsedRealtime();socket.close();blocked.join(3500);
            require(!blocked.isAlive(),"receive did not finish");require(SystemClock.elapsedRealtime()-t<1000,"close did not promptly unblock");
            require(result[0] instanceof SocketException,"expected SocketException, got "+result[0]);
        }});
        System.err.println("[GAP_NET] COMPLETE");
    }
}
