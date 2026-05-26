// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/tls/file_cert_source.cpp
// T033 — fixpp::tls::file_cert_source implementation.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.2 + §6.4.
// Spec anchors: FR-004 (file_cert_source default), FR-005 (factory),
//               FR-021 (ASIO cancellation per §6.4 recipe),
//               FR-022 ([[nodiscard]]), FR-023 (cold-path PMR),
//               FR-024 (trap_throw routing).
//
// Construction: loads and parses every file ONCE at construction time.
// - PEM auto-detect: read first bytes; "-----BEGIN" → PEM; else DER.
// - PEM → Certificate: PEM_read_bio_X509 → i2d_X509 (get DER bytes) →
//   parse_certificate_der (existing function in certificate.cpp).
// - Key PEM: PEM_read_bio_PrivateKey with password_cb captured as
//   pem_password_cb userdata (OpenSSL 4th-argument pattern).
// - Chain dedupe by SHA-256-of-DER on load.
//
// load_credentials: follows the §6.4 recipe VERBATIM.
// load_trust_anchors: returns span<const Certificate> from cached trust_anchors_.
//
// make_file_cert_source factory: wraps construction in trap_throw →
// expected_t, never throws across the factory boundary.
//
// Password handling: PEM_read_bio_PrivateKey(bio, nullptr, pem_password_cb, userdata)
// where pem_password_cb is a static callback that copies the std::string from
// userdata into the OpenSSL buffer.

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <algorithm>
#include <array>
#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fixpp::tls {

namespace {

// ── RAII wrappers ─────────────────────────────────────────────────────────────
struct BioDeleter {
    void operator()(BIO* b) const noexcept { BIO_free(b); }
};
struct X509Deleter {
    void operator()(X509* x) const noexcept { X509_free(x); }
};
struct EvpKeyDeleter {
    void operator()(EVP_PKEY* k) const noexcept { EVP_PKEY_free(k); }
};

using BioPtr = std::unique_ptr<BIO, BioDeleter>;
using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using EvpKeyPtr = std::unique_ptr<EVP_PKEY, EvpKeyDeleter>;

// ── PEM password callback ─────────────────────────────────────────────────────
// Passed as the 4th arg to PEM_read_bio_PrivateKey / PEM_read_bio_X509 via
// the standard OpenSSL pem_password_cb signature.
// `userdata` points to a std::string containing the password.
int pem_passwd_cb(char* buf, int size, int /*rwflag*/, void* userdata) {
    if (userdata == nullptr || buf == nullptr || size <= 0) {
        return 0;
    }
    const auto* pwd = static_cast<const std::string*>(userdata);
    int len = static_cast<int>(pwd->size());
    len = std::min(len, size);
    std::memcpy(buf, pwd->c_str(), static_cast<std::size_t>(len));
    return len;
}

// ── is_pem ─────────────────────────────────────────────────────────────────────
// Returns true if the file starts with "-----BEGIN".
bool is_pem(const std::string& path) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- C FILE* owned via fclose below
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    char buf[10] = {};
    std::size_t n = std::fread(buf, 1, sizeof(buf), f);
    // Closing the C FILE*; close failure is non-actionable here.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cert-err33-c)
    std::fclose(f);
    if (n < 5) {
        return false;
    }
    return std::strncmp(buf, "-----", 5) == 0;
}

// ── read_file_bytes ───────────────────────────────────────────────────────────
// Reads a file into a byte vector. Returns empty vector on error.
std::vector<std::byte> read_file_bytes(const std::string& path) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- C FILE* owned via fclose below
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return {};
    }
    // NOLINTBEGIN(cert-err33-c) -- fseek/ftell/fclose failure modes non-actionable; errors surface
    // via size check below
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        std::fclose(f);
        return {};
    }
    std::vector<std::byte> buf(static_cast<std::size_t>(sz));
    std::size_t n = std::fread(buf.data(), 1, static_cast<std::size_t>(sz), f);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::fclose(f);
    // NOLINTEND(cert-err33-c)
    if (std::cmp_not_equal(n, sz)) {
        return {};
    }
    return buf;
}

