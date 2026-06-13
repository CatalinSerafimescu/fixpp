# Implementation Plan: Credential redaction at the message-store boundary

**Branch**: `034-credential-store-redaction` | **Date**: 2026-06-13 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/034-credential-store-redaction/spec.md`

## Summary

Mask the `Password(554)` value before an outbound Logon (`35=A`) frame is handed to the message
store, while transmitting the original unmasked frame on the wire. The mask is applied **once**, at
the single store-entry boundary (`Session::store_then_emit`), so every store backend and any future
store inherits it (FR-009). The mask is a **same-length, in-place** substitution of the 554 value
bytes into **coroutine-frame-resident fixed-size storage** (zero heap allocation — self-imposed
persist-path discipline aligned with `[const §XV.1]` per-message heap avoidance); the caller's original
`frame` span is transmitted untouched. No new wire field, error slot, config knob, codegen, C-ABI, or
store interface change — this is a behavior-only hardening internal to the session layer.

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI per `[const Art.II §2]`)
**Primary Dependencies**: standalone Asio (coroutines), existing `MessageStore` interface, existing FIX framer
**Storage**: existing `MessageStore` implementations (`FileStore`, `MemoryStore`, null) — **unchanged**
**Testing**: GoogleTest (session unit tests); on-disk store-byte witness; mallocnesia / counting-resource alloc gate
**Target Platform**: Linux (Tier-1); platform-agnostic byte logic
**Project Type**: single library (`fixpp`)
**Performance Goals**: zero added heap allocation on the persist path; O(frame) single scan, only for 554-bearing Logons
**Constraints**: zero-alloc persistence (self-imposed discipline aligned with `[const §XV.1]`); same-length mask (preserve `9=` BodyLength + store offsets/CRC); wire frame byte-identical
**Scale/Scope**: ~1 new inline byte-utility + a guarded branch in `store_then_emit`; ~1 new unit-test file. Est. < 80 LoC production.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Relevance | Disposition |
|---|---|---|
| **Appendix A — mandatory triggers** | This is a **security** feature | All four controls REQUIRED: `/clarify` ✓ (done, 1 Q), `/analyze` (step 6), Codex Gate A, **user `/plan` sign-off**. |
| **XVI §3 — `/clarify` mandatory before `/plan`** (security trigger) | Yes | ✓ completed before this plan. |
| **XV §1 — per-message heap avoidance** | Masking sits on the persist path | Same-length mask into **coroutine-frame-resident fixed-size storage** — the masked copy lives in the existing `store_then_emit` coroutine frame, enlarging it by ≤`kMaxMaskableLogonBytes` and adding **zero new allocations** (precedent: the resend path's `rp_buf` `std::array` in the same function, `session.cpp:4758`). Self-imposed persist-path discipline aligned with §XV.1, not a §VIII.5 parse→fromApp mandate. SC-004 count basis unchanged; witnessed by the alloc gate. PASS-by-design. |
| **XII — Security & TLS / credential handling** | Removes an at-rest cleartext-secret exposure | Aligns; deliberate hardening beyond reference-engine parity. No app-layer crypto introduced (`[const §XV.10]` untouched). |
| **IX §1 — coverage / sanitizers** | New byte logic | New code fully covered (DA/BRDA); ASan/UBSan/TSan over the new unit + touched session tests. |
| **X — ABI** | No public type/signature change | No new exported symbol; `store_then_emit` is internal. PASS. |
| **XI — Concurrency** | Masking runs inside the `noexcept` `store_then_emit` awaitable, async-mutex held throughout | Pure synchronous byte work before `co_await store`; no new suspension, no `std::mutex`. PASS. |
| **VI — 100% FIX rule / catalogue** | No new FIX message/field | No catalogue *row* add; updates B&L + corrects 033 disposition note (FR-010). |
| **XV — banned patterns** | — | None triggered (no sync-hot-path logging, no drop-oldest, no app-layer crypto). |

**No new surface**: no wire field, no error-taxonomy slot, no codegen, no C-ABI, no `SessionConfig` field, no `MessageStore` pure-virtual. Gate-clean.

## Project Structure

### Documentation (this feature)

```text
specs/034-credential-store-redaction/
├── plan.md              # This file
├── research.md          # Phase 0 — design decisions
├── data-model.md        # Phase 1 — masker contract + stored-frame invariant
├── quickstart.md        # Phase 1 — the on-disk witness recipe
├── contracts/
│   └── store-redaction.md   # internal contract: masker signature + store-boundary behavior
├── checklists/
│   └── requirements.md  # spec-quality checklist (done)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── logon_credentials.hpp        # + mask_tag554_same_length_inplace(std::span<std::byte>) — span, zero-alloc, sibling of redact_tag554
└── session.hpp                  # + static constexpr kMaxMaskableLogonBytes (FIXPP_TEST_HOOKS-gated test-seam; no public surface)

src/session/
└── session.cpp                  # store_then_emit: guarded copy→mask→store the masked span; transmit the original frame; + role-independent open()-time credential-length guard

tests/session/
└── test_credential_store_redaction.cpp   # NEW — on-disk store-byte witness + no-op + alloc-gate cells

spec/
├── behaviors-and-limitations.md # L-033-6 limitation → mitigation; (FR-010)
└── feature-catalogue.md / coverage-index.md  # traceability row for 034

