package com.nuvio.engine.internal

import android.system.Os
import android.util.Base64
import java.io.File
import java.io.FileOutputStream
import java.nio.charset.StandardCharsets
import java.security.KeyStore
import java.security.cert.X509Certificate

internal object AndroidTrustStore {
    private const val MAXIMUM_CERTIFICATES = 4_096
    private const val MAXIMUM_CERTIFICATE_BYTES = 1024 * 1024
    private const val MAXIMUM_BUNDLE_BYTES = 16L * 1024L * 1024L
    private val beginCertificate = "-----BEGIN CERTIFICATE-----\n"
        .toByteArray(StandardCharsets.US_ASCII)
    private val endCertificate = "-----END CERTIFICATE-----\n"
        .toByteArray(StandardCharsets.US_ASCII)

    fun export(dataDirectory: File): File {
        val trustDirectory = File(dataDirectory, "tls")
        check(trustDirectory.mkdirs() || trustDirectory.isDirectory) {
            "could not create the engine TLS directory"
        }
        val destination = File(trustDirectory, "android-ca.pem")
        val temporary = File.createTempFile(".android-ca.pem-", ".tmp", trustDirectory)

        val keyStore = KeyStore.getInstance("AndroidCAStore").apply { load(null) }
        val aliases = keyStore.aliases().toList().sorted()
        check(aliases.isNotEmpty()) { "Android trust store contains no certificates" }
        check(aliases.size <= MAXIMUM_CERTIFICATES) { "Android trust store is unexpectedly large" }

        var written = 0L
        try {
            FileOutputStream(temporary, false).use { output ->
                for (alias in aliases) {
                    val certificate = keyStore.getCertificate(alias) as? X509Certificate ?: continue
                    val encoded = certificate.encoded
                    check(encoded.size <= MAXIMUM_CERTIFICATE_BYTES) {
                        "Android trust store contains an oversized certificate"
                    }
                    val base64 = Base64.encode(encoded, Base64.NO_WRAP)
                    val pemLineCount = (base64.size + PEM_LINE_LENGTH - 1) / PEM_LINE_LENGTH
                    val certificateSize =
                        beginCertificate.size + base64.size + pemLineCount + endCertificate.size
                    written += certificateSize
                    check(written <= MAXIMUM_BUNDLE_BYTES) {
                        "Android trust store exceeds the engine TLS bundle limit"
                    }
                    output.write(beginCertificate)
                    for (offset in base64.indices step PEM_LINE_LENGTH) {
                        val length = minOf(PEM_LINE_LENGTH, base64.size - offset)
                        output.write(base64, offset, length)
                        output.write('\n'.code)
                    }
                    output.write(endCertificate)
                }
                output.fd.sync()
            }
            check(written > 0) { "Android trust store contains no X.509 certificates" }
            Os.rename(temporary.absolutePath, destination.absolutePath)
        } finally {
            if (temporary.exists()) {
                temporary.delete()
            }
        }
        return destination
    }

    private const val PEM_LINE_LENGTH = 64
}
