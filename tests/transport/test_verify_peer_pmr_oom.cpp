// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_verify_peer_pmr_oom.cpp
// RC#C (P1-3) — noexcept PMR OOM boundary witness for verify_peer.
//
// Validates that verify_peer(noexcept) does NOT propagate std::bad_alloc when
// the PMR resource throws on the N-th allocation. Instead it surfaces as
// tls_pinset_alloc_failed.
//
// Per [[feedback_trap_throw_pmr_witness_enumerate_sites]]:
//   - Boundary site: N=1 (first allocation — subject_dn assign).
//   - Mid site: N=2 (second allocation — san_dns_names_owned construction).
//   - Tail site: large N (all allocations exhausted — survives to return).
//
// Design anchor: spec.md FR-035; .specify/2h-transport.md §1 item 5.
// Constitutional: [arch §5.3] no exceptions across public surface.

#include <gtest/gtest.h>
#include "../support/msvc_debug_arena_skip.hpp"

#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <memory_resource>
#include <new>
#include <span>
#include <string_view>

namespace {

using fixpp::core::error;
using fixpp::tls::Certificate;
using fixpp::tls::CertSourceCaps;
using fixpp::tls::SecurityProfile;
using fixpp::tls::signature_algorithm;
using fixpp::tls::SslCtxConfig;
using fixpp::tls::verify_peer;

// ── Counting PMR that throws std::bad_alloc on the N-th allocation ───────────
//
// Used to witness both boundary (N=1) and mid (N=2..k) allocation sites inside
// verify_peer per [[feedback_trap_throw_pmr_witness_enumerate_sites]].
class throw_on_nth_resource final : public std::pmr::memory_resource {
public:
    explicit throw_on_nth_resource(std::size_t throw_on) : throw_on_{throw_on} {}

