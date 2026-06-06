# Implementation Plan: Nanosecond-resolution SendingTime / OrigSendingTime (G3 slice)

**Branch**: `026-nanosecond-sendingtime` | **Date**: 2026-06-06 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/026-nanosecond-sendingtime/spec.md`

## Summary

Add nanosecond-resolution `SendingTime(52)` / `OrigSendingTime(122)` support, selectable per session, defaulting to millisecond (FIX 4.x parity, byte-identical no-op). The core time layer already does almost all the work: `core::utc_time_to_fix_string` computes the sub-second remainder **in nanoseconds** (`src/core/fix_time.cpp:130 ns_rem`) and truncates to the requested precision, and `core::fix_string_to_utc_time` already composes the parsed fraction **as nanoseconds** (`:277-287`). `utc_time_point = std::chrono::time_point<std::chrono::system_clock>` is nanosecond-resolution on libstdc++ (all Linux Tier-1 profiles). So the change is bounded:

1. **Core**: extend `fix_time_precision` with `nanos` (9 digits), add the nanos branch to the formatter (emit the full `ns_rem`, 27-char timestamp), and make the parser **lenient** — accept any sub-second width 1–9 (length 17–27), reject only non-digit / empty-fraction / >9-digit sub-seconds.
2. **Session**: add one additive `SessionConfig` field `sending_time_precision` (the `fix_time_precision` enum, default `millis`) and thread it through **both** SendingTime-stamping helpers — the public `session::stamp_sending_time` (`sending_time.cpp`, 2 call sites) and the file-local `stamp_sending_time(Clock&)` (`session.cpp:1295`, ~23 send sites). The precision parameter is **non-defaulted** so the compiler forces every call site to pass `cfg_.sending_time_precision` (turning the exhaustive-sweep requirement into a compile-time guarantee — no silent missed-site emitting the wrong precision).

**Key design grounding (source sweep + reference engines, in [research.md](./research.md)):**

- **Config = enum, default millis** (Clarifications). QFcpp uses `TimestampPrecision` (int 0–9); QFJ uses `TimeStampPrecision` (enum SECONDS/MILLIS/MICROS/NANOS, default MILLIS). fixpp reuses its existing `fix_time_precision` enum + `nanos` — matches QFJ, cleaner than a raw int, already the formatter's parameter type.
- **Lenient inbound parse** (Clarifications): accept any 1–9 fraction digits, pad to nanoseconds; matches QFcpp (`FieldConvertors.h` accepts length 17–27) and is simpler than the current strict 17/21/24 check. Emit stays at the configured standard precision.
- **Version-gating**: QFcpp/QFJ gate sub-second to FIX4.2+/FIXT.1.1. fixpp is FIX.4.4 → sub-second always permitted; the FIXT/5.0SP2 gate defers to G4.
- **OrigSendingTime(122)**: preserved verbatim on resend (it is a copy of the original `52`); only newly-stamped `SendingTime(52)` uses the configured precision.
- **Default millis ⇒ pure no-op**: with the field unset, every `52=` is the 21-char millis form, byte-identical (FR-003/SC-002).

**Bounded change: +1 core enum value, formatter nanos branch + lenient parser, +1 additive `SessionConfig` field, precision threaded (non-defaulted) through 2 stamping helpers and ~25 call sites.** No new wire field, no new error slot (the formatter reuses `decimal_buffer_too_small`; the parser reuses `wire_invalid_field_format`), no codegen, no C-ABI surface. Orthogonal to the seqnum/persistence/handshake area.

## Technical Context

**Language/Version**: C++23 (Clang; chrono, `std::expected`) — [const §II]
**Primary Dependencies**: `core::{fix_time_precision, utc_time_to_fix_string, fix_string_to_utc_time}` (extended), `session::stamp_sending_time` + the file-local `stamp_sending_time(Clock&)` (precision-threaded), `SessionConfig` (+1 field), `core::Clock`. No new third-party deps.
**Storage**: none — this is a wire-format/precision change; no persistent state, no store interaction.
**Testing**: GoogleTest; sanitizers ASan/UBSan/TSan; coverage llvm-cov; no-heap (the formatters are stack-buffer/noexcept); the existing `fix_time` round-trip + property tests extended to nanos; live interop ctest cells (skip-without-counterparty). — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1, libstdc++ → ns-resolution system_clock); Windows/MSVC (Tier 2 — `system_clock` ~100 ns ticks, documented resolution ceiling)
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension
**Performance Goals**: N/A — the formatter is a fixed-cost stack format on the existing send path; nanos adds 3 more digits, no allocation, no new suspension
**Constraints**: `noexcept`/`expected_t`; no `std::mutex` in awaitable headers ([const §XV.9] — the only header touches are the core enum value (pure chrono header, no mutex) and one `SessionConfig` enum field; adding `#include <fixpp/core/fix_time.hpp>` to `session_config.hpp` pulls a mutex-free chrono header — verify it does not perturb the `session.hpp` awaitable closure via `-L sync`); the default-millis path must be byte-identical (FR-003)
**Scale/Scope**: 1 core enum value + 1 formatter branch + lenient parser + 1 `SessionConfig` field + precision threaded (non-defaulted) through 2 helpers and ~25 call sites + unit witnesses (emit nanos/micros/millis-default; lenient parse 1–9 digits; reject malformed; round-trip ns; MaxLatency on ns; both-roles) + live interop cells. No FIXT version-gating (G4), no store/seqnum interaction.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | Catalogue row for nanosecond SendingTime (G3). Exact catalogue/coverage-index delta below (Polish). Normative refs: `[FIX-SL]` UTCTimestamp + `[FIX50SP2]` nanos. | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: emit nanos (27-char 52=) / micros / millis-default byte-identity; lenient parse of 1–9 fraction digits incl. non-standard widths; reject non-digit/empty/>9; ns round-trip lossless; MaxLatency on ns inbound; OrigSendingTime(122) preserved; both roles | ✅ planned |
| **VII.6** Interop | extend the live QFJ/QFcpp both-role matrix: fixpp emits nanos `52=` accepted by a live engine; fixpp accepts a live engine's nanos `52=` | ✅ planned |
| **VIII.5** Allocator | formatters are stack-buffer + noexcept; nanos adds digits, no heap; the default path adds zero cost. No-heap witness on the format/parse path | ✅ planned (witnessed) |
| **IX.1** Coverage | ≥95/85 on the new nanos formatter branch + lenient-parse branches + the config-threaded stamp helpers | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on fix_time + session send-path changes + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot change; +1 enum value + 1 `SessionConfig` field → source rebuild; default-millis preserves wire behavior | ✅ N/A / additive |
| **XI.4** Threading | the stamping runs on the existing session strand; no new concurrency surface, no callback | ✅ PASS |
| **XII.5** No-implicit-default | `sending_time_precision` defaults to `millis` explicitly (FIX 4.x parity), documented | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | no pluggable interface touched | ✅ N/A |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | core enum is in a mutex-free chrono header; `session_config.hpp` gains `#include <fixpp/core/fix_time.hpp>` (mutex-free) — verify the `session.hpp` awaitable closure is unaffected (`-L sync` / unfiltered Tier-1) | ✅ PASS (watch-item) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-06 (4 axes: parse leniency [asked], config shape, version-gating, 122 echo) engine-grounded | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan | ⚠ Gate A PENDING |

