# Implementation Plan: FIX 4.4 closeout — session-negotiation fields + XMLnonFIX passthrough

**Branch**: `070-fix44-closeout` | **Date**: 2026-07-12 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/070-fix44-closeout/spec.md`

## Summary

Close the last four open FIX 4.4 catalogue rows — S-029 TestMessageIndicator(464),
S-030 MaxMessageSize(383), S-037 NoMsgTypes(384) advertise, and A-034 XMLnonFIX(35=n)
passthrough — as four **additive, opt-in** capabilities on the existing Logon handshake +
inbound dispatch. With none of the new configuration set, engine behavior is byte-for-byte
and disposition-for-disposition identical to the pre-feature baseline (FR-012). No public /
C-ABI surface change (C-ABI GA-frozen at 1.5.0 — FR-013); no Python.

Technical approach:
- **S-029**: new `std::optional<session_posture>` in `SessionConfig` (`enum class session_posture { production, test }`, default `nullopt` = disabled). Inbound: add tag 464 to the header pre-scan (`scan_frame_header.hpp`); on an inbound Logon, when posture is set, apply the symmetric rule (`464=Y` ⇒ peer test; `464=N`/absent ⇒ peer production) and refuse a mismatch by emitting a Logout(35=5) with distinct posture-mismatch text, then Disconnect — mirroring the existing Logon-time Logout+disconnect disposition (`session.cpp:2676-2702`). Outbound: advertise `464=Y` when local posture == test, via the new `logon_advertise_options`.
- **S-030**: `std::optional<std::uint32_t> advertised_max_message_size` in `SessionConfig`. Outbound: emit `383=<value>` in our Logon when set. Inbound: capture the peer's advertised 383 into session state (observability, FR-007); hard-enforce **inbound-only** — at the top of `on_inbound_frame` (`session.cpp:1961`), if `frame.size()` exceeds OUR advertised max, disconnect with a distinct "negotiated max message size exceeded" reason. Distinct from and never weaker than the absolute `max_frame_bytes` framer backstop (`framer.hpp:21`, `framer.cpp` — unchanged).
- **S-037**: `std::vector<supported_msg_type>` in `SessionConfig` (`struct supported_msg_type { char direction; std::string msg_type; }`, default empty ⇒ no group). Outbound: when non-empty, emit `NoMsgTypes(384)=k` + k contiguous `385`/`372` member pairs in config order in `build_logon`, honoring the bounded stack-buffer / fail-closed discipline. Inbound parse is already tolerated (proven by `test_066_arena_fit_test.cpp`).
- **A-034**: no parse/dispatch change — fixpp already parses the 212/213 length-delimited pair SOH-safe and routes 35=n to `fromApp` (`n` is not in `is_admin_msgtype`, `msgtype_classifier.hpp:43-50`). Ship a discriminating witness (embedded-SOH XmlData delivered on `fromApp`, tag 213 byte-exact) and pin FR-011 (validator accepts a well-formed 35=n even with `validate_inbound_messages` on — **already true**; see research.md D-E).

Shared enabler: one new trailing `const logon_advertise_options& opts = {}` param on `build_logon` bundling the 383 / 464 / 384 advertisements; two call sites updated (initiator emit `session.cpp:838`, acceptor reply `session.cpp:2490`).

## Technical Context

**Language/Version**: C++23 (Article II §1 — no fallback; free use of `std::optional`, `std::expected`, `std::pmr`, `std::span`).

**Primary Dependencies**: None new. Reuses the existing session/wire surface — `build_logon` (`admin_messages.{hpp,cpp}`), `interpret_logon`, `scan_frame_header`, `wire::Framer`, `wire::Parser`/offset table (212/213 LEN+DATA), `wire::Validator`, `fromApp` dispatch. New standard includes needed in `session_config.hpp` (`<optional>`, `<vector>`, `<string>`) are **already present** (`session_config.hpp:32-34`) — §XV.9 new-include justification is N/A.

**Storage**: N/A (no MessageStore contract change; no persisted-field addition).

**Testing**: GoogleTest + ctest (Article VII). Discriminating red-provable tests per capability (FR-014). Mirror existing patterns: `tests/session/test_fixt_logon_establishment.cpp` (Logon field assertions), `tests/session/conformance/tc_sendingtime_test.cpp` (`extract_field(sent, tag)` sent-wire assertion), `tests/session/test_066_arena_fit_test.cpp` (384-bearing Logon parse), `tests/session/test_027_*` (Logon-field emit + scan).

**Target Platform**: Linux (Clang 22 primary — sanitizers/coverage/fuzz; GCC release sanity) + Windows/MSVC (Tier 2) + libc++ (Tier 3). Platform-independent logic; no new platform surface.

**Project Type**: Single C++23 library (`fixpp`). Source under `src/`, `include/fixpp/`; tests under `tests/`.

**Performance Goals**: Preserve the zero-alloc / bounded stack-buffer discipline of `build_logon` (Article VIII §5 — no `new`/`delete` parse→dispatch). The 384 group is written into the existing `Writer` over the caller's stack buffer with bound-checked, fail-closed appends (no heap).

**Constraints**: Additive opt-in only — default-config behavior byte-identical (FR-012 / SC-006). No C-ABI change (FR-013). Inbound negotiated-size check is O(1) (`frame.size()` compare) at the top of `on_inbound_frame`.

**Scale/Scope**: 4 capabilities, 14 FRs, 6 SCs. Touched files: `session_config.hpp`, `admin_messages.{hpp,cpp}`, `scan_frame_header.hpp`, `session.{hpp,cpp}` (+ session-state field), and new test files. Estimated < ~400 net LoC of source.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Verdict: PASS** (opt-in additive; no article violated). Relevant articles:

- **Article I §3 / VI (Spec Coverage / 100% FIX Rule).** Feature moves S-029, S-030, S-037, A-034 to `done`-eligible. `spec/coverage-index.md` + `spec/feature-catalogue.md` rows updated at close-out; each row is backed by ≥1 discriminating test (FR-014 / SC-001). **Satisfied.**
- **Article XVIII §7 (Application-message codegen scope).** A-034 (XMLnonFIX, 35=n) is `msgcat='admin'`, explicitly *not* an application-writer/codegen row — this feature ships it as **runtime passthrough** (parse + `fromApp` delivery), not typed codegen, exactly as §7 prescribes ("runtime-XML only"). **Consistent, no amendment.**
- **Article II §1 (C++23) / VII §3 (TDD).** Red-green per capability; discriminating red-provable tests first (FR-014). **Satisfied.**
- **Article VIII §5 / XV.1 (zero-alloc hot path).** `build_logon` stays zero-alloc over the caller stack buffer; the S-037 384 group is the single new bounded-buffer write — bound-checked, fail-closed on overflow (returns `wire_*` error, no partial/garbage frame). No per-field or heap allocation added. **Satisfied — see Complexity Tracking.**
- **Article X / IV §2 (ABI Policy).** No `include/fix/c_api.h` change; new config is C++-only on the existing `SessionConfig` surface (FR-013). C-ABI GA-frozen at 1.5.0. Appendix-A "ABI surface change" trigger **not** fired. **Satisfied.**
- **Article XV.9 (new include justification).** `<optional>`/`<vector>`/`<string>` already included in `session_config.hpp:32-34`; adding a 464 field to `scan_frame_header.hpp` needs no new include (`<string_view>` already present). **N/A / satisfied.**
- **Appendix A / Article XVI §3 / XVII §1 (Session-FSM trigger).** S-029 adds an inbound-Logon refusal path and S-030 adds an inbound disconnect path — both touch the session FSM. `/speckit-clarify` (mandatory) **done** (4 clarifications, spec §Clarifications); `/speckit-analyze` pending after `/tasks`; Codex Gate A + user `/plan` sign-off apply per Appendix A. **On track.**
- **Article IX §2 (Sanitizers).** No new threading/concurrency surface — the new logic runs on the existing single-threaded session strand + inbound path. No new TSan surface. **Satisfied.**
- **Article XV.15 (no silent app/session drops).** S-029 refusal and S-030 over-size both **disconnect** (a defined, loud disposition) — no silent drop of an application/session message. **Satisfied.**

## Project Structure

### Documentation (this feature)

```text
specs/070-fix44-closeout/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 — decisions A–F
├── data-model.md        # Phase 1 — config entities, FrameHeader additions, session state
├── quickstart.md        # Phase 1 — integrator walkthrough
├── contracts/
│   └── session-config-and-logon.md   # Phase 1 — config + build_logon + emit/disposition contracts
└── tasks.md             # Phase 2 — /speckit-tasks (NOT created here)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── session_config.hpp        # + session_posture enum, advertised_max_message_size,
│                             #   supported_msg_type + list; copy-constructible static_assert holds (:483)
└── admin_messages.hpp        # + logon_advertise_options struct; build_logon gains trailing opts param (:54)