specs/033-fixt-fix50sp2-session/tasks.md  # dated correction note on the T024/T020 "no production frame persistence" claim (no history rewrite)
```

**Structure Decision**: Single-library, in-place. The masker is an inline header utility beside the
existing `redact_tag554` (same module, same ownership). The production `.cpp` change is a guarded branch
in `store_then_emit` plus the role-independent `open()`-time credential-length guard; the only production
header change is the `FIXPP_TEST_HOOKS`-gated `kMaxMaskableLogonBytes` constant in `session.hpp` (no
public/exported surface — Art. X preserved). No layering change (`tools/check_layers.py` unaffected — session-internal).

## Phase 0 — Research

See [research.md](./research.md). Resolves: where to mask (single boundary), how to mask same-length
zero-alloc, coroutine-frame buffer sizing & the >bound fallback, MsgType gating, CRC/`10=` consistency, and what
`retrieve()` returns.

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md) — the masker function contract; the stored-frame invariant
  (masked-at-rest, same-length, never-replayed); the wire/store divergence proof.
- [contracts/store-redaction.md](./contracts/store-redaction.md) — masker signature + the
  `store_then_emit` masking behavior contract (pre/post-conditions, no-op cases).
- [quickstart.md](./quickstart.md) — the P1 witness recipe (open FIXT session w/ password + FileStore,
  read store-file bytes from disk, assert password absent + same-length mask present).

## Complexity Tracking

No constitution violations to justify. The single non-trivial choice (coroutine-frame-buffer sizing vs the
256 KiB store max-frame) is resolved in research by **binding the maskable copy to the `build_logon`
builder's maximum output capacity (`kMaxMaskableLogonBytes`) + an `open()`-time credential-length guard**,
not by allocating — kept minimal per the Karpathy "simplest thing" rule.

## Gate A

- Round 1 applied 2026-06-13: Codex P1=1 P2=2 P3=1; Opus post-judging P1=0 P2=3 P3=5; rewrite addresses root causes RC1 (bound→build_logon capacity, dead-defensive; keep I-07 disposition), RC2 (MsgType=A gate = safety boundary, reject mask-any-554), RC3 (§VIII.5→§XV.1 cite sweep) + N1 (parameterized dead-branch coverage) + N2 (acceptor open()-guard symmetry). Reviews: research/reviews/codex_034-credential-store-redaction_gate_a_review.md, research/reviews/opus_034-credential-store-redaction_gate_a_adversarial_review.md.
- Round 2 2026-06-13: **CONVERGED** — Codex P1=0 P2=0 P3=2; Opus post-judging P1=0 P2=0 P3=2 ("converged — ready for /tasks"). The flagged test-seam-ABI concern was judged NOT a contradiction (`Session` is non-template; `store_then_emit` private; `FIXPP_TEST_HOOKS` + `static constexpr kRpBufSize` precedent). The 2 residual P3s (contracts:27 wording slip; research:58 test-seam wording → test-only/internal, no public surface) applied as doc edits. User-signed-off 2026-06-13. Reviews: research/reviews/codex_034-credential-store-redaction_gate_a_2_review.md, research/reviews/opus_034-credential-store-redaction_gate_a_2_adversarial_review.md.

### Implementation-discovered mechanism deviations (2026-06-13, recorded for Gate B — NOT drift)

Two seams in the converged design (R3 / N1) assumed a `FIXPP_TEST_LOGON_MASK_BOUND` **compile-time bound override** to drive the dead over-bound branch and to measure the no-alloc NFR. Implementation found that assumption could not be realized as written, and substituted equivalent mechanisms that reach the **same** coverage/assurance outcome with the same `FIXPP_TEST_HOOKS` gate and zero production surface:

1. **T010 over-bound BRDA — frame-injection, not bound-override.** `kMaxMaskableLogonBytes` is consumed inside `store_then_emit`, which is compiled into `libfixpp_session` **without** `FIXPP_TEST_HOOKS`/`FIXPP_TEST_LOGON_MASK_BOUND` (confirmed via `compile_commands.json`). So a `FIXPP_TEST_LOGON_MASK_BOUND` define on a *test target* cannot change the library's baked-in 256. The dead `#if defined(FIXPP_TEST_HOOKS) && defined(FIXPP_TEST_LOGON_MASK_BOUND)` arm was **collapsed** to a plain `static constexpr kMaxMaskableLogonBytes = 256`, and a `FIXPP_TEST_HOOKS`-gated accessor `store_then_emit_test_access()` (methods-only — no `Session` layout change vs the no-hooks library, same shape as the existing `seqnum_mgr_test_access`) lets T010 feed a hand-crafted **>256-byte `35=A` frame** straight into the REAL branch against the REAL 256 bound. Earns the over-bound BRDA with zero production ABI. **Mutation-proven discriminating** (flip `skip_store` true→false → both T010 (a) assertions go RED). Art. X preserved (no public/exported surface; accessor is `FIXPP_TEST_HOOKS`-only).

2. **T011 no-alloc NFR — by-construction primary, mallocnesia gate present-but-inert.** T011 follows the established `NoHeap.*` + `_mallocnesia`/`check_alloc.py` pattern (027). The mallocnesia LD_PRELOAD guard is **inert in `linux-clang-debug`**: the `alloc_guard_start/end` weak symbols defined in the test executable bind locally (clang does not route intra-executable calls through the PLT), so LD_PRELOAD never arms the counting window — **proven identical on the shipped 027 gate** (an injected `std::malloc` in the guarded window does not trip either gate). This is a **pre-existing, project-wide** condition, not a 034 regression (captured as a separate finding in `REMAINING-WORK.md`). Therefore **SC-004's primary evidence is zero-alloc BY CONSTRUCTION** — the masker is `std::array` + `std::memcpy` + in-place byte overwrite with no allocating operation (verifiable by source inspection); the mallocnesia cell is present for pattern-consistency and future build configs, not relied upon as the enforcing gate. (Fallback if a Gate B reviewer rejects by-construction: a test-local global-`operator new` counter around the window, build-config-independent — not added now.)
