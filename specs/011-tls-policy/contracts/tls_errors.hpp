// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-025 + Key Entity E-14.
//
// Design anchor: .specify/2g-tls.md v0.4 §6.6 (15-variant TLS error envelope +
// the `tls_handshake_failed` C-ABI coalescing group) + §6.6's "C-ABI mapping
// (delegated to 2i)" five-group projection.
// Spec anchors: spec.md FR-025 (error::tls_* variant family, per-doc-prefix
// `FIXPP_ERR_TLS_*` coalescing rule), FR-020a (verify_peer sub-reason via the
// `tls_handshake_failed` group's diagnostic field), `## Clarifications` Q2
// (the only legitimately new variant since 2g v0.4 signed off:
// `tls_pin_empty_at_open`).
//
// The actual enum-variant declarations are APPENDED to
// include/fixpp/core/error.hpp at the next free contiguous slot range after
// slot 77 (010-session-cfg-lifetime's last addition). This file is a
// CONTRACT ORACLE listing the binding variants + the per-doc-prefix
// FIXPP_ERR_TLS_* coalescing rule the 2i C-ABI bridge consumes.

#pragma once

// ============================================================================
// error::tls_* variants — re-emitted VERBATIM from .specify/2g-tls.md v0.4 §6.6
// (15 variants in the design-doc-signed-off table) + the one legitimately new
// variant from /clarify Q2 (`tls_pin_empty_at_open`). Total: 16 variants.
//
// 2g §6.6 prose: "2g claims a contiguous block of **15 variants** under the
// `tls_*` prefix; 2i is asked to assign the block adjacent to 2f's `sync_*`
// block." `tls_pin_empty_at_open` is an additive amendment from /clarify Q2;
// it occupies one additional slot.
//
// Slot allocation is at /speckit-implement time; the rule is "next free
// contiguous range after slot 77, NEVER renumber prior slots". Per
// [[project_2e_design_doc_only_seqnum_handoff]] precedent.
// ============================================================================
//
// namespace fixpp::core {
//
// enum class error : std::uint8_t {
//     // ... existing variants 0..77 unchanged (decimal / wire / codegen /
//     //     session / msgstore / async_mutex / threading / message_store /
//     //     session-fsm-finalize / session-cfg-lifetime slots) ...
//
//     // ── 011-tls-policy: 2g §6.6 15-variant block + Q2 amendment (slots TBD at /implement) ──
//
//     // ── Configuration / cold-path-load errors (§6.6 group `FIXPP_ERR_TLS_CONFIG`) ──
//     tls_cert_load_failed         = 78,   // §4.2 — file_cert_source could not read a cert file at construction; OR make_file_cert_source factory returning the same condition through expected_t<...>; OR §4.2 local cert exceeds Config::max_rsa_key_bits / max_cert_der_bytes / max_san_entries at load time.
//     tls_cert_parse_failed        = 79,   // §4.2 / §4.5 — PEM/DER parse failed (malformed envelope, unexpected ASN.1 shape, unsupported encoding).
//     tls_cipher_not_allowed       = 80,   // §4.4 / §4.5 / §6.1 — runtime configuration attempted to enable a cipher not on the four CipherPolicy allow-lists. Compile-time static_assert catches build-time; this variant covers the C-ABI / config-file path through CipherPolicy::is_allowed(...).
//     tls_invalid_security_profile = 81,   // §4.5 — SecurityProfile::unset reached make_ssl_ctx_config; OR mtls_pinned with a null Pinset; OR one_way_ca with a non-null Pinset; OR any profile with a null clock.
//     tls_sign_callback_unavailable = 82,  // §4.1 — impl returned local_credentials whose signer variant carried a software_key_ref with a null handle, AND the awaitable signer path was also empty. Preserved for the C-ABI path where the discriminator may carry an unset value.
//
//     // ── Pinset state errors (§6.6 group `FIXPP_ERR_TLS_PINSET`) ──
//     tls_pin_not_found            = 83,   // §4.3 — Pinset::remove(fp) called with a fingerprint not in the set.
//     tls_pin_already_present      = 84,   // §4.3 — Pinset::add(p) called with a pin already in the set (by SHA-256).
//     tls_pinset_capacity_exhausted = 85,  // §4.3 / §1.1 — Pinset::add with size() == max_pins.
//
//     // ── Runtime-allocation errors (§6.6 group `FIXPP_ERR_TLS_RUNTIME`) ──
//     tls_pinset_alloc_failed      = 86,   // §4.3 — PMR allocation throw on snapshot clone routed through [2a §4.2] trap_throw.
//
//     // ── Handshake / verify_peer errors (§6.6 group `FIXPP_ERR_TLS_HANDSHAKE` — the C-ABI coalescing group) ──
//     //
//     // 2g §6.6: `tls_handshake_failed` is the GROUPING variant that holds the
//     // C-ABI coalescing scheme together. The diagnostic field carries the
//     // specific sub-reason ("`expired`", "`not_yet_valid`", "`rsa_under_min`",
//     // "`sigalg_disallowed`", etc.). Per FR-020a / /clarify Q3, verify_peer
//     // short-circuits on the first violation hit; the variant returned is
//     // EITHER this grouping variant with a sub-reason OR one of the dedicated
//     // DoS-cap variants below (tls_rsa_key_too_large / tls_cert_der_too_large
//     // / tls_san_entries_exceeded — each is its own envelope variant per 2g
//     // §6.6 lines 996-998 but is still grouped under FIXPP_ERR_TLS_HANDSHAKE
//     // at the C-ABI boundary).
//     tls_handshake_failed         = 87,   // §4.5 — verify_peer rejected the peer cert (chain unverifiable, notAfter past cfg.clock->now(), notBefore future, RSA key strength below T-039 minimum, signature algorithm not in sig_algs, X.509 v1, ECDSA curve outside P-256/P-384, chain depth over cap). Diagnostic field carries sub-reason.
//     tls_rsa_key_too_large        = 88,   // §4.5 / §1.1 — peer presented an RSA key with bits > Config::max_rsa_key_bits (default 8192). DoS bound. Surfaces through FIXPP_ERR_TLS_HANDSHAKE group with sub-reason "rsa_over_max".
//     tls_cert_der_too_large       = 89,   // §4.5 / §1.1 — peer cert ASN.1 envelope exceeds Config::max_cert_der_bytes (default 16 KiB). DoS bound.
//     tls_san_entries_exceeded     = 90,   // §4.5 / §1.1 — peer cert SAN list cardinality exceeds Config::max_san_entries (default 64). DoS bound.
//     tls_pin_mismatch             = 91,   // §4.5 — under SecurityProfile::mtls_pinned, the verified peer cert's SHA-256 is not in the captured handshake-time Pinset snapshot.
//
//     // ── Cancellation (§6.6 reuses the existing FIXPP_ERR_CANCELLED group) ──
//     tls_load_cancelled           = 92,   // §4.1 / §6.4 — cert_source::load_credentials's awaitable was cancelled (or async_signer_ref::sign was). Joins [2d §6.5] dispatch_aborted, [2d §4.1.1] clock_sleeps_cancelled, store_cancelled, [2f §6.5] sync_lock_aborted in the cancellation group.
//
//     // (15 variants — the 2g §6.6 signed-off block ends here.)
//
//     // ── /clarify Q2 amendment (the only legitimately new variant since 2g v0.4) ──
//     tls_pin_empty_at_open        = 93,   // /clarify Q2 — make_ssl_ctx_config(mtls_pinned, non-null but empty pinset, ...) returns this distinct variant. Surfaces at session-open, NOT per-handshake; distinct from tls_pin_mismatch so operator logs separate config-error from peer-error.
//
//     // ... end of 011-tls-policy variants ...
// };
//
// }  // namespace fixpp::core
//
// ============================================================================
// 2g §6.6 C-ABI coalescing (delegated to 2i) — re-emitted verbatim from the
// signed-off 2g §6.6 "C-ABI mapping" paragraph (lines 1006-1013).
//
// Per the per-doc-prefix discipline established by [2b §6.4]
// (FIXPP_ERR_WIRE_*), [2c §6.4] (FIXPP_ERR_DICT_*), [2d §6.7]
// (FIXPP_ERR_THREAD_*), [2e §6.4] (FIXPP_ERR_STORE_*), [2f §6.5]
// (FIXPP_ERR_SYNC_*): 011 adopts the prefix `FIXPP_ERR_TLS_*` for its C-ABI
// mapping target, owned by 2i.
//
//   - configuration errors (tls_cert_load_failed, tls_cert_parse_failed,
//     tls_cipher_not_allowed, tls_invalid_security_profile,
//     tls_sign_callback_unavailable) →   FIXPP_ERR_TLS_CONFIG
//
//   - handshake / verification (tls_handshake_failed, tls_pin_mismatch,
//     tls_rsa_key_too_large, tls_cert_der_too_large,
//     tls_san_entries_exceeded) →   FIXPP_ERR_TLS_HANDSHAKE
//     (the C-ABI coalescing group; 2g §6.6 lines 1007-1013 — the grouping
//     variant tls_handshake_failed AND the dedicated DoS-cap variants
//     coalesce to this one C code, with the diagnostic-field sub-reason
//     surfacing the specific cause through the C-ABI's diagnostic accessor).
//
//   - pinset state (tls_pin_not_found, tls_pin_already_present,
//     tls_pinset_capacity_exhausted) →   FIXPP_ERR_TLS_PINSET
//
//   - runtime allocation (tls_pinset_alloc_failed) →   FIXPP_ERR_TLS_RUNTIME
//
//   - cancellation (tls_load_cancelled) reuses the existing
//     FIXPP_ERR_CANCELLED per [const §XI.2].
//
// `tls_pin_empty_at_open` (Q2 amendment) projects to FIXPP_ERR_TLS_CONFIG
// (config-time validation failure shape; matches the
// `tls_invalid_security_profile` grouping rather than the per-handshake
// FIXPP_ERR_TLS_HANDSHAKE group).
//
// Final coalescing is 2i's call.
//
// ============================================================================
// Contract assertions (verified at /speckit-verify against the actual code):
//
//   1. include/fixpp/core/error.hpp contains EXACTLY the 15 2g §6.6 variants
//      + the 1 /clarify Q2 amendment variant (total 16). NO additional
//      bundle-invented variants; 2g §6.6 is the binding source.
//   2. The `tls_handshake_failed` GROUPING VARIANT (2g §6.6 line 995) is
//      present and carries the diagnostic-field sub-reason contract per
//      FR-020a — `verify_peer` returns this grouping variant on any
//      non-DoS-cap handshake reject (cert chain unverifiable, expiration,
//      X.509 v1, ECDSA curve, chain depth, RSA key too small, sigalg
//      disallowed) with the specific cause carried in the diagnostic field.
//   3. NO renumbering of pre-slot-77 variants (precedent rule per
//      [[project_2e_design_doc_only_seqnum_handoff]]).
//   4. Each variant has a corresponding `tests/tls/test_*` (or
//      `tests/conformance/test_*`) test that witnesses ITS specific rejection
//      path. The error-variant exercise test (`[2g §9 seam #12]`) drives
//      every variant in one place.
//   5. The C-ABI coalescing five-group mapping above is honoured by the 2i
//      C-ABI bridge (delegated). This file publishes the C++ source-of-truth.
