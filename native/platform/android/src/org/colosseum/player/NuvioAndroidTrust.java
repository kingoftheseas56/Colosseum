package org.colosseum.player;

import android.os.Build;
import android.system.Os;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyStore;
import java.security.cert.Certificate;
import java.security.cert.CertificateEncodingException;
import java.util.Base64;
import java.util.Enumeration;

/**
 * Narrow JNI helper for the opt-in Nuvio playback backend: exports the
 * Android system CA store to a private PEM bundle before the native engine is
 * created, mirroring Nuvio's AndroidTrustStore. Certificate verification is
 * never disabled; the engine only ever receives this explicit bundle path.
 */
public final class NuvioAndroidTrust {
    private static final String TEMP_SUFFIX = ".tmp";
    private static final int MAX_CERTIFICATES = 512;
    private static final int MAX_BUNDLE_BYTES = 4 * 1024 * 1024;

    private NuvioAndroidTrust() {
    }

    /** Exports the Android system trust store to {@code pemPath} atomically. */
    public static boolean exportTo(String pemPath) {
        try {
            File target = new File(pemPath);
            File temp = new File(pemPath + TEMP_SUFFIX);
            StringBuilder builder = new StringBuilder();
            KeyStore store = KeyStore.getInstance("AndroidCAStore");
            store.load(null);
            int exported = 0;
            for (Enumeration<String> aliases = store.aliases();
                    aliases.hasMoreElements() && exported < MAX_CERTIFICATES; ) {
                Certificate certificate = store.getCertificate(aliases.nextElement());
                if (certificate == null) {
                    continue;
                }
                appendPem(builder, certificate);
                exported++;
                if (builder.length() > MAX_BUNDLE_BYTES) {
                    break;
                }
            }
            try (FileOutputStream output = new FileOutputStream(temp)) {
                output.write(builder.toString().getBytes(StandardCharsets.US_ASCII));
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                Files.move(temp.toPath(), target.toPath(),
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            } else {
                if (target.exists() && !target.delete()) {
                    return false;
                }
                if (!temp.renameTo(target)) {
                    return false;
                }
            }
            Os.chmod(target.getAbsolutePath(), 0600);
            return true;
        } catch (Exception error) {
            // KeyStore/GeneralSecurityException, IOException and
            // android.system.ErrnoException from chmod all mean "export
            // failed"; the backend stays unavailable rather than running
            // without a system trust bundle.
            return false;
        }
    }

    private static void appendPem(StringBuilder builder, Certificate certificate)
            throws CertificateEncodingException {
        byte[] encoded = certificate.getEncoded();
        builder.append("-----BEGIN CERTIFICATE-----\n");
        String base64 = Base64.getEncoder().encodeToString(encoded);
        for (int index = 0; index < base64.length(); index += 64) {
            builder.append(base64, index, Math.min(index + 64, base64.length()));
            builder.append('\n');
        }
        builder.append("-----END CERTIFICATE-----\n");
    }
}