**Result**: PASS to proceed. The one wire-behavior change (outbound `52=` carries 6/9 sub-second digits when the precision knob is set) is in Complexity Tracking; it is opted-in default-millis, reuses the existing nanosecond-capable formatter, and is grounded against both live interop targets. No unjustified violations.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md`: the nanosecond-SendingTime catalogue row `backlog → done`, cite 026. Note: *"Per-session fix_time_precision (default millis) selects SendingTime(52) emit precision incl. nanos (27-char); lenient inbound parse of any 1–9 fraction digits; OrigSendingTime(122) preserved; default millis byte-identical."* Append B-026-1.
- `spec/coverage-index.md`: add the nanosecond-SendingTime row to the relevant UTCTimestamp / §4.x coverage entry; exact-set diff at Polish ([[feedback_completeness_gate_exact_set_not_subset]]).
- `spec/behaviors-and-limitations.md`: **B-026-1** (configurable SendingTime precision incl. nanos; lenient inbound parse; default millis no-op; resolution clock-bounded on non-ns platforms). **L-026-1** (achieved sub-second resolution is bounded by the platform `system_clock` period — full ns only where the clock provides it; FIXT/version-gating of sub-second precision deferred to G4).

## Project Structure

### Documentation (this feature)

```text
specs/026-nanosecond-sendingtime/
├── plan.md  ├── research.md  ├── data-model.md
├── contracts/sendingtime-precision.md  ├── quickstart.md
├── checklists/requirements.md  └── tasks.md (Phase 2 — NOT created here)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/core/
└── fix_time.hpp         # +1 enum value: fix_time_precision::nanos = 3;
                         #   update the buffer-size + accepted-length doc comments (27-char nanos).
src/core/
└── fix_time.cpp         # (1) utc_time_to_fix_string: add the nanos branch — emit the full 9-digit
                         #     ns_rem (no truncation); buffer needs 27. (2) fix_string_to_utc_time:
                         #     LENIENT parse — accept any sub-second width 1-9 (length 17-27), parse
                         #     the digits present and scale to ns; reject non-digit / empty fraction /
                         #     >9 digits (reuses wire_invalid_field_format).