src/session/
├── admin_messages.cpp        # build_logon: emit 383/464/384 from opts (:79) — bounded, fail-closed
├── scan_frame_header.hpp     # FrameHeader += test_message_indicator(464) + max_message_size(383) (:38);
│                             #   switch += case 383/464 (:103)
├── msgtype_classifier.hpp    # UNCHANGED (35=n already routes to fromApp — :43-50)
└── session.cpp               # negotiated-size check @ on_inbound_frame top (:1961);
                              #   posture check after interpret_logon success, before acceptor reply (:2490);
                              #   Logout+disconnect disposition mirrors :2676-2702;
                              #   initiator emit (:838) + acceptor reply (:2490) pass opts;
                              #   capture peer 383 into a new session-state field

include/fixpp/wire/
├── framer.hpp                # UNCHANGED — default_max_frame_bytes 256 KiB backstop (:21)
└── validator.hpp             # UNCHANGED — already accepts 35=n (212/213 are header fields; research.md D-E)

tests/session/                # NEW discriminating tests (one per capability, FR-014):
├── test_070_posture_mismatch_test.cpp     # S-029: refuse cross-posture Logon
├── test_070_max_message_size_test.cpp     # S-030: advertise 383 + N/N+1 boundary disconnect
├── test_070_supported_msgtypes_test.cpp   # S-037: 384 group emit + round-trip
└── test_070_xmlnonfix_passthrough_test.cpp# A-034: 35=n embedded-SOH → fromApp byte-exact; validator accepts
```

**Structure Decision**: Single-project fixpp layout (Option 1). All changes land in the existing
`session/` and (test-only) `tests/session/` trees; no new module, subsystem, or public header. The
`wire/` framer + validator headers are read-but-unchanged (they set the backstop / already accept 35=n).

## Complexity Tracking

> Only one item rises above trivial-additive.

| Item | Why it needs care | Mitigation / why the simpler alternative was rejected |
|------|-------------------|-------------------------------------------------------|
| S-037 NoMsgTypes(384) repeating group in the hand-rolled `build_logon` | `build_logon` is a zero-alloc, bounded-stack-buffer writer (Article VIII §5 / XV.1). A repeating group of k `(385,372)` pairs is the first variable-length body construct in this builder; a naive loop could overflow the caller's stack buffer for large k. | Emit each member pair through the existing `wire::Writer::append_raw`, which is already bound-checked and returns a `wire_*` error on overflow → propagate `std::unexpected` (fail-closed, no partial frame), identical to the existing 553/554/789 append pattern (`admin_messages.cpp:174-207`). No heap, no new arena. Rejected: pre-sizing / a temporary `std::vector` (would allocate on the hot path, violating §XV.1). |

No Constitution violations require justification; the table above documents the one non-trivial engineering item, not a violation.
