#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {

constexpr int maximum_chain_certificates = 64;
constexpr int maximum_certificate_bytes = 1024 * 1024;
constexpr std::size_t maximum_hostname_bytes = 253;

struct certificate_context_deleter {
    void operator()(CERT_CONTEXT const* const context) const noexcept {
        if (context != nullptr) {
            CertFreeCertificateContext(context);
        }
    }
};

struct certificate_chain_deleter {
    void operator()(CERT_CHAIN_CONTEXT const* const context) const noexcept {
        if (context != nullptr) {
            CertFreeCertificateChain(context);
        }
    }
};

struct certificate_store_deleter {
    void operator()(void* const store) const noexcept {
        if (store != nullptr) {
            CertCloseStore(store, 0);
        }
    }
};

using certificate_context =
    std::unique_ptr<CERT_CONTEXT const, certificate_context_deleter>;
using certificate_chain =
    std::unique_ptr<CERT_CHAIN_CONTEXT const, certificate_chain_deleter>;
using certificate_store =
    std::unique_ptr<void, certificate_store_deleter>;

int reject(X509_STORE_CTX* const context) {
    X509_STORE_CTX_set_error(context, X509_V_ERR_CERT_REJECTED);
    return 0;
}

certificate_context convert_certificate(X509* const certificate) {
    const auto length = i2d_X509(certificate, nullptr);
    if (length <= 0 || length > maximum_certificate_bytes) {
        return {};
    }
    std::vector<unsigned char> encoded(static_cast<std::size_t>(length));
    auto* cursor = encoded.data();
    if (i2d_X509(certificate, &cursor) != length) {
        return {};
    }
    return certificate_context(CertCreateCertificateContext(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        encoded.data(),
        static_cast<DWORD>(encoded.size())
    ));
}

bool add_certificate(
    HCERTSTORE const store,
    X509* const certificate
) {
    auto converted = convert_certificate(certificate);
    return converted != nullptr
        && CertAddCertificateContextToStore(
            store,
            converted.get(),
            CERT_STORE_ADD_ALWAYS,
            nullptr
        ) != FALSE;
}

int verify_with_windows_trust(X509_STORE_CTX* const context, void*) {
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
    const auto wide_length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        hostname,
        static_cast<int>(hostname_length),
        nullptr,
        0
    );
    if (wide_length <= 0) {
        return reject(context);
    }
    std::vector<wchar_t> wide_hostname(static_cast<std::size_t>(wide_length) + 1U);
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            hostname,
            static_cast<int>(hostname_length),
            wide_hostname.data(),
            wide_length
        ) != wide_length) {
        return reject(context);
    }

    auto* const leaf = X509_STORE_CTX_get0_cert(context);
    auto* const untrusted = X509_STORE_CTX_get0_untrusted(context);
    const auto untrusted_count = untrusted == nullptr ? 0 : sk_X509_num(untrusted);
    if (leaf == nullptr || untrusted_count < 0 || untrusted_count >= maximum_chain_certificates) {
        return reject(context);
    }
    auto windows_leaf = convert_certificate(leaf);
    certificate_store intermediates(CertOpenStore(
        CERT_STORE_PROV_MEMORY,
        0,
        0,
        CERT_STORE_CREATE_NEW_FLAG,
        nullptr
    ));
    if (windows_leaf == nullptr || intermediates == nullptr) {
        return reject(context);
    }
    for (int index = 0; index < untrusted_count; ++index) {
        auto* const certificate = sk_X509_value(untrusted, index);
        if (certificate != leaf && !add_certificate(intermediates.get(), certificate)) {
            return reject(context);
        }
    }

    CERT_CHAIN_PARA chain_parameters{};
    chain_parameters.cbSize = sizeof(chain_parameters);
    PCCERT_CHAIN_CONTEXT raw_chain = nullptr;
    constexpr DWORD chain_flags =
        CERT_CHAIN_CACHE_END_CERT | CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL;
    if (CertGetCertificateChain(
            nullptr,
            windows_leaf.get(),
            nullptr,
            intermediates.get(),
            &chain_parameters,
            chain_flags,
            nullptr,
            &raw_chain
        ) == FALSE) {
        return reject(context);
    }
    certificate_chain chain(raw_chain);

    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_parameters{};
    ssl_parameters.cbSize = sizeof(ssl_parameters);
    ssl_parameters.dwAuthType = AUTHTYPE_SERVER;
    ssl_parameters.pwszServerName = wide_hostname.data();
    CERT_CHAIN_POLICY_PARA policy_parameters{};
    policy_parameters.cbSize = sizeof(policy_parameters);
    policy_parameters.pvExtraPolicyPara = &ssl_parameters;
    CERT_CHAIN_POLICY_STATUS policy_status{};
    policy_status.cbSize = sizeof(policy_status);
    if (CertVerifyCertificateChainPolicy(
            CERT_CHAIN_POLICY_SSL,
            chain.get(),
            &policy_parameters,
            &policy_status
        ) == FALSE || policy_status.dwError != 0) {
        return reject(context);
    }

    X509_STORE_CTX_set_error(context, X509_V_OK);
    return 1;
}

}

extern "C" void nuvio_windows_configure_tls_trust(SSL_CTX* const context) {
    if (context != nullptr) {
        SSL_CTX_set_cert_verify_callback(context, verify_with_windows_trust, nullptr);
    }
}