include/fixpp/session/
├── session_config.hpp   # +1 additive field: fix_time_precision sending_time_precision = millis;
│                        #   + #include <fixpp/core/fix_time.hpp> (mutex-free; §XV.9 watch).
└── sending_time.hpp     # stamp_sending_time gains a fix_time_precision param (non-defaulted).
src/session/
├── sending_time.cpp     # stamp_sending_time uses the passed precision (was hardcoded millis).
└── session.cpp          # (1) the file-local stamp_sending_time(Clock&) at :1295 gains a
                         #     fix_time_precision param (NON-DEFAULTED -> compiler forces all sites).
                         #  (2) ~25 call sites (:570,:1420,:1695,:1844,:1929,:1976,:2017,:2061,:2089,
                         #     :2241,:2296,:2390,:2425,:2470,:2682,:2792,:3304,:3585,:3639,:3822,:3872,
                         #     ...) pass cfg_.sending_time_precision. (3) inbound MaxLatency/122/52
                         #     parse paths (:1826,:2054,:2084,:2772) already call fix_string_to_utc_time
                         #     -> accept ns automatically once the parser is lenient (no extra change).
tests/core/
└── fix_time_test.cpp (extend)   # nanos format/parse round-trip; lenient 1-9 widths; reject malformed.
tests/session/
└── test_sending_time_precision.cpp (NEW)  # config emits nanos/micros; default millis byte-identity;
                                           # inbound ns parsed; MaxLatency on ns; 122 preserved; no-heap.
tests/interop/   # extend 018 fixture: nanos SendingTime emit/accept cells (both roles)
phase-9-harness/ # parent: live cells (QFJ/QFcpp)
```

**Structure Decision**: A core time-layer extension (`fix_time_precision::nanos` + nanos formatter branch + lenient parser) plus one additive `SessionConfig` enum field threaded (non-defaulted) through the two `stamp_sending_time` helpers and all ~25 outbound call sites. The inbound parse path (`fix_string_to_utc_time`) is shared, so MaxLatency (`session.cpp:1826`), `OrigSendingTime(122)` (`:2054`), and `SendingTime(52)` (`:2084`) parse paths gain ns acceptance for free once the parser is lenient. No new modules / error slots / C surface.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| `utc_time_to_fix_string` nanos branch + `fix_string_to_utc_time` made lenient | FR-001/FR-004/FR-005 — emit 9-digit + accept any width | Changes a core, widely-used time function. Hazards: (1) the nanos branch must emit exactly 9 zero-padded digits from `ns_rem` with no overflow of the 27-char buffer; (2) the lenient parser must scale an N-digit fraction to nanoseconds correctly (e.g. 4 digits = ×10^5), reject empty/non-digit/>9, and keep the round-trip lossless; (3) must not regress the existing seconds/millis/micros paths. RED witnesses: round-trip at each width 1–9; reject `.`, `.abc`, `.1234567890`(10 digits); millis/micros byte-identity. |
| `sending_time_precision` threaded (non-defaulted) through 2 helpers + ~25 `session.cpp` call sites | FR-002/FR-003/FR-006 — per-session emit precision, every outbound message | The dominant stamping path is the file-local `stamp_sending_time(Clock&)` (`:1295`) used at ~23 sites; ALL must use the configured precision or some message types silently emit the wrong precision (the half-restructure / exhaustive-sweep class, [[feedback_half_restructure_symmetric_api]]). Mitigation: the precision param is **non-defaulted** -> the compiler errors on any un-updated site (compile-time exhaustiveness). RED witnesses: each admin/app message type emits at the configured precision; default millis byte-identity across the suite. |
| `SessionConfig` +1 enum field + `#include fix_time.hpp` | FR-002 — the config surface | Additive field; the new include into `session_config.hpp` (-> `session.hpp`) must not drag a mutex into the awaitable closure (§XV.9). `fix_time.hpp` is mutex-free chrono, so safe — but verify via `-L sync` / unfiltered Tier-1 ([[feedback_awaitable_header_mutex_include_edge]]). |

No 4th-project / repository-pattern / speculative-abstraction violations. All rows extend the existing nanosecond-capable time formatter + an additive config field threaded compile-time-exhaustively through the existing send path, behind a default-millis knob; the wire delta is grounded against both live interop targets and RED-witnessed.

## Gate A

- PENDING — runs after this plan, before `/speckit-tasks` ([const §XVII.1]). Reviews will land at `research/reviews/{codex,opus}_026-nanosecond-sendingtime_gate_a_*`.