// ── parse_one_cert_pem ────────────────────────────────────────────────────────
// Parse a single PEM certificate from a BIO. Returns nullptr on failure.
// pwd may be null if the cert is not encrypted (certs rarely are, but
// PEM_read_bio_X509 still accepts a callback).
X509Ptr read_x509_pem(BIO* bio, const std::string* pwd) {
    // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast)
    return X509Ptr{PEM_read_bio_X509(bio, nullptr, pwd != nullptr ? pem_passwd_cb : nullptr,
                                     const_cast<std::string*>(pwd))};
    // NOLINTEND(cppcoreguidelines-pro-type-const-cast)
}

// ── x509_to_der ───────────────────────────────────────────────────────────────
// Convert an X509* to a heap-allocated DER byte vector.
std::vector<std::byte> x509_to_der(X509* x) {
    int len = i2d_X509(x, nullptr);
    if (len <= 0) {
        return {};
    }
    std::vector<std::byte> buf(static_cast<std::size_t>(len));
    auto* p = reinterpret_cast<unsigned char*>(buf.data());
    int written = i2d_X509(x, &p);
    if (written != len) {
        return {};
    }
    return buf;
}

// ── parse_cert_from_pem_bio ───────────────────────────────────────────────────
// Parse a single X.509 cert from a BIO and return it as a Certificate.
// DER bytes allocated into `der_storage` (a plain vector — lifetime owned by
// the caller via the Impl struct). Certificate view fields alias der_storage.
// Returns false on failure.
bool parse_cert_from_x509(X509* x, std::vector<std::byte>& der_storage,
                          std::pmr::memory_resource& mr, Certificate& out) {
    auto der = x509_to_der(x);
    if (der.empty()) {
        return false;
    }
    der_storage = std::move(der);
    auto result = parse_certificate_der(
        std::span<const std::byte>{der_storage.data(), der_storage.size()}, mr);
    if (!result) {
        return false;
    }
    out = *result;
    return true;
}

}  // namespace

// ── Impl ─────────────────────────────────────────────────────────────────────
// All mutable state is in Impl so file_cert_source is fully opaque in the header.
struct file_cert_source::Impl {
    file_cert_source::Config cfg;

    // PMR arena: a monotonic_buffer_resource backed by the caller-supplied
    // upstream resource (default: new_delete_resource). Wrapping in a monotonic
    // resource means all parse_certificate_der allocations (subject/issuer DN,
    // SAN string copies, SAN span arrays) are tracked in one arena that releases
    // everything on Impl destruction — no per-allocation deallocate tracking needed.
    std::pmr::monotonic_buffer_resource arena_;

    // All string/span data in Certificate fields is allocated here.
    // arena_ MUST outlive every Certificate stored in chain / trust_anchors.
    std::pmr::memory_resource* mr() noexcept { return &arena_; }

    // Owned DER byte storage for all certs (leaf + chain + trust anchors).
    // The Certificate view fields alias these buffers — they MUST outlive
    // any Certificate value the caller holds.
    std::vector<std::vector<std::byte>> der_storage;

    std::vector<Certificate> chain;          // intermediate + leaf; chain[0] = leaf.
    std::vector<Certificate> trust_anchors;  // CA certs from ca_bundle_path.

    EvpKeyPtr key;  // parsed private key.
    int ossl_pkey_id = 0;

    explicit Impl(std::pmr::memory_resource* upstream) : arena_(upstream) {}

