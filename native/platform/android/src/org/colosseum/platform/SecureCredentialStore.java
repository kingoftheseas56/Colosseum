package org.colosseum.platform;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.nio.ByteBuffer;
import java.security.KeyStore;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public final class SecureCredentialStore {
    private static final String PREFS = "colosseum_secure_credentials_v1";
    private static final String KEY_ALIAS = "colosseum_account_credentials_v1";
    private static final String KEYSTORE = "AndroidKeyStore";
    private static final String TRANSFORMATION = "AES/GCM/NoPadding";
    private static final int GCM_TAG_BITS = 128;

    private SecureCredentialStore() {}

    public static boolean isAvailable(Context context) {
        if (context == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M)
            return false;
        try {
            getOrCreateKey();
            return true;
        } catch (Exception ignored) {
            return false;
        }
    }

    public static String read(Context context, String key) {
        if (context == null || key == null || key.isEmpty())
            return null;
        try {
            String encoded = preferences(context).getString(key, null);
            if (encoded == null)
                return null;
            byte[] clear = decrypt(Base64.decode(encoded, Base64.NO_WRAP));
            return Base64.encodeToString(clear, Base64.NO_WRAP);
        } catch (Exception ignored) {
            return null;
        }
    }

    public static boolean write(Context context, String key, String valueBase64) {
        if (context == null || key == null || key.isEmpty() || valueBase64 == null)
            return false;
        try {
            byte[] clear = Base64.decode(valueBase64, Base64.NO_WRAP);
            String encoded = Base64.encodeToString(encrypt(clear), Base64.NO_WRAP);
            return preferences(context).edit().putString(key, encoded).commit();
        } catch (Exception ignored) {
            return false;
        }
    }

    public static boolean remove(Context context, String key) {
        if (context == null || key == null || key.isEmpty())
            return false;
        return preferences(context).edit().remove(key).commit();
    }

    public static String keys(Context context, String prefix) {
        if (context == null)
            return "";
        String wanted = prefix == null ? "" : prefix;
        List<String> keys = new ArrayList<>();
        for (String key : preferences(context).getAll().keySet()) {
            if (key.startsWith(wanted))
                keys.add(key);
        }
        Collections.sort(keys);
        return String.join("\n", keys);
    }

    private static SharedPreferences preferences(Context context) {
        return context.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private static SecretKey getOrCreateKey() throws Exception {
        KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
        keyStore.load(null);
        if (keyStore.containsAlias(KEY_ALIAS))
            return (SecretKey) keyStore.getKey(KEY_ALIAS, null);

        KeyGenerator generator = KeyGenerator.getInstance(
            KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
        KeyGenParameterSpec spec = new KeyGenParameterSpec.Builder(
            KEY_ALIAS,
            KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .setRandomizedEncryptionRequired(true)
            .build();
        generator.init(spec);
        return generator.generateKey();
    }

    private static byte[] encrypt(byte[] clear) throws Exception {
        Cipher cipher = Cipher.getInstance(TRANSFORMATION);
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey());
        byte[] iv = cipher.getIV();
        byte[] ciphertext = cipher.doFinal(clear);
        if (iv == null || iv.length == 0 || iv.length > 255)
            throw new IllegalStateException("Invalid GCM IV");
        ByteBuffer packed = ByteBuffer.allocate(1 + iv.length + ciphertext.length);
        packed.put((byte) iv.length);
        packed.put(iv);
        packed.put(ciphertext);
        return packed.array();
    }

    private static byte[] decrypt(byte[] packed) throws Exception {
        if (packed == null || packed.length < 3)
            throw new IllegalArgumentException("Invalid encrypted credential");
        int ivLength = packed[0] & 0xff;
        if (ivLength == 0 || ivLength >= packed.length - 1)
            throw new IllegalArgumentException("Invalid encrypted credential IV");

        byte[] iv = new byte[ivLength];
        byte[] ciphertext = new byte[packed.length - 1 - ivLength];
        System.arraycopy(packed, 1, iv, 0, ivLength);
        System.arraycopy(packed, 1 + ivLength, ciphertext, 0, ciphertext.length);

        Cipher cipher = Cipher.getInstance(TRANSFORMATION);
        cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(),
            new GCMParameterSpec(GCM_TAG_BITS, iv));
        return cipher.doFinal(ciphertext);
    }
}
