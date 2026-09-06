package org.westlake.gapcontrol;
import java.security.SecureRandom;
import java.util.Arrays;
import javax.crypto.*;
import javax.crypto.spec.IvParameterSpec;

/** Exercises startup ordering and real JCA encryption, without printing secrets. */
public final class CryptoProbe implements Runnable {
    public void run() {
        try {
            SecureRandom random=new SecureRandom();
            byte[] a=new byte[64],b=new byte[64];random.nextBytes(a);random.nextBytes(b);
            if(Arrays.equals(a,b)||Arrays.equals(a,new byte[64]))throw new AssertionError("random smoke test");
            KeyGenerator generator=KeyGenerator.getInstance("AES");generator.init(256,random);
            SecretKey key=generator.generateKey();byte[] iv=new byte[16];random.nextBytes(iv);
            Cipher encrypt=Cipher.getInstance("AES/CBC/PKCS5Padding");
            encrypt.init(Cipher.ENCRYPT_MODE,key,new IvParameterSpec(iv));
            byte[] ciphertext=encrypt.doFinal(a);
            Cipher decrypt=Cipher.getInstance("AES/CBC/PKCS5Padding");
            decrypt.init(Cipher.DECRYPT_MODE,key,new IvParameterSpec(iv));
            if(!Arrays.equals(a,decrypt.doFinal(ciphertext)))throw new AssertionError("AES round trip");
            System.err.println("[GAP_CRYPTO] PASS kernel_random_and_aes256_roundtrip provider="+random.getProvider().getName());
        } catch(Throwable error) {System.err.println("[GAP_CRYPTO] FAIL "+error);error.printStackTrace();}
    }
}
