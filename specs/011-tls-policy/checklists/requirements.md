# Specification Quality Checklist: 011 — TLS Policy Core

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-05-23
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — **Library-spec carve-out**: this Phase-4 feature ships a public C++ surface (and a C-ABI projection via 2i-capi). The user-visible contract IS the type/method surface; naming `cert_source`, `Pinset`, `SecurityProfile`, `[[clang::lifetimebound]]`, `expected_t`, OpenSSL, and ASIO is intentional and traces directly to the signed-off design doc `.specify/2g-tls.md` v0.4. Same carve-out applied in prior Phase-4 specs (006/007/008/009/010). Stakeholder audience is FIX-engine operators + security architects, not non-technical readers.
- [x] Focused on user value and business needs — User stories anchor on operator workflows: rotation without disconnects (P1), plug a custom credential source (P2), hardened-by-default trust mode (P3).
- [x] Written for non-technical stakeholders — modulo the library-spec carve-out above; user-story narrative is operator-language.
- [x] All mandatory sections completed — User Scenarios & Testing, Requirements, Success Criteria, Assumptions all populated.

## Requirement Completeness

- [x] No `[NEEDS CLARIFICATION]` markers remain — none in the spec body. The `/clarify` pass completed 2026-05-23 with 5 questions resolved (TLS-version negotiation order → FR-014; `mtls_pinned` empty-pinset bootstrap → FR-025 + US3 AC5; `verify_peer` multi-violation evaluation order → FR-020a; pin lookup key SHA-256-of-DER → FR-006 + FR-008; per-counterparty Pinset granularity → FR-009a). See `spec.md ## Clarifications Session 2026-05-23` for the answers. (Codex P3-1 close — the v0.1 checklist's future-tense framing for the three pre-clarify probes is updated to past tense.)
- [x] Requirements are testable and unambiguous — each FR maps to a verifiable check (compile-time failure, specific `error::tls_*` variant, latency bound, attribute presence at declaration site, etc.).
- [x] Success criteria are measurable — SC-001 (zero disconnects across rotation), SC-002 (100% refusal rate against out-of-envelope peers), SC-003 (build fails), SC-007 (≤ 130 ns p99 hot-path lookup), etc.
- [x] Success criteria are technology-agnostic — describe operator-observable outcomes. SC-007 cites the design-doc latency anchor `[2g §6.3]`, not an implementation detail.
- [x] All acceptance scenarios are defined — every user story has Given/When/Then scenarios (4 / 4 / 5 across P1/P2/P3).
- [x] Edge cases are identified — 14 edge cases enumerated covering concurrent rotation, `pin_view` outliving `remove`, PEM password failure, DoS bound rejections, expired-against-effective-clock, cancellation, PMR throw, banned-cipher runtime override.
- [x] Scope is clearly bounded — FR-026 (negative ownership: what 2g does NOT own) and FR-027 (out-of-scope features: PSK, CRL/OCSP, mid-handshake rotation, mid-session swaps, dlopen) make the boundary explicit. Cross-cuts T-039/T-040/T-041 named.
- [x] Dependencies and assumptions identified — Assumptions section enumerates design-doc inheritance, TLS-provider lock-in, 2h-transport boundary, session-FSM boundary, 2i/2j/2k ownership transfers, interop deferral.

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria — FR↔US acceptance-scenario mapping is tight: rotation (FR-006/007/008/009/010 ↔ US1 scenarios), plugin surface (FR-001..005 ↔ US2 scenarios), policy hardening (FR-011..015, FR-019/020 ↔ US3 scenarios).
- [x] User scenarios cover primary flows — three user stories cover the three operator-facing capability surfaces (rotation, plug, harden); each is independently testable as an MVP slice.
- [x] Feature meets measurable outcomes defined in Success Criteria — every SC traces back to at least one FR; every FR cluster has at least one SC.
- [x] No implementation details leak into specification — same library-spec carve-out as Content Quality item 1.

## Notes

- All items pass first-iteration validation.
- The `/clarify` pass (mandatory per `[const §XVI.3]` for security features) **completed 2026-05-23**. Five questions resolved, recorded in `spec.md ## Clarifications Session 2026-05-23`:
  1. TLS-version negotiation order — TLS 1.3 preferred; TLS 1.2 ECDHE-AEAD admitted as fallback (`SSL_CTX_set_min_proto_version = TLS1_2_VERSION`, `set_max = TLS1_3_VERSION`). → FR-014.
  2. `mtls_pinned` empty-pinset bootstrap — fail-EARLY at session-open with distinct variant `tls_pin_empty_at_open`. → FR-025 + US3 AC5.
  3. `verify_peer` multi-violation ordering — short-circuit first hit in a documented evaluation order. → FR-020a.
  4. Pin lookup key for `Pinset::find` / `add` / `remove` — SHA-256 fingerprint of leaf cert's DER encoding (32 bytes). → FR-006 + FR-008.
  5. Pinset granularity — per-counterparty recommended; engine does not enforce. → FR-009a + US1 Independent Test.
- Spec inherits the signed-off `.specify/2g-tls.md` v0.4 — design decisions are settled upstream, this spec codifies the user-visible contract only.