    std::size_t alloc_count() const noexcept { return count_; }

protected:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        ++count_;
        if (count_ >= throw_on_) {
            throw std::bad_alloc{};
        }
        return std::pmr::new_delete_resource()->allocate(bytes, align);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    std::size_t throw_on_;
    std::size_t count_{0};
};

// ── Build a minimal Certificate that passes all verify_peer guards ────────────
//
// Passes guards 1-10 (size, alg, RSA bounds, chain depth, SAN count, x509v3,
// expiry skipped via null clock, pinning skipped via mtls_ca profile).
// The subject_dn and one SAN DNS entry are included so peer_identity
// construction actually allocates into the PMR.
//
// NOTE: strings must be >22 chars to exceed libc++ SSO (22 bytes) AND
// >15 chars to exceed libstdc++ SSO (15 bytes), so all three PMR alloc sites
// (subject_dn, san_dns_names_owned buffer, SAN string) trigger on both stdlibs.
Certificate make_valid_cert_with_san() {
    static const std::string_view kSubjectDn{
        "CN=test-node.fixpp.example.invalid"};  // 34 chars > libc++ SSO(22)
    static const std::string_view kSanDns{
        "test-node.fixpp.example.invalid"};  // 31 chars > libc++ SSO(22)
    static const std::span<const std::string_view> kSanDnsSpan{&kSanDns, 1};
    static const std::span<const std::string_view> kNoSanUri{};

    Certificate c{};
    c.subject_dn_ = kSubjectDn;
    c.issuer_dn_ = kSubjectDn;
    c.san_dns_names_ = kSanDnsSpan;
    c.san_uris_ = kNoSanUri;
    c.x509_version_ = 3;  // v3 required (v1 rejected at step 7)
    c.alg_ = signature_algorithm::rsa_pss;
    c.rsa_key_bits_ = 2048;  // exactly the lower bound (step 2)
    // not_before / not_after default-initialized to epoch — clock is null so
    // step 8 (expiry) is skipped per verify_peer impl.
    return c;
}

// ── Build a Certificate with 3 SAN-DNS entries (mirrors leaf_multi_san.pem) ──
//
// T024 / FR-014 / [[feedback_trap_throw_pmr_witness_enumerate_sites]]:
// With 3 SAN-DNS entries, throw_on_nth_resource can exercise:
//   N=1: boundary  — subject_dn.assign
//   N=2: mid       — first san_dns_names_owned.emplace_back
//   N=3: tail-1    — second san_dns_names_owned.emplace_back
//   N=4: tail-2    — third san_dns_names_owned.emplace_back
// All must surface as tls_pinset_alloc_failed (not terminate).
// Mirrors leaf_multi_san.pem (3 SAN-DNS entries, T023 fixture).
Certificate make_valid_cert_with_multi_san() {
    static const std::string_view kSubjectDn{"CN=fixpp-leaf-multi-san"};
    static const std::array<std::string_view, 3> kSanDnsArr{{
        "host-1-fixpp-test-fixtures-very-long-subdomain.fixpp.example.invalid",
        "host-2-fixpp-test-fixtures-very-long-subdomain.fixpp.example.invalid",
        "host-3-fixpp-test-fixtures-very-long-subdomain.fixpp.example.invalid",
    }};
    static const std::span<const std::string_view> kSanDnsSpan{kSanDnsArr.data(), 3};
    static const std::span<const std::string_view> kNoSanUri{};

    Certificate c{};
    c.subject_dn_ = kSubjectDn;
    c.issuer_dn_ = kSubjectDn;
    c.san_dns_names_ = kSanDnsSpan;
    c.san_uris_ = kNoSanUri;
    c.x509_version_ = 3;
    c.alg_ = signature_algorithm::rsa_pss;
    c.rsa_key_bits_ = 2048;
    return c;
}

// ── Build an SslCtxConfig that passes all verify_peer profile checks ─────────
//
// profile = mtls_ca: pinset is not checked (step 9 skipped).
// clock   = nullptr: expiry is not checked (step 8 skipped).
// mr      = supplied by caller — set to the throw_on_nth_resource.
SslCtxConfig make_cfg(std::pmr::memory_resource* mr) {
    SslCtxConfig cfg;
    cfg.profile = SecurityProfile::mtls_ca;
    cfg.mr = mr;
    // Caps: defaults from CertSourceCaps (max_chain_depth=8, etc.).
    // Single-cert chain fits within all bounds.
    cfg.caps = CertSourceCaps{};
    return cfg;
}

// ── Helper: call verify_peer with one cert and expect no terminate ────────────
//
// Returns the error code from the unexpected{} result.
// Precondition: mr throws before peer_identity construction completes.
error call_verify_peer_with_throwing_mr(std::pmr::memory_resource& mr) {
    const auto cert = make_valid_cert_with_san();
    const std::span<const Certificate> chain{&cert, 1};
    SslCtxConfig cfg = make_cfg(&mr);

    // verify_peer is noexcept — this call must NOT throw.
    auto result = verify_peer(cfg, chain);

    // The process must still be alive here (no terminate).
    EXPECT_FALSE(result.has_value())
        << "verify_peer must reject on PMR OOM, not return a peer_identity";
    return result.has_value() ? error{} : result.error();
}

// ── Helper: call verify_peer with multi-SAN cert and expect no terminate ──────
error call_verify_peer_multi_san_with_throwing_mr(std::pmr::memory_resource& mr) {
    const auto cert = make_valid_cert_with_multi_san();
    const std::span<const Certificate> chain{&cert, 1};
    SslCtxConfig cfg = make_cfg(&mr);

    auto result = verify_peer(cfg, chain);

    EXPECT_FALSE(result.has_value())
        << "verify_peer must reject on PMR OOM (multi-SAN cert), not return a peer_identity";
    return result.has_value() ? error{} : result.error();
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Boundary site: N=1 — first allocation inside peer_identity construction
// (subject_dn.assign triggers the PMR). Process must NOT terminate.
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VerifyPeerPmrOom, BoundaryFirstAllocation) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    throw_on_nth_resource mr{/*throw_on=*/1};
    const auto ec = call_verify_peer_with_throwing_mr(mr);
    EXPECT_EQ(ec, error::tls_pinset_alloc_failed)
        << "boundary OOM (first alloc) must surface as tls_pinset_alloc_failed";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mid site: N=2 — second allocation (san_dns_names_owned vector construction or
// its first push_back). Process must NOT terminate.
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VerifyPeerPmrOom, MidSecondAllocation) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    throw_on_nth_resource mr{/*throw_on=*/2};
    const auto ec = call_verify_peer_with_throwing_mr(mr);
    EXPECT_EQ(ec, error::tls_pinset_alloc_failed)
        << "mid OOM (second alloc) must surface as tls_pinset_alloc_failed";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tail site: N=3 — third allocation (san_uris_owned or its first emplace).
// Process must NOT terminate.
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VerifyPeerPmrOom, TailThirdAllocation) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    throw_on_nth_resource mr{/*throw_on=*/3};
    const auto ec = call_verify_peer_with_throwing_mr(mr);
    EXPECT_EQ(ec, error::tls_pinset_alloc_failed)
        << "tail OOM (third alloc) must surface as tls_pinset_alloc_failed";
}

// ═══════════════════════════════════════════════════════════════════════════════
// No-throw path: large N — PMR never exhausts, verify_peer succeeds normally.
// Confirms the catch block is not incorrectly entered on success.
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VerifyPeerPmrOom, NoThrowSucceedsNormally) {
    throw_on_nth_resource mr{/*throw_on=*/1000};  // effectively unlimited
    const auto cert = make_valid_cert_with_san();
    const std::span<const Certificate> chain{&cert, 1};
    SslCtxConfig cfg = make_cfg(&mr);

    auto result = verify_peer(cfg, chain);
    EXPECT_TRUE(result.has_value()) << "verify_peer must succeed when PMR does not exhaust; "
                                       "got error: "
                                    << (result.has_value() ? 0 : static_cast<int>(result.error()));
}

