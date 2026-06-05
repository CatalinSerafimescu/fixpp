# Implementation Plan: PossResend(97) Inbound + AllowPosDup Send-Path Strip (S-010 completion, G3 slice 2)

**Branch**: `022-possresend-allowpossdup-send` | **Date**: 2026-06-05 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/022-possresend-allowpossdup-send/spec.md`

## Summary

Complete catalogue row **S-010** (flip `backlog → done`) by delivering the two pieces `021-inbound-possdup-origsendingtime` deferred:

1. **Inbound `PossResend(97)` (US1) — witness-only, ZERO production code.** The `/speckit-clarify` reference-engine sweep (spec Clarifications 2026-06-05) confirmed **neither** QuickFIX-cpp v1.16.0 nor QuickFIX-J 3.0.1 handles `PossResend(97)` in its session layer (the field is defined but never read in `Session.cpp`/`Session.java`); a `97=Y` application message simply flows to the application, which reads tag 97 itself. fixpp's `Application::fromApp` already delivers the **full `MessageView`** (`application.hpp:84` — the 021 redeliver path relies on this), so an in-sequence `97=Y` app message already reaches the app unchanged. US1 ships as **parity/witness tests** proving fixpp processes a `97=Y` app message normally (seqnum advances), delivers it to `fromApp`, and never session-rejects it for `97`, in both roles.

2. **`AllowPosDup` send-path strip knob (US2, FR-008 from 021) — the only production change.** Add one additive `SessionConfig` field `bool allow_pos_dup = false` (the QuickFIX-J config-key spelling exactly, for 008 `cfg_loader` parity) and, in `Session::send_impl`, a **fail-closed, boundary-anchored excision** that — when `allow_pos_dup == false` (default, strip) — removes the complete `43=…`<SOH> and `122=…`<SOH> fields from the caller's opaque `app_payload` before framing; when `true`, passes them through verbatim.

**Key design grounding (CodeGraph + source sweep, in [research.md](./research.md)):**

- **Strip semantics match BOTH engines' default.** QuickFIX-cpp `Session::send` (`Session.cpp:533-537`) removes `PossDupFlag`/`OrigSendingTime` **unconditionally**; QuickFIX-J `Session::send(msg, allowPosDup)` (`Session.java:2788-2796`) removes them unless `AllowPosDup=true`. fixpp adopts QFJ's knob shape with the same default = strip.
- **The excision is injection-safe by construction**, because it runs **after** the existing 020 opaque-payload validation (`send_impl`, `session.cpp:2900-2926`), which already guarantees the payload begins with `35=` and is a sequence of well-formed SOH-delimited `<tag>=<value>` fields, rejecting anything malformed with `app_payload_malformed=131`. The strip reuses the same **boundary-anchored** matcher (`has_boundary_token`, `session.cpp:2917`) so a `43=`/`122=` substring **inside another field's value** is never matched ([[feedback_delimiter_injection_verbatim_field_copy]] — the 020 RC#1 hazard). `35=` is always field 0, so excision never touches the MsgType.
- **The auto-resend path is independent of the knob by construction.** Retransmission re-serializes stored frames via `build_replay_frame` (`session.cpp:1186-1239`), which re-adds `43=Y`+`122` (`:1220`,`:1227`) and **never routes through `send_impl`**. FR-007 therefore holds without any resend-path change; a witness pins it.

**This is a bounded, single-site production change.** It reuses: the 020 opaque-payload validation + `has_boundary_token`, the existing `send_impl` stack-buffer framing (no heap — [const §VIII.5]), the `SessionConfig` additive-field pattern (cf. 021 `redeliver_poss_dup`), and the 018/020 live-interop fixture. No new infrastructure, no new module, no codegen, **no new `error::core` enum slot** (`app_payload_malformed=131` already covers fail-closed framing), no new public C-ABI surface.

## Technical Context

**Language/Version**: C++23 (Clang; coroutines, `std::expected`) — [const §II]
**Primary Dependencies**: existing `session::Session::send_impl` + the 020 opaque-payload validator (`has_boundary_token`), `SessionConfig`, 019 `Application::fromApp`, `build_replay_frame` (read-only, for the resend-independence witness), `core::{error,expected_t}` — no new third-party deps
**Storage**: N/A — no persistent state touched; the strip is a pure pre-framing transform over the caller payload
**Testing**: GoogleTest + GoogleMock; sanitizers ASan/UBSan/TSan; coverage llvm-cov; mallocnesia no-heap gate; live interop ctest cells (skip-without-counterparty) extending the 018/020 fixture — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1); Windows/MSVC (Tier 2)
**Project Type**: single C++ library (`fixpp`) + tests-only interop-harness extension (parent `phase-9-harness/`)
**Performance Goals**: the strip stays on the existing no-heap send path — it copies the surviving fields into the same 4096-byte stack scratch the framer already uses (or excises in place), preserving the [const §VIII.5] / INV-5 "no heap on the send path" rule (binding gate = mallocnesia LD_PRELOAD, `/speckit-verify` Step 6)
**Constraints**: `noexcept`/`expected_t` house style; no `std::mutex` in awaitable headers ([const §XV.9] — the strip lives in `session.cpp`, the only header touch is one additive `SessionConfig` POD field, no new include into `session.hpp`'s awaitable closure); `43`/`122` matched as raw boundary-anchored byte tokens, no locale/field-parse
**Scale/Scope**: **1** production site (`send_impl` strip pre-pass) + **1** additive `SessionConfig` knob (`allow_pos_dup`) + unit witnesses (strip default/retain, embedded-`43=` injection hostile witness, resend-independence, no-op-when-absent) + US1 `PossResend(97)` parity/witness tests + live interop cells (QFJ + QFcpp, both roles). No inbound-FSM change, no FIXT/5.0SP2 (G4), no other G3 knobs.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed — design adds no new violation).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **S-010** (`PossDupFlag(43)+PossResend(97)` duplicate detection, `feature-catalogue.md:30`) `backlog → done` — this slice delivers the PossResend(97) inbound disposition (witness-confirmed) **and** the `AllowPosDup` send-path knob, the two pieces 021 left open. Exact catalogue/coverage-index delta below (applied at **Polish**, per the 020/021 precedent). | ⚠ RESOLVED (S-010 → done; delta specified, applied at Polish before merge) |
| **VII** Testing/TDD | every behavior lands RED-first: strip-default (43/122 gone), retain (passthrough), **embedded-`43=` injection hostile witness** (only the boundary field excised), resend-independence (replay still carries 43/122), no-op-when-absent; US1 PossResend parity witnesses red-first; live cells red-first; GoogleTest | ✅ planned |
| **VII.6** Interop | extends the live QFJ/QFcpp both-role matrix with an `AllowPosDup`-strip wire-capture cell (assert outbound bytes free of 43/122) + a `PossResend(97)` deliver cell (SC-005) | ✅ planned |
| **VIII.5** Allocator | the strip copies surviving fields within the existing 4096-byte send stack scratch (no heap); binding INV-5 gate = mallocnesia LD_PRELOAD (`/speckit-verify` Step 6, `test_session_alloc_guard`) | ✅ by design |
| **IX.1** Coverage | ≥95/85 on the new `send_impl` strip branch + helper; the strip's both arms (strip/retain) + the malformed/absent edges are genuine tested paths | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the send-path change + interop ctest (018/020 discipline) | ✅ planned |
| **X** ABI | no C-ABI surface; the one `SessionConfig` knob (`allow_pos_dup`) is an additive default-valued C++ field in the **public** header `include/fixpp/session/session_config.hpp` (no break); **no new `error` enum slot** (`app_payload_malformed=131` reused for fail-closed framing) | ✅ N/A / additive |
| **XI.4** Threading | the strip is a synchronous pre-framing transform inside the existing `send_impl` (already on the caller→strand send path); **no new concurrency surface**, no callback, no off-strand call. US1 reuses the existing inbound `fromApp` delivery context (L-019-3). | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | no new pluggable interface; the knob is a plain `SessionConfig` field | ✅ N/A |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | strip lives in `session.cpp`; the only header touch is one additive `SessionConfig` POD field — verify no new include drags a mutex into `session.hpp` (Tier-1 unfiltered / `-L sync` per the §XV.9 watch-item) | ✅ PASS (watch-item flagged for verify) |
| **XV.15** No app-message drop | the strip removes **fields within** an outbound message (caller-supplied `43`/`122`), never a whole message; it is the operator-requested ownership of duplicate flags, not a backpressure/queue drop. No message leaves the sequence contract. | ✅ PASS (field strip, not message drop) |
| **XVI.3 / XVI.4** /clarify before /plan | session send-path + config trigger → `/speckit-clarify` Session 2026-06-05 (2 axes: `AllowPosDup` name/default, PossResend witness-vs-surface), engine-grounded ✅ | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⚠ Gate A PENDING |

**Result**: PASS to proceed. One production-behavior change (the `send_impl` `43`/`122` strip) is surfaced in Complexity Tracking for Gate-A scrutiny; it is a single bounded site that reuses the proven 020 validation substrate, and its sole real hazard (delimiter injection) is closed by construction + a dedicated hostile witness. No unjustified violations.

**Exact §VI delta (written before `/speckit-tasks`; applied at Polish):**
- `spec/feature-catalogue.md`:
  - **S-010** (`PossDupFlag(43) + PossResend(97)` — duplicate detection semantics, `:30`) `backlog → done`, cite 022. Replace the 021 partial-delivery note's "Do NOT flip to done — PossResend(97) + send-path unimplemented" with a completion note: *"PossDupFlag(43) inbound semantics delivered by 021; PossResend(97) inbound disposition (witness-confirmed: delivered to fromApp, never session-rejected for 97) + the AllowPosDup send-path strip knob (FR-008) delivered by 022 → row complete."*
  - The **AllowPossDup send-path knob** sub-note (021 had it as `backlog`/DEFERRED) → delivered (022, knob `allow_pos_dup`, default strip).
  - Append the new B-/L- entries (below).
- `spec/coverage-index.md`: flip **S-010** to `done` with a 022 reference; remove it from the deferred set; keep `NextExpectedMsgSeqNum(789)` + remaining G3 config knobs in the still-deferred set.
- `spec/behaviors-and-limitations.md`: add **B-022-1** (plain `send` strips caller `43`/`122` by default; `AllowPosDup=true` retains; boundary-anchored, fail-closed on malformed framing via slot 131; the auto-resend path always re-adds 43/122 independent of the knob) and **L-022-1** (`PossResend(97)` is delivered to the application for business-level duplicate determination — fixpp adds **no** session-level PossResend handling, matching QFcpp/QFJ; the app must dedup on its own business keys).

## Project Structure

### Documentation (this feature)

```text
specs/022-possresend-allowpossdup-send/
├── plan.md              # this file
├── research.md          # Phase 0 — engine-grounded decisions (D1..D6)
├── data-model.md        # Phase 1 — the AllowPosDup knob + strip transform + PossResend disposition table
├── contracts/
│   └── session-send-possdup.md   # the send-path strip contract + SessionConfig knob surface + PossResend inbound contract
├── quickstart.md        # Phase 1 — how to exercise (unit + hostile witness + live interop cells)
├── checklists/
│   └── requirements.md  # spec-quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 — /speckit-tasks (NOT created here)
```

### Source Code (repository root = library submodule)

```text
src/session/
└── session.cpp          # PRIMARY (behavior site): in send_impl, AFTER the 020 opaque-payload
                         #   validation (~:2900) and BEFORE SendingTime stamp/frame build, add the
                         #   AllowPosDup strip pre-pass — when !cfg_.allow_pos_dup, excise
                         #   boundary-anchored 43=…SOH and 122=…SOH via the has_boundary_token
                         #   primitive into the send stack scratch; build_replay_frame UNCHANGED.
