// src/tls/certificate.cpp
// DER-bytes-in → Certificate-out parse helper.
// Anchors: data-model.md E-12 / [2g §4.5] (Certificate value-typed view).
//          [2a §4.2] trap_throw pattern for OpenSSL error conversion.
//          [const §VIII.5] — no allocation outside caller-supplied PMR.
//          [arch §5.3] — no exceptions across the session-handling window.
//
// Implements: fixpp::tls::parse_certificate_der.
// Does NOT implement: PEM parsing (Phase 4 / file_cert_source), validation
// (verify_peer, Phase 5), or cipher checks (CipherPolicy, Phase 3).

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/certificate.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace fixpp::tls {

namespace {

// RAII wrapper around X509* that calls X509_free on destruction.
struct X509Deleter {
    void operator()(X509* p) const noexcept { X509_free(p); }
};
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

// Copy a C-string (possibly null) into the PMR arena.  Returns a string_view
// into that arena-owned storage.  The caller must ensure `mr` outlives the
// returned view.
static std::string_view pmr_copy_str(const char* src, std::size_t len,
                                     std::pmr::memory_resource& mr) {
    if (len == 0) return {};
    void* p = mr.allocate(len, alignof(char));
    std::memcpy(p, src, len);
    return {static_cast<const char*>(p), len};
}

// Convert an OpenSSL ASN1_TIME to system_clock::time_point.
// Returns the epoch on failure (not a hard error; verify_peer will reject it).
static std::chrono::system_clock::time_point asn1_time_to_tp(const ASN1_TIME* t) noexcept {
    if (!t) return {};
    struct tm tm_val{};
    if (ASN1_TIME_to_tm(t, &tm_val) != 1) return {};
    // timegm is POSIX; mktime interprets local time, timegm interprets UTC.
#if defined(_WIN32)
    std::time_t tt = _mkgmtime(&tm_val);
#else
    std::time_t tt = timegm(&tm_val);
#endif
    if (tt == -1) return {};
    return std::chrono::system_clock::from_time_t(tt);
}

// Extract a one-line subject/issuer DN string from an X509_NAME.
// Returns "" on failure.  The returned string_view is allocated in `mr`.
static std::string_view extract_dn(X509_NAME* name, std::pmr::memory_resource& mr) {
    if (!name) return {};
    // BIO to render the name as a one-line string.
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return {};
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_ONELINE);
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(bio, &bptr);
    std::string_view result;
    if (bptr && bptr->length > 0) {
        result = pmr_copy_str(bptr->data, bptr->length, mr);
    }
    BIO_free(bio);
    return result;
}

}  // namespace

