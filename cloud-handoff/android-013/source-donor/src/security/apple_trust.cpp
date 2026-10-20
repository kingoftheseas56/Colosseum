#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace {

constexpr int maximum_chain_certificates = 64;
constexpr int maximum_certificate_bytes = 1024 * 1024;
constexpr std::size_t maximum_hostname_bytes = 253;

int reject(X509_STORE_CTX* const context) {
    X509_STORE_CTX_set_error(context, X509_V_ERR_CERT_REJECTED);
    return 0;
}

bool append_certificate(CFMutableArrayRef const certificates, X509* const certificate) {
    const auto length = i2d_X509(certificate, nullptr);
    if (length <= 0 || length > maximum_certificate_bytes) {
        return false;
    }
    std::vector<unsigned char> encoded(static_cast<std::size_t>(length));
    auto* cursor = encoded.data();
    if (i2d_X509(certificate, &cursor) != length) {
        return false;
    }
    const auto data = CFDataCreate(
        kCFAllocatorDefault,
        encoded.data(),
        static_cast<CFIndex>(encoded.size())
    );
    if (data == nullptr) {
        return false;
    }
    const auto apple_certificate = SecCertificateCreateWithData(
        kCFAllocatorDefault,
        data
    );
    CFRelease(data);
    if (apple_certificate == nullptr) {
        return false;
    }
    CFArrayAppendValue(certificates, apple_certificate);
    CFRelease(apple_certificate);
    return true;
}

int verify_with_apple_trust(X509_STORE_CTX* const context, void*) {
    if (context == nullptr) {
        return 0;
    }
    const auto ssl_index = SSL_get_ex_data_X509_STORE_CTX_idx();
    auto* const ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(context, ssl_index));
    const auto* const hostname = ssl == nullptr
        ? nullptr
        : SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (hostname == nullptr) {
        return reject(context);
    }
    const auto hostname_length = std::strlen(hostname);
    if (hostname_length == 0 || hostname_length > maximum_hostname_bytes) {
        return reject(context);
    }

    auto* const leaf = X509_STORE_CTX_get0_cert(context);
    auto* const untrusted = X509_STORE_CTX_get0_untrusted(context);
    const auto untrusted_count = untrusted == nullptr ? 0 : sk_X509_num(untrusted);
    if (leaf == nullptr || untrusted_count < 0 || untrusted_count >= maximum_chain_certificates) {
        return reject(context);
    }

    const auto certificates = CFArrayCreateMutable(
        kCFAllocatorDefault,
        untrusted_count + 1,
        &kCFTypeArrayCallBacks
    );
    if (certificates == nullptr || !append_certificate(certificates, leaf)) {
        if (certificates != nullptr) {
            CFRelease(certificates);
        }
        return reject(context);
    }
    for (int index = 0; index < untrusted_count; ++index) {
        auto* const certificate = sk_X509_value(untrusted, index);
        if (certificate != leaf && !append_certificate(certificates, certificate)) {
            CFRelease(certificates);
            return reject(context);
        }
    }

    const auto apple_hostname = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(hostname),
        static_cast<CFIndex>(hostname_length),
        kCFStringEncodingUTF8,
        false
    );
    if (apple_hostname == nullptr) {
        CFRelease(certificates);
        return reject(context);
    }
    const auto policy = SecPolicyCreateSSL(true, apple_hostname);
    CFRelease(apple_hostname);
    if (policy == nullptr) {
        CFRelease(certificates);
        return reject(context);
    }

    SecTrustRef trust = nullptr;
    const auto create_status = SecTrustCreateWithCertificates(
        certificates,
        policy,
        &trust
    );
    CFRelease(policy);
    CFRelease(certificates);
    if (create_status != errSecSuccess || trust == nullptr) {
        if (trust != nullptr) {
            CFRelease(trust);
        }
        return reject(context);
    }
    if (SecTrustSetNetworkFetchAllowed(trust, false) != errSecSuccess) {
        CFRelease(trust);
        return reject(context);
    }

    CFErrorRef error = nullptr;
    const auto trusted = SecTrustEvaluateWithError(trust, &error);
    if (error != nullptr) {
        CFRelease(error);
    }
    CFRelease(trust);
    if (!trusted) {
        return reject(context);
    }
    X509_STORE_CTX_set_error(context, X509_V_OK);
    return 1;
}

}

extern "C" void nuvio_apple_configure_tls_trust(SSL_CTX* const context) {
    if (context != nullptr) {
        SSL_CTX_set_cert_verify_callback(context, verify_with_apple_trust, nullptr);
    }
}
