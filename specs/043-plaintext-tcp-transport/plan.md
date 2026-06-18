# Implementation Plan: Plaintext TCP transport (insecure_plain_tcp)

**Branch**: `043-plaintext-tcp-transport` | **Date**: 2026-06-17 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/043-plaintext-tcp-transport/spec.md`

## Summary

Add a plaintext TCP transport (`asio_plain_transport`) as an encryption-agnostic sibling to
`asio_tls_transport`, gated behind the new `SecurityProfile::kind::insecure_plain_tcp` (constitution
§XII.5 amended v0.3). The transport implements the five base `Transport` operations over a plain
`asio::ip::tcp::socket` with no TLS handshake; a credential-free factory sibling mints it (initiator
`make()` + acceptor `make_accepted()`). The engine auto-derives the plaintext factory from the profile
when no override is supplied, rejects an explicit profile↔factory kind mismatch at `open()`, and skips the
TLS handshake stage (initiator FSM + acceptor accept-loop) for the plaintext profile. Selecting the
profile emits a loud `[[deprecated]]`-class diagnostic. The mTLS-gated CompID↔identity authorization is
inert (as for `one_way_ca`); the cert-independent `check_comp_id` is preserved. No new wire/error/codegen
surface. Full design rationale in [research.md](./research.md); entities in
[data-model.md](./data-model.md); contracts in [contracts/](./contracts/).

## Technical Context

**Language/Version**: C++23 (clang; coroutines, `std::expected`, `std::pmr`)
**Primary Dependencies**: standalone Asio (`asio::ip::tcp`, awaitables, cancellation slots). **No TLS
context, credentials, or handshake are constructed on the plaintext path** (the plain transport class pulls
no OpenSSL — contract asio_plain_transport.hpp:7). NOTE: the plain *factory* body lives in
`transport_factory.cpp` (D-3), which includes OpenSSL for the TLS factory, and its contract header pulls
`<fixpp/tls/security_profile.hpp>` for the ignored `SslCtxConfig` parameter — so this is "no TLS work on
the plaintext path", not a binary/TU-level OpenSSL split (a separate non-OpenSSL TU is deferred; the
benchmark-fairness driver does not require it for Gate A).
**Storage**: N/A (transport layer)
**Testing**: GoogleTest; loopback `asio::ip::tcp::acceptor` for socket round-trip; mutation-tested seams;
sanitizer matrix (ASan/UBSan/TSan) + coverage per §IX.1.
**Target Platform**: Linux (clang); transport is OS-portable plain TCP.
**Project Type**: library (single project; the fixpp engine)
**Performance Goals**: plaintext removes the TLS handshake + per-record crypto — it is the *cheaper* path;
no per-message heap on the in-memory path (§XV.1); caller-owned read buffers (no transport read alloc).
**Constraints**: `[const §XIV.2]` ≤5 pure-virtual per pluggable interface; `[const §XII.5]` (amended)
opt-in-only + loud friction; fail-closed; frozen-at-open.
**Scale/Scope**: surgical — 1 new transport + 1 new factory + 1 enum value + 1 factory `kind()` query +
profile-gated branches in session-open / reconnect-FSM / accept-loop / listener. Excludes the phase-9
bench driver (FR-012).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design (below).*

- **Article XII §5 (explicit SecurityProfile; amended v0.3)** — ✅ This feature *is* the consumer of the
  v0.3 amendment. `insecure_plain_tcp` is opt-in-only (D-4/D-6: never an implicit default; `unset` still
  rejected), with the mandated loud `[[deprecated]]`-class friction (D-9). **Article XX:** the amendment's
  Codex Gate A folds into THIS feature's Gate A.
- **Article XII §1–§4 (TLS mechanism)** — ✅ inapplicable on the plaintext path (no TLS context built),
  vacuously satisfied per the amended §5; the TLS path is behaviour-unchanged (D-1 leaves
  `asio_tls_transport` untouched).
- **Article XII §7 (EncryptMethod(98)≠0 rejected)** — ✅ ENFORCED by 043 on the inbound Logon path.
  **CORRECTION (2026-06-17, post-implement, user-ratified):** this row was a **false-green at Gate A** —
  the pre-043 baseline did NOT reject inbound `98≠0` (`interpret_logon` skipped tag 98; S-021 "inbound
  98≠0 not handled"; TC-017 backlog). The discrepancy surfaced during `/speckit-implement` (US1-d) and was
  acute on the plaintext profile (a peer requesting app-layer encryption over plain TCP would silently get
  cleartext). 043 closes it: `interpret_logon` now rejects `98 ≠ "0"` (malformed fails closed) with
  `session_invalid_logon`, all profiles, witnessed by `tests/session/test_interpret_logon_encrypt_method.cpp`.
  Plaintext removes *transport* encryption only. (FR-009.)
- **Article XII §9 (security feature → 4 controls)** — `/clarify` ✅ done (reference-engine sweep + 3
  decisions); `/analyze` pending (step 6); **Codex Gate A** pending (required, after this plan); user
  `/plan` sign-off pending. In scope and tracked.
- **Article XIV §2 (pluggable interface ≤5 pure-virtual)** — ✅ `Transport` base = 5 (plain uses all 5,
  the `TlsTransport` +1 extension slot unused). `TransportFactory` pure count stays **3** — the new
  `kind()` (D-5) is a **defaulted** virtual (default `tls`), so it adds no pure-virtual and does not break
  the ~11 existing test-double factories.
- **Article XV §1 (no per-message hot-path heap)** — ✅ plain read/write are caller-buffer; the transport
  never allocates a read buffer (mirrors TLS); no MemoryStore/in-memory-path alloc added.
- **Article XVI §3 (`/clarify` mandatory for security)** — ✅ completed before this plan.
- **Article XVII (Gate A/B)** — Gate A after /plan (security trigger); Gate B before merge. Tracked.

**No violations.** Complexity Tracking table below is empty.

## Project Structure

### Documentation (this feature)

```text
specs/043-plaintext-tcp-transport/
├── plan.md              # This file
├── research.md          # Phase 0 — design decisions D-1..D-13
├── data-model.md        # Phase 1 — entities
├── quickstart.md        # Phase 1 — operator usage
├── contracts/           # Phase 1 — header contracts (asio_plain_transport, plain factory, kind())
│   ├── asio_plain_transport.hpp
│   └── plain_transport_factory.hpp
├── checklists/
│   └── requirements.md  # spec quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 — /speckit-tasks output (NOT created here)
```

### Source Code (repository root = the library submodule)

```text
include/fixpp/
├── session/security_profile.hpp          # + kind::insecure_plain_tcp [[deprecated]] (D-9)
└── transport/
    ├── transport_factory.hpp             # + transport_security_kind enum + kind() defaulted virtual (D-5);
    │                                      #   + asio_plain_transport_factory decl + make_asio_plain_transport_factory()
    └── plain_transport.hpp (optional)     # public alias/fwd if needed; impl lives in src/