// ── parse_certificate_der ─────────────────────────────────────────────────────
// Public entry point: parse raw DER bytes into a Certificate, allocating all
// string/span storage into `mr`.  The raw DER bytes themselves are NOT copied;
// the caller owns them and must ensure they outlive any Certificate produced.
//
// On parse failure → unexpected{error::tls_cert_parse_failed}.
// On success       → Certificate with all view fields pointing into `mr`.
[[nodiscard]] core::expected_t<Certificate> parse_certificate_der(
    std::span<const std::byte> der, std::pmr::memory_resource& mr) noexcept {
    if (der.empty()) {
        return std::unexpected{core::error::tls_cert_parse_failed};
    }

    // d2i_X509 consumes DER bytes.
    const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
    X509Ptr cert{d2i_X509(nullptr, &p, static_cast<long>(der.size()))};
    if (!cert) {
        return std::unexpected{core::error::tls_cert_parse_failed};
    }

    Certificate out{};
    // raw_der_ is a back-pointer into the caller's storage (not copied).
    out.raw_der_ = der;

    // ── SHA-256 of raw DER ────────────────────────────────────────────────────
    {
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(der.data()), der.size(), digest);
        static_assert(SHA256_DIGEST_LENGTH == 32);
        std::memcpy(out.sha256_.data(), digest, 32);
    }

    // ── Subject DN ───────────────────────────────────────────────────────────
    out.subject_dn_ = extract_dn(X509_get_subject_name(cert.get()), mr);

    // ── Issuer DN ────────────────────────────────────────────────────────────
    out.issuer_dn_ = extract_dn(X509_get_issuer_name(cert.get()), mr);

    // ── X.509 version (API returns version-1) ────────────────────────────────
    out.x509_version_ = static_cast<int>(X509_get_version(cert.get())) + 1;

    // ── Validity window ───────────────────────────────────────────────────────
    out.not_before_ = asn1_time_to_tp(X509_get0_notBefore(cert.get()));
    out.not_after_ = asn1_time_to_tp(X509_get0_notAfter(cert.get()));

    // ── Signature algorithm + key details ────────────────────────────────────
    {
        EVP_PKEY* pkey = X509_get0_pubkey(cert.get());
        if (pkey) {
            const int id = EVP_PKEY_get_base_id(pkey);
            if (id == EVP_PKEY_RSA || id == EVP_PKEY_RSA_PSS) {
                out.alg_ = signature_algorithm::rsa_pss;
                out.rsa_key_bits_ = static_cast<std::size_t>(EVP_PKEY_get_bits(pkey));
            } else if (id == EVP_PKEY_EC) {
                out.alg_ = signature_algorithm::ecdsa;
                // Determine the curve via EC_GROUP.
                const EC_GROUP* grp = nullptr;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                // OpenSSL 3.x: use EVP_PKEY_get_group_name
                char gname[64]{};
                std::size_t gname_len = 0;
                if (EVP_PKEY_get_group_name(pkey, gname, sizeof(gname), &gname_len) == 1) {
                    std::string_view gv{gname, gname_len};
                    if (gv == "prime256v1" || gv == "P-256") {
                        out.curve_ = ecdsa_curve::p256;
                    } else if (gv == "secp384r1" || gv == "P-384") {
                        out.curve_ = ecdsa_curve::p384;
                    }
                }
                (void)grp;
#else
                EC_KEY* ec = EVP_PKEY_get0_EC_KEY(pkey);
                if (ec) {
                    grp = EC_KEY_get0_group(ec);
                    if (grp) {
                        const int nid = EC_GROUP_get_curve_name(grp);
                        if (nid == NID_X9_62_prime256v1) {
                            out.curve_ = ecdsa_curve::p256;
                        } else if (nid == NID_secp384r1) {
                            out.curve_ = ecdsa_curve::p384;
                        }
                    }
                }
#endif
            }
        }
    }

    // ── SAN entries (DNS + URI) ───────────────────────────────────────────────
    // We collect into temporary vectors first, then copy the string storage into
    // `mr` and build a PMR-allocated array of string_view for the spans.
    {
        GENERAL_NAMES* gens = static_cast<GENERAL_NAMES*>(
            X509_get_ext_d2i(cert.get(), NID_subject_alt_name, nullptr, nullptr));
        if (gens) {
            // Two-pass: first count, then allocate.
            int total = sk_GENERAL_NAME_num(gens);

            // Temporary vectors to collect string_views into mr-owned storage.
            // These themselves live on the native stack/heap — they're temporary.
            std::vector<std::string_view> dns_views;
            std::vector<std::string_view> uri_views;
            dns_views.reserve(static_cast<std::size_t>(total));
            uri_views.reserve(static_cast<std::size_t>(total));

            for (int i = 0; i < total; ++i) {
                GENERAL_NAME* gn = sk_GENERAL_NAME_value(gens, i);
                if (!gn) continue;
                if (gn->type == GEN_DNS) {
                    const char* s =
                        reinterpret_cast<const char*>(ASN1_STRING_get0_data(gn->d.dNSName));
                    const int slen = ASN1_STRING_length(gn->d.dNSName);
                    if (s && slen > 0) {
                        dns_views.push_back(pmr_copy_str(s, static_cast<std::size_t>(slen), mr));
                    }
                } else if (gn->type == GEN_URI) {
                    const char* s = reinterpret_cast<const char*>(
                        ASN1_STRING_get0_data(gn->d.uniformResourceIdentifier));
                    const int slen = ASN1_STRING_length(gn->d.uniformResourceIdentifier);
                    if (s && slen > 0) {
                        uri_views.push_back(pmr_copy_str(s, static_cast<std::size_t>(slen), mr));
                    }
                }
            }

            GENERAL_NAMES_free(gens);

            // Allocate PMR arrays of string_view for the spans.
            if (!dns_views.empty()) {
                void* p = mr.allocate(dns_views.size() * sizeof(std::string_view),
                                      alignof(std::string_view));
                auto* arr = static_cast<std::string_view*>(p);
                std::memcpy(arr, dns_views.data(), dns_views.size() * sizeof(std::string_view));
                out.san_dns_names_ = {arr, dns_views.size()};
            }
            if (!uri_views.empty()) {
                void* p = mr.allocate(uri_views.size() * sizeof(std::string_view),
                                      alignof(std::string_view));
                auto* arr = static_cast<std::string_view*>(p);
                std::memcpy(arr, uri_views.data(), uri_views.size() * sizeof(std::string_view));
                out.san_uris_ = {arr, uri_views.size()};
            }
        }
    }

    return out;
}

}  // namespace fixpp::tls