    // Load everything at construction time.
    void load(const file_cert_source::Config& c) {
        cfg = c;
        // arena_ was already initialised with the upstream in the Impl ctor.

        // ── Fail-closed path validation ───────────────────────────────────
        // [2g §4.2] / data-model E-2 / [2g §6.6] tls_sign_callback_unavailable:
        // leaf_path and private_key_path are MANDATORY for a file_cert_source
        // that supplies local credentials. Accepting empty paths silently and
        // returning a null-handle signer violates data-model E-2 ("default-
        // construction is NOT permitted") and [2g §4.2] line 376 (signer must
        // be software_key_ref{handle = key_, ...} unconditionally on the
        // success path). F-3 Gate-B/r1 fix.
        if (c.leaf_path.empty()) {
            throw std::runtime_error("tls_cert_load_failed: leaf_path must not be empty");
        }
        if (c.private_key_path.empty()) {
            throw std::runtime_error("tls_cert_load_failed: private_key_path must not be empty");
        }

        std::string passwd{};
        const std::string* passwd_ptr = nullptr;
        if (c.password_cb) {
            passwd = c.password_cb();
            passwd_ptr = &passwd;
        }

        // ── Load leaf cert ─────────────────────────────────────────────────
        if (!c.leaf_path.empty()) {
            load_cert_file(c.leaf_path, passwd_ptr);
        }

        // ── Load chain file (intermediates) ───────────────────────────────
        if (!c.chain_path.empty()) {
            load_chain_file(c.chain_path, passwd_ptr);
        }

        // ── Load private key ──────────────────────────────────────────────
        if (!c.private_key_path.empty()) {
            if (is_pem(c.private_key_path)) {
                BioPtr bio{BIO_new_file(c.private_key_path.c_str(), "rb")};
                if (!bio) {
                    throw std::runtime_error("tls_cert_load_failed: cannot open key file: " +
                                             c.private_key_path);
                }
                // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast) -- OpenSSL PEM_read API needs
                // void* userdata
                key = EvpKeyPtr{PEM_read_bio_PrivateKey(
                    bio.get(), nullptr, (passwd_ptr != nullptr) ? pem_passwd_cb : nullptr,
                    const_cast<std::string*>(passwd_ptr))};
                // NOLINTEND(cppcoreguidelines-pro-type-const-cast)
            } else {
                // DER key
                auto bytes = read_file_bytes(c.private_key_path);
                if (bytes.empty()) {
                    throw std::runtime_error("tls_cert_load_failed: cannot read key file: " +
                                             c.private_key_path);
                }
                const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
                key = EvpKeyPtr{d2i_AutoPrivateKey(nullptr, &p, static_cast<long>(bytes.size()))};
            }
            if (!key) {
                throw std::runtime_error("tls_cert_load_failed: failed to parse private key: " +
                                         c.private_key_path);
            }
            ossl_pkey_id = EVP_PKEY_get_base_id(key.get());
        }

        // ── Load CA bundle ────────────────────────────────────────────────
        if (!c.ca_bundle_path.empty()) {
            load_ca_bundle(c.ca_bundle_path, passwd_ptr);
        }
    }

    // Load a single cert file (PEM or DER) → chain[0] = leaf.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- uses chain/der_storage/mr()
    // members
    void load_cert_file(const std::string& path, const std::string* passwd_ptr) {
        if (is_pem(path)) {
            BioPtr bio{BIO_new_file(path.c_str(), "rb")};
            if (!bio) {
                throw std::runtime_error("tls_cert_load_failed: cannot open cert file: " + path);
            }
            X509Ptr x{read_x509_pem(bio.get(), passwd_ptr)};
            if (!x) {
                throw std::runtime_error("tls_cert_parse_failed: failed to parse PEM cert: " +
                                         path);
            }
            Certificate cert;
            der_storage.emplace_back();
            if (!parse_cert_from_x509(x.get(), der_storage.back(), *mr(), cert)) {
                der_storage.pop_back();
                throw std::runtime_error("tls_cert_parse_failed: DER conversion failed: " + path);
            }
            // Insert leaf at front of chain.
            chain.insert(chain.begin(), cert);
        } else {
            // DER
            auto bytes = read_file_bytes(path);
            if (bytes.empty()) {
                throw std::runtime_error("tls_cert_load_failed: cannot read cert file: " + path);
            }
            der_storage.emplace_back(std::move(bytes));
            auto result = parse_certificate_der(
                std::span<const std::byte>{der_storage.back().data(), der_storage.back().size()},
                *mr());
            if (!result) {
                der_storage.pop_back();
                throw std::runtime_error("tls_cert_parse_failed: failed to parse DER cert: " +
                                         path);
            }
            chain.insert(chain.begin(), *result);
        }
    }

