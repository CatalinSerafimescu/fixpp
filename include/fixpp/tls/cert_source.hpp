#pragma once
// include/fixpp/tls/cert_source.hpp
// fixpp::tls::cert_source — pluggable credential source interface.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.1 + §6.4.
// Spec anchors: FR-001 (≤2 pure-virtuals), FR-002 (awaitable+expected_t),
//               FR-003 (signer variant), FR-021 (ASIO cancellation),
//               FR-022 ([[nodiscard]]), FR-023 (cold-path PMR),
//               FR-024 (trap_throw routing).
//
// EXACTLY 2 pure-virtuals per [const §XIV.2] (cap = 5).
//
// §6.4 Cancellation contract — load_credentials recipe (verbatim from
// contracts/cert_source.hpp lines 200-247 / [2g §6.4] lines 903-944):
//
//   asio::awaitable<core::expected_t<local_credentials>>
//   my_cert_source::load_credentials() {
//       // 1. Read the awaiter's bound executor (project-owned session_executor).
//       auto exec = co_await asio::this_coro::executor;
//
//       // 2. Read the awaiter's cancellation_state.
//       auto cs = co_await asio::this_coro::cancellation_state;
//
//       // 3. REAP PRE-I/O CANCELLATION — the "between-call-and-first-suspension"
//       //    reap. Load-bearing for the §6.4 binding contract.
//       if (cs.cancelled() != asio::cancellation_type::none)
//           co_return core::expected_t<local_credentials>{
//               unexpect, error::tls_load_cancelled };
//
//       // 4. For disk/network/HSM work, post via cancellable_dispatch.
//       //    expected_t::unexpected{dispatch_aborted} → tls_load_cancelled.
//       auto dispatched = co_await fixpp::core::cancellable_dispatch(
//           exec, cs.slot(),
//           [this]() { /* build local_credentials from cached parsed state */ });
//
//       if (!dispatched)
//           co_return core::expected_t<local_credentials>{
//               unexpect, error::tls_load_cancelled };
//
//       co_return core::expected_t<local_credentials>{ /* built value */ };
//   }

#include <asio/awaitable.hpp>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/certificate.hpp>
#include <functional>
#include <memory_resource>
#include <span>
#include <variant>
#include <vector>

namespace fixpp::tls {

namespace detail {
// Opaque handle to a parsed private key owned by file_cert_source.
// The actual EVP_PKEY* is managed inside file_cert_source.cpp RAII.
// Callers treat this as an opaque token; 2h's wiring uses the handle
// to bind the EVP_PKEY* directly through this indirection.
struct private_key_handle {
    void* ossl_pkey = nullptr;  // EVP_PKEY* — owned by the issuing cert_source.
};
}  // namespace detail

// ── software_key_ref ──────────────────────────────────────────────────────────
// Engine-internal handle to a parsed private key owned by the cert_source.
// Lifetime is bounded by the cert_source instance that issued it.
// Per [2g §4.1]; data-model E-3.
struct software_key_ref {
    detail::private_key_handle handle;  // EVP_PKEY* wrapped opaquely; *this-bounded.
    int ossl_pkey_id;                   // OpenSSL EVP_PKEY_id (e.g., EVP_PKEY_RSA, EVP_PKEY_EC).
};

// ── sign_request / sign_response ─────────────────────────────────────────────
// Per [2g §4.1]; data-model E-4.
struct sign_request {
    // NOTE: [[clang::lifetimebound]] only applies to function parameters, not data
    // members (clang limitation). The lifetime contract is documented here: `tbs`
    // always points into caller-owned storage and is valid for the duration of the
    // sign call. See pin_view::value in pinset.hpp for the same precedent.
    std::span<const std::byte> tbs;  // bytes-to-be-signed; caller-owned.
    int sig_alg;                     // OpenSSL NID for the signature algorithm.
};

struct sign_response {
    std::pmr::vector<std::byte> signature;  // owning; PMR-allocated by the impl.
};

// ── async_signer_ref ─────────────────────────────────────────────────────────
// HSM-style awaitable signing oracle. The sign callable MUST NOT block the
// session strand; it must post blocking work off-strand via cancellable_dispatch.
// Per [2g §4.1]; data-model E-4.
struct async_signer_ref {
    using sign_fn =
        std::function<asio::awaitable<core::expected_t<sign_response>>(sign_request const& req)>;
    sign_fn sign;
};

// ── local_credentials ────────────────────────────────────────────────────────
// Everything OpenSSL's SSL_CTX configuration needs at handshake time.
// Returned by load_credentials(); lifetime bounded by the cert_source instance.
//
// CRITICAL: leaf is a VALUE-typed Certificate (its view fields alias cert_source
// storage). chain is a std::span<const Certificate> VIEW — NOT an owning vector.
// Per [2g §4.1] lines 252-254; data-model E-2.
// NOTE: [[clang::lifetimebound]] only applies to function parameters and implicit
// object parameters, not data members. The lifetime invariants are:
//   leaf.* fields alias *this-owned cert_source storage.
//   chain views *this-owned cert_source storage.
// These are enforced by the struct invariant, not by an attribute.
// See pin_view::value in pinset.hpp for the same precedent (N-P3-1).
struct local_credentials {
    Certificate leaf;  // view fields alias cert_source storage; *this-bounded.
    std::span<const Certificate>
        chain;  // root last; view into cert_source-owned storage; *this-bounded.
    std::variant<software_key_ref, async_signer_ref>
        signer;  // software handle OR awaitable HSM oracle.
};

// ── cert_source ───────────────────────────────────────────────────────────────
// Pluggable credential source — exactly 2 pure-virtuals per FR-001 /
// [const §XIV.2] (cap = 5). Re-emitted verbatim from [2g §4.1] lines 259-290.
// Abstract interface; defaulted dtor + Liskov-safe non-copy/non-move is intentional.
// NOLINTNEXTLINE(hicpp-special-member-functions,cppcoreguidelines-special-member-functions)
class cert_source {
public:
    virtual ~cert_source() = default;

    // (1) Load the local credentials bundle (leaf cert + chain + signer).
    //     Awaitable so that fetch-from-vault / HSM-blocking / file-on-network-fs
    //     impls can suspend without blocking the session strand.
    //     Cancellation follows the §6.4 recipe verbatim (above).
    //     On cancellation → expected_t::unexpected{error::tls_load_cancelled}.
    //     [[clang::lifetimebound]] on local_credentials fields is at the
    //     ABSTRACT BASE declaration site per [arch §5.5] / [2b §6.4] precedent.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<local_credentials>>
    load_credentials() = 0;

    // (2) Trust anchors (CA certs). Span view is *this-bounded.
    //     Returns expected_t<span> so that error cases (e.g., a pinned-only
    //     deployment that explicitly has no CA trust) can be surfaced.
    //     [[clang::lifetimebound]] at the abstract base per [arch §5.5].
    [[nodiscard]] virtual core::expected_t<std::span<const Certificate>> load_trust_anchors()
        [[clang::lifetimebound]] = 0;
};

}  // namespace fixpp::tls
