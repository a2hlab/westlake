package org.westlake.gapcontrol;
import java.net.URL;
import java.security.*;
import java.security.cert.*;
import javax.net.ssl.*;
import android.net.http.X509TrustManagerExtensions;
final class TrustProbe implements Runnable {
    void log(String s){System.err.println("[GAP_TRUST] "+s);}
    public void run(){
        for(String name:new String[]{"EC","1.2.840.10045.2.1"})try{log("keyfactory "+name+"="+KeyFactory.getInstance(name).getProvider());}catch(Throwable t){log("KEYFACTORY_FAIL "+name+"="+t);}
        for(String name:new String[]{"SHA256withECDSA","1.2.840.10045.4.3.2","SHA384withECDSA"})try{log("signature "+name+"="+Signature.getInstance(name).getProvider());}catch(Throwable t){log("SIGNATURE_FAIL "+name+"="+t);}
        try{SecureRandom r=new SecureRandom();byte[] b=new byte[32];r.nextBytes(b);log("random="+r.getAlgorithm()+" provider="+r.getProvider().getName());}
        catch(Throwable t){log("RANDOM_FAIL="+t);t.printStackTrace();}
        HttpsURLConnection c=null;
        try{
            TrustManagerFactory f=TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());f.init((KeyStore)null);
            X509TrustManager tm=(X509TrustManager)f.getTrustManagers()[0];log("manager="+tm.getClass().getName()+" roots="+tm.getAcceptedIssuers().length);
            X509TrustManagerExtensions extension=new X509TrustManagerExtensions(tm);log("extension=OK");
            c=(HttpsURLConnection)new URL("https://example.com/").openConnection();c.setConnectTimeout(10000);c.setReadTimeout(10000);log("status="+c.getResponseCode());
            java.security.cert.Certificate[] certificates=c.getServerCertificates();X509Certificate[] chain=new X509Certificate[certificates.length];
            for(int i=0;i<chain.length;i++){chain[i]=(X509Certificate)certificates[i];if(chain[i].getPublicKey()==null)throw new AssertionError("public key missing");log("chain "+i+" alg="+chain[i].getSigAlgName()+" issuer="+chain[i].getIssuerX500Principal());}
            if(chain.length<2)throw new AssertionError("example.com intermediate chain missing");
            log("direct validated="+extension.checkServerTrusted(chain,chain[0].getPublicKey().getAlgorithm(),"example.com").size());
            log("PASS full-standard-chain count="+chain.length);
        }catch(Throwable t){log("FAIL="+t);t.printStackTrace();}finally{if(c!=null)c.disconnect();}
    }
}