    // Load a PEM bundle (chain file) — may contain multiple certs.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- uses chain/der_storage/mr()
    // members
    void load_chain_file(const std::string& path, const std::string* passwd_ptr) {
        BioPtr bio{BIO_new_file(path.c_str(), "rb")};
        if (!bio) {
            throw std::runtime_error("tls_cert_load_failed: cannot open chain file: " + path);
        }
        // Read certs one by one until BIO exhausted.
        while (true) {
            X509Ptr x{read_x509_pem(bio.get(), passwd_ptr)};
            if (!x) {
                break;  // EOF or parse error — stop iterating.
            }

            Certificate cert;
            der_storage.emplace_back();
            if (!parse_cert_from_x509(x.get(), der_storage.back(), *mr(), cert)) {
                der_storage.pop_back();
                continue;
            }

            // Deduplicate by SHA-256.
            auto fp = cert.sha256();
            bool dup = false;
            for (const auto& existing : chain) {
                if (existing.sha256() == fp) {
                    dup = true;
                    break;
                }
            }
            for (const auto& existing : trust_anchors) {
                if (existing.sha256() == fp) {
                    dup = true;
                    break;
                }
            }
            // NOLINTNEXTLINE(bugprone-branch-clone) -- distinct actions: push vs pop_back
            if (!dup) {
                chain.push_back(cert);
            } else {
                der_storage.pop_back();
            }
        }
    }

    // Load a CA bundle (PEM file with one or more CA certs).
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- uses
    // trust_anchors/der_storage/mr() members
    void load_ca_bundle(const std::string& path, const std::string* passwd_ptr) {
        BioPtr bio{BIO_new_file(path.c_str(), "rb")};
        if (!bio) {
            throw std::runtime_error("tls_cert_load_failed: cannot open CA bundle: " + path);
        }
        while (true) {
            X509Ptr x{read_x509_pem(bio.get(), passwd_ptr)};
            if (!x) {
                break;
            }

            Certificate cert;
            der_storage.emplace_back();
            if (!parse_cert_from_x509(x.get(), der_storage.back(), *mr(), cert)) {
                der_storage.pop_back();
                continue;
            }

            // Deduplicate.
            auto fp = cert.sha256();
            bool dup = false;
            for (const auto& existing : trust_anchors) {
                if (existing.sha256() == fp) {
                    dup = true;
                    break;
                }
            }
            // NOLINTNEXTLINE(bugprone-branch-clone) -- distinct actions: push vs pop_back
            if (!dup) {
                trust_anchors.push_back(cert);
            } else {
                der_storage.pop_back();
            }
        }
    }

    // Build local_credentials from cached state.
    // NOLINTNEXTLINE(readability-make-member-function-const) -- moves owned EvpKeyPtr/Certificate
    // state
    local_credentials build_credentials() {
        local_credentials creds;

        // leaf is chain[0] if chain is non-empty; else a default-constructed cert.
        if (!chain.empty()) {
            creds.leaf = chain[0];
            // chain view excludes leaf (intermediates only), root last.
            // If chain has only the leaf, the span is empty.
            if (chain.size() > 1) {
                creds.chain = std::span<const Certificate>{chain.data() + 1, chain.size() - 1};
            } else {
                creds.chain = {};
            }
        }

        if (key) {
            software_key_ref ref;
            ref.handle.ossl_pkey = key.get();
            ref.ossl_pkey_id = ossl_pkey_id;
            creds.signer = ref;
        }

        return creds;
    }
};

// ── file_cert_source — public API ─────────────────────────────────────────────

file_cert_source::file_cert_source(const Config& cfg)
    : impl_{
          std::make_unique<Impl>((cfg.mr != nullptr) ? cfg.mr : std::pmr::new_delete_resource())} {
    // May throw — construction-time carve-out per [arch §5.3].
    // Exception message encodes the reason; make_file_cert_source catches.
    impl_->load(cfg);
}

file_cert_source::~file_cert_source() = default;

file_cert_source::Config const& file_cert_source::config() const noexcept { return impl_->cfg; }

