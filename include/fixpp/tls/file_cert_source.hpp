#pragma once
// include/fixpp/tls/file_cert_source.hpp
// fixpp::tls::file_cert_source — default file-path cert_source implementation.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.2 lines 314-369.
// Spec anchors: FR-004 (file_cert_source default), FR-005 (factory),
//               FR-021 (ASIO cancellation), FR-022 ([[nodiscard]]),
//               FR-023 (cold-path PMR), FR-024 (trap_throw routing).
//
// Construction loads and parses every file once. Once construction returns,
// every cert_source method is exception-free per [arch §5.3] hot-path discipline.
// The factory make_file_cert_source wraps construction in expected_t<...>
// so non-construction-time callers (2i C ABI, future hot-reload) never see a
// thrown exception.

#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <string>

namespace fixpp::tls {

// ── file_cert_source ─────────────────────────────────────────────────────────
// Concrete cert_source that reads credentials from local files.
// PEM/DER auto-detect per file content ("-----BEGIN" magic → PEM; else DER).
// Chain deduplication on load (by SHA-256-of-DER).
// Encrypted PEM passphrase obtained via password_cb, invoked once at
// construction-time per [arch §5.3] carve-out.
//
// Per data-model E-1a verbatim from [2g §4.2] lines 314-369.
class file_cert_source final : public cert_source {
 public:
    struct Config {
        std::string                  leaf_path;               // PEM or DER (auto-detect by magic bytes).
        std::string                  chain_path;              // PEM bundle; intermediates only or leaf+intermediates (deduped on load).
        std::string                  private_key_path;        // PEM or DER; PKCS#8 or RFC1421-style.
        std::string                  ca_bundle_path;          // PEM bundle for CA trust anchors. May be empty for mtls_pinned.
        std::function<std::string()> password_cb;             // Optional; called once at construction-time. May be empty.
        std::pmr::memory_resource*   mr          {nullptr};   // PMR for parsed-cert storage. nullptr → new_delete_resource.
        std::size_t                  max_chain_depth    {8};
        std::size_t                  max_rsa_key_bits   {8192};
        std::size_t                  max_cert_der_bytes {16 * 1024};
        std::size_t                  max_san_entries    {64};
    };

    // [arch §6] rule-4 factory. Wraps construction-time throw in expected_t<...>.
    // The factory mr parameter overrides cfg.mr when non-null.
    // No exception escapes this function per [arch §5.3] / FR-005.
    [[nodiscard]] static core::expected_t<std::shared_ptr<cert_source>>
    make_file_cert_source(Config cfg, std::pmr::memory_resource* mr) noexcept;

    // Direct-construction path for in-process C++ callers that prefer
    // throw-on-failure per [arch §5.3] construction-time carve-out.
    explicit file_cert_source(Config cfg);
    ~file_cert_source() override;

    // Non-copyable, non-movable (holds OpenSSL RAII resources).
    file_cert_source(file_cert_source const&)            = delete;
    file_cert_source& operator=(file_cert_source const&) = delete;
    file_cert_source(file_cert_source&&)                 = delete;
    file_cert_source& operator=(file_cert_source&&)      = delete;

    // ── cert_source overrides ─────────────────────────────────────────────────
    // Follows the §6.4 cancellation recipe verbatim (cert_source.hpp doc-comment).
    [[nodiscard]] asio::awaitable<core::expected_t<local_credentials>>
    load_credentials() override;

    // Returns span<const Certificate> into *this-owned storage.
    [[nodiscard]] core::expected_t<std::span<const Certificate>>
    load_trust_anchors() [[clang::lifetimebound]] override;

 private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fixpp::tls