src/
├── transport/
│   ├── asio_plain_transport.hpp / .cpp   # NEW — Transport impl, 5 base ops, no handshake (D-1/D-2)
│   ├── transport_factory.cpp             # + asio_plain_transport_factory bodies + kind() overrides (D-3/D-5)
│   └── asio_listener.{hpp,cpp}           # Config += transport_security_kind transport_kind{tls};
│                                          #   async_accept mints plain accept-factory when plaintext (D-4 site #2/E-7)
└── session/
    ├── session.cpp                       # open(): accept profile + FR-008 consistency (D-6);
    │                                      #   SK→TK mapping skips SslCtxConfig for plaintext (D-7);
    │                                      #   auto-derive plain factory (D-4 initiator)
    ├── engine.cpp                        # run_accept_loop THREE sites (E-7): :664-681 profile-map add
    │                                      #   insecure_plain_tcp arm (no mtls_ca fall-through);
    │                                      #   :783-797 skip handshake on plaintext (D-4/D-8);
    │                                      #   fix stale :322 comment + generalise assert body (D-13)
    └── reconnect_fsm.{hpp,cpp}           # set_plaintext_profile + step-6 handshake skip (D-7)

tests/
├── transport/                            # asio_plain_transport unit + factory + loopback round-trip (SC-001);
│                                          #   Config TCP-knob + close()-no-tls-timeout witness (SC-008)
├── session/                              # open() accept + effective-factory mismatch reject incl.
│                                          #   TLS-profile+plaintext-engine-default (SC-003); plaintext authz
│                                          #   skip + live_peer_id_==nullopt (SC-004); profile gates;
│                                          #   run_accept_loop end-to-end on insecure_plain_tcp (SC-001 acceptor)
└── (deprecation diagnostic witness)      # SC-005 — selecting insecure_plain_tcp warns (build-observable)
```

**Structure Decision**: Single-project library. Net-new files: `src/transport/asio_plain_transport.{hpp,
cpp}`. Edited: `transport_factory.{hpp,cpp}`, `session/security_profile.hpp`, `session.cpp`, `engine.cpp`,
`reconnect_fsm.{hpp,cpp}`, `asio_listener.cpp`. Sibling-of-TLS-transport; no new module/layer.

## Phase 1 re-check (post-design Constitution Check)

Design (D-1…D-13) introduces: 1 transport class (5 base virtuals, 0 new), 1 factory class (implements the
existing 3 pure-virtuals + overrides the new defaulted `kind()`), 1 enum value, 1 factory-kind enum,
profile-gated branches. No new wire field, error slot, codegen, or config field beyond the enumerator +
factory-kind enum (FR-013). ≤5 pure-virtual caps hold (Transport 5/5; TransportFactory pure count stays
3/5 — `kind()` is defaulted). **No new violations. Gate A may proceed.**

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| — | — | — |

## Gate A

- Round 1 applied 2026-06-17: Codex P1=1 P2=3 P3=2; Opus post-judging P1=1 P2=5 P3=4; rewrite addresses
  root causes RC#1 (acceptor path specified as a contract, not behaviour — the coordinated THREE-site change
  engine.cpp:664-681 profile-map arm + asio_listener Config `transport_kind` selector + engine.cpp:783-797
  handshake skip, no `mtls_ca` fall-through, plus plaintext accepted transports get no TLS-validation event
  hooks; data-model E-4 "4 virtuals" corrected to 3 pure + defaulted `kind()` + concrete non-virtual
  `make_accepted`; acceptor end-to-end `run_accept_loop` test added), RC#2 (`live_peer_id_` made a MUST-stay-
  `nullopt` fail-closed-by-construction across research D-10 ⇄ data-model E-7 ⇄ spec — no fake
  `handshake_result`; all three `hr` fields enumerated inert per P2-B), RC#3 (FR-008 keyed on the
  **effective/resolved** factory, not the override alone, so a plaintext engine-default for a TLS session
  fails at `open()` not at the FSM cast — spec FR-008 + SC-003 + new US3 AC + research D-6 + data-model E-6),
  RC#4 (SC renumbered monotonic SC-001..SC-008 + added SC-008 for FR-010/FR-011 + advanced the checklist
  phase note); plus P3-A (D-13 extended to note the `assert_transport_on_session_strand` body — engine.cpp:
  317-332 — silently skips plain transports) and P3 "no OpenSSL" softened (plan.md Primary Dependencies).
  Reviews: research/reviews/codex_043-plaintext-tcp-transport_gate_a_review.md,
  research/reviews/opus_043-plaintext-tcp-transport_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-17: Codex P1=0 P2=1 P3=2; Opus post-judging P1=0 P2=2 P3=2; rewrite addresses
  RC#1 (FSM effective-factory wiring contract) + RC#2 (stale-doc-bundle drift: listener accept-factory
  holding reconciled across D-3/D-4/E-4/E-7) + checklist. Reviews:
  research/reviews/codex_043-plaintext-tcp-transport_gate_a_2_review.md,
  research/reviews/opus_043-plaintext-tcp-transport_gate_a_2_adversarial_review.md.