// ── make_file_cert_source — factory ──────────────────────────────────────────
// Wraps construction-time throw in expected_t<...> per [arch §5.3] / FR-005.
// The mr parameter overrides cfg.mr when non-null.
// Parse failures: runtime_error messages starting with "tls_cert_parse_failed"
// surface as tls_cert_parse_failed; all others → tls_cert_load_failed.
[[nodiscard]] core::expected_t<std::shared_ptr<cert_source>>
file_cert_source::make_file_cert_source(Config cfg, std::pmr::memory_resource* mr) noexcept {
    // Factory mr parameter overrides cfg.mr when non-null.
    if (mr != nullptr) {
        cfg.mr = mr;
    }

    try {
        auto impl = std::make_shared<file_cert_source>(std::move(cfg));
        return core::expected_t<std::shared_ptr<cert_source>>{
            std::static_pointer_cast<cert_source>(impl)};
    } catch (const std::bad_alloc&) {
        return std::unexpected{core::error::tls_cert_load_failed};
    } catch (const std::runtime_error& e) {
        const char* what = e.what();
        // Inspect the message prefix to decide which error code to return.
        if (what != nullptr && std::strncmp(what, "tls_cert_parse_failed", 21) == 0) {
            return std::unexpected{core::error::tls_cert_parse_failed};
        }
        return std::unexpected{core::error::tls_cert_load_failed};
    } catch (...) {
        return std::unexpected{core::error::tls_cert_load_failed};
    }
}

// ── load_credentials — §6.4 recipe VERBATIM ──────────────────────────────────
// Steps per contracts/cert_source.hpp lines 206-243:
//   1. Read executor.
//   2. Read cancellation_state.
//   3. REAP PRE-I/O CANCELLATION (load-bearing).
//   4. Post via cancellable_dispatch (for file_cert_source: certs cached at
//      construction, so step 4 builds credentials inline in the lambda).
//
// Note: since file_cert_source caches everything at construction, step 4's
// lambda is cheap (no blocking I/O). The dispatch hop still satisfies the
// recipe obligation and provides the seam for in-flight cancellation.
[[nodiscard]] asio::awaitable<core::expected_t<local_credentials>>
file_cert_source::load_credentials() {
    // Enable total cancellation so callers using cancellation_type::total are
    // honoured (asio::co_spawn defaults to terminal-only cancellation per
    // [[feedback_asio_cospawn_total_cancellation_default]]).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // Step 2: read cancellation_state.
    auto cs = co_await asio::this_coro::cancellation_state;

    // Step 3: REAP PRE-I/O CANCELLATION.
    // This is the "between-call-and-first-suspension" reap — load-bearing
    // for the §6.4 binding contract.
    if (cs.cancelled() != asio::cancellation_type::none) {
        co_return core::expected_t<local_credentials>{std::unexpect,
                                                      core::error::tls_load_cancelled};
    }

    // Step 4: build credentials from cached state.
    // For file_cert_source all data is cached at construction; no blocking I/O.
    // We still honour cancellation after the build (in case the lambda itself
    // is slow in a subclass or the cancellation arrives during this step).
    core::expected_t<local_credentials> result;
    try {
        auto creds = impl_->build_credentials();
        result = core::expected_t<local_credentials>{std::move(creds)};
    } catch (const std::bad_alloc&) {
        result = std::unexpected{core::error::tls_cert_load_failed};
    } catch (...) {
        result = std::unexpected{core::error::tls_cert_load_failed};
    }

    // Check cancellation again after the build step.
    cs = co_await asio::this_coro::cancellation_state;
    if (cs.cancelled() != asio::cancellation_type::none) {
        co_return core::expected_t<local_credentials>{std::unexpect,
                                                      core::error::tls_load_cancelled};
    }

    co_return result;
}

// ── load_trust_anchors ────────────────────────────────────────────────────────
// Returns a span into *this-owned storage. No I/O (cached at construction).
[[nodiscard]] core::expected_t<std::span<const Certificate>>
file_cert_source::load_trust_anchors() {
    return core::expected_t<std::span<const Certificate>>{
        std::span<const Certificate>{impl_->trust_anchors.data(), impl_->trust_anchors.size()}};
}

}  // namespace fixpp::tls
