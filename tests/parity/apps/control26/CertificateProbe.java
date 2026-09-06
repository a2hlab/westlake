package org.westlake.gapcontrol;
import android.content.Context;
import java.io.*;
import java.security.*;
import java.security.cert.*;
import javax.net.ssl.*;

final class CertificateProbe implements Runnable {
    final Context context;CertificateProbe(Context c){context=c;}
    X509Certificate read(String name)throws Exception{
        try(InputStream in=context.getAssets().open("certs/"+name+".pem")){
            return (X509Certificate)CertificateFactory.getInstance("X.509").generateCertificate(in);
        }
    }
    void check(String name,X509Certificate root,X509Certificate leaf,boolean accept)throws Exception{
        KeyStore store=KeyStore.getInstance(KeyStore.getDefaultType());store.load(null);store.setCertificateEntry("fixture",root);
        TrustManagerFactory factory=TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());factory.init(store);
        X509TrustManager manager=(X509TrustManager)factory.getTrustManagers()[0];boolean passed;
        try{manager.checkServerTrusted(new X509Certificate[]{leaf},"ECDHE_ECDSA");passed=true;}
        catch(CertificateException rejected){passed=false;}
        System.err.println("[GAP_CERT] "+(passed==accept?"PASS ":"FAIL ")+name+" accepted="+passed);
    }
    public void run(){try{
        X509Certificate root=read("root"),valid=read("valid");
        check("valid EC chain",root,valid,true);
        check("untrusted EC root",read("other"),valid,false);
        check("expired EC leaf",root,read("expired"),false);
        byte[] corrupt=valid.getEncoded();corrupt[corrupt.length-1]^=1;
        X509Certificate tampered=(X509Certificate)CertificateFactory.getInstance("X.509").generateCertificate(new ByteArrayInputStream(corrupt));
        check("tampered EC signature",root,tampered,false);
        System.err.println("[GAP_CERT] COMPLETE");
    }catch(Throwable t){System.err.println("[GAP_CERT] FAIL infrastructure "+t);t.printStackTrace();}}
}