// ═══════════════════════════════════════════════════════════════════════════════
// T024 / FR-014 / [[feedback_trap_throw_pmr_witness_enumerate_sites]]:
// Multi-SAN cert (mirrors leaf_multi_san.pem — T023 fixture, 3 SAN-DNS entries).
//
// Per the anti-pattern: a 1-SAN cert trips at boundary/mid/tail of its own
// short allocation sequence. The multi-SAN cert EXTENDS the sequence:
//   N=2 → MID: first san_dns_names_owned.emplace_back (after subject_dn)
//   N=3 → TAIL-1: second san_dns_names_owned.emplace_back
//   N=4 → TAIL-2: third san_dns_names_owned.emplace_back
// Confirms verify_peer catches bad_alloc at EVERY SAN-DNS allocation site
// and surfaces tls_pinset_alloc_failed (not terminate / crash).
//
// The multi-SAN fixture is required because with 1 SAN, N=3 is already the
// san_uris_owned path (no DNS san left to witness). With 3 SAN-DNS entries,
// N=3 and N=4 witness distinct mid-SAN-body sites: the 2nd and 3rd DNS SANs.
// This is the FR-014 carry-forward from 012 RC#C per tasks.md T024.
// ═══════════════════════════════════════════════════════════════════════════════

// Multi-SAN boundary: N=1 — first allocation (subject_dn.assign).
TEST(VerifyPeerPmrOomMultiSan, BoundaryFirstAllocation) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    throw_on_nth_resource mr{/*throw_on=*/1};
    const auto ec = call_verify_peer_multi_san_with_throwing_mr(mr);
    EXPECT_EQ(ec, error::tls_pinset_alloc_failed)
        << "multi-SAN boundary OOM (first alloc, subject_dn) must surface as "
           "tls_pinset_alloc_failed";
}

// Multi-SAN mid: N=2 — second allocation (first san_dns_names_owned.emplace_back).
// This is the MID site in the peer_identity construction sequence.
TEST(VerifyPeerPmrOomMultiSan, MidFirstSanDnsAllocation) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    throw_on_nth_resource mr{/*throw_on=*/2};
    const auto ec = call_verify_peer_multi_san_with_throwing_mr(mr);
    EXPECT_EQ(ec, error::tls_pinset_alloc_failed)
        << "multi-SAN mid OOM (second alloc, first SAN-DNS) must surface as "
           "tls_pinset_alloc_failed";
}

// Multi-SAN tail-1: N=3 — third allocation (second san_dns_names_owned.emplace_back).
TEST(VerifyPeerPmrOomMultiSan, TailSecondSanDnsAllocation) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    throw_on_nth_resource mr{/*throw_on=*/3};
    const auto ec = call_verify_peer_multi_san_with_throwing_mr(mr);
    EXPECT_EQ(ec, error::tls_pinset_alloc_failed)
        << "multi-SAN tail-1 OOM (third alloc, second SAN-DNS) must surface as "
           "tls_pinset_alloc_failed";
}

// Multi-SAN tail-2: N=4 — fourth allocation (third san_dns_names_owned.emplace_back).
TEST(VerifyPeerPmrOomMultiSan, TailThirdSanDnsAllocation) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    throw_on_nth_resource mr{/*throw_on=*/4};
    const auto ec = call_verify_peer_multi_san_with_throwing_mr(mr);
    EXPECT_EQ(ec, error::tls_pinset_alloc_failed)
        << "multi-SAN tail-2 OOM (fourth alloc, third SAN-DNS) must surface as "
           "tls_pinset_alloc_failed";
}

// Multi-SAN no-throw: all allocations succeed, verify_peer returns a peer_identity.
TEST(VerifyPeerPmrOomMultiSan, NoThrowSucceedsNormally) {
    throw_on_nth_resource mr{/*throw_on=*/1000};
    const auto cert = make_valid_cert_with_multi_san();
    const std::span<const Certificate> chain{&cert, 1};
    SslCtxConfig cfg = make_cfg(&mr);

    auto result = verify_peer(cfg, chain);
    EXPECT_TRUE(result.has_value())
        << "multi-SAN cert: verify_peer must succeed when PMR does not exhaust";
    if (result.has_value()) {
        EXPECT_EQ(result->san_dns_names_owned.size(), 3u)
            << "multi-SAN peer_identity must have 3 SAN-DNS entries";
    }
}