include/fixpp/session/
└── session_config.hpp   # PUBLIC header: +1 additive POD knob: bool allow_pos_dup = false
                         #   (next to 021 redeliver_poss_dup; QFJ-parity name; default strip)

tests/session/
├── test_send_allow_pos_dup_strip.cpp     # NEW (US2): strip-default / retain / embedded-43= injection
│                                         #   hostile witness / no-op-when-absent / resend-independence (RED-first)
└── test_inbound_poss_resend.cpp          # NEW (US1): 97=Y in-seq app msg → fromApp gets full frame,
                                          #   seqnum advances, no reject; no-app byte-identical;
                                          #   43=Y+97=Y → 021 PossDup arms on 43 only (RED-first/parity)

tests/interop/                        # extend 018/020 fixture: AllowPosDup wire-capture cell + PossResend deliver cell
phase-9-harness/                      # parent: live cells (QFJ/QFcpp, both roles)
```

**Structure Decision**: Single-library change centred on one site in `src/session/session.cpp` (`send_impl`, the existing send pipeline) plus **one** additive public-header `SessionConfig` field (`allow_pos_dup`). US1 adds no production code (witness tests only). No new modules, files-of-record, or public C surface. The interop witnesses extend the existing 018/020 fixture and the parent harness.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| `send_impl` gains an `AllowPosDup` strip pre-pass: excise boundary-anchored `43=`/`122=` from the caller's opaque payload when the knob is default-strip | FR-006/008 — operator ownership of duplicate flags; the send-path half of S-010, deferred from 021 because the opaque payload had no strip seam | Adds a **byte-level transform on the production send path** that rewrites caller payload. Its sole hazard is delimiter injection — a caller value containing `\x0143=Y` or an embedded `43=` could over-/under-excise ([[feedback_delimiter_injection_verbatim_field_copy]], the 020 RC#1 class). Closed **by construction** (runs after the 020 validation that guarantees SOH-delimited framing; uses the boundary-anchored `has_boundary_token`, never an unanchored match) **and** by a dedicated RED-first hostile witness that injects `43=` inside a field value and asserts only the true boundary field is removed. Must also preserve no-heap (mallocnesia) and the 020 fail-closed disposition (slot 131). |
| Inbound `PossResend(97)` (US1) | S-010 completion; interop correctness | **No production change** (witness-only — clarify-confirmed: both engines do nothing session-level; fixpp's `fromApp` already delivers the full frame). Carries Gate-B review only as parity/witness tests proving no regression (a `97=Y` app message is never mis-rejected and reaches `fromApp`). |

No 4th-project / repository-pattern / speculative-abstraction violations. The first row is a bounded byte-transform on the send path, justified by the feature and de-risked by reusing the 020 validated-framing substrate; the second is tests-only.

## Gate A

- PENDING — runs after this plan, before `/speckit-tasks` ([const §XVII.1]). Reviews will land at `research/reviews/{codex,opus}_022-possresend-allowpossdup-send_gate_a_*`.
