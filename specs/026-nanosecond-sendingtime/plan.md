# Implementation Plan: Nanosecond-resolution SendingTime / OrigSendingTime (G3 slice)

**Branch**: `026-nanosecond-sendingtime` | **Date**: 2026-06-06 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/026-nanosecond-sendingtime/spec.md`

## Summary

Add nanosecond-resolution `SendingTime(52)` / `OrigSendingTime(122)` support, selectable per session, defaulting to millisecond (FIX 4.x parity, byte-identical no-op). The core time layer already does almost all the work: `core::utc_time_to_fix_string` computes the sub-second remainder **in nanoseconds** (`src/core/fix_time.cpp:130 ns_rem`) and truncates to the requested precision, and `core::fix_string_to_utc_time` already composes the parsed fraction **as nanoseconds** (`:277-287`). `utc_time_point = std::chrono::time_point<std::chrono::system_clock>` is nanosecond-resolution on libstdc++ (all Linux Tier-1 profiles). So the change is bounded:

1. **Core**: extend `fix_time_precision` with `nanos` (9 digits), add **two** named formatter edits (resize `min_size[3]→{17,21,24,27}` to stop the `min_size[nanos]` OOB read at `fix_time.cpp:119`; add the `else if (prec == nanos)` arm at `fix_time.cpp:160-171` — without it nanos emits the 17-char seconds form; both UBSan-RED-witnessed — see data-model E2), and make the parser **lenient** — one grammar: bare length-17 OR `.`-at-index-17 + 1–9 digits (total 19–27), reject length-18 empty fraction / non-digit / >9-digit (the >9 case via an explicit width gate, NOT an int64 overflow trap).
2. **Session**: add one additive `SessionConfig` field `sending_time_precision` (the `fix_time_precision` enum, default `millis`) and thread it through **both** SendingTime-stamping helpers — the public `session::stamp_sending_time` (2-arg `(now, buf)` → 3-arg `(now, prec, buf)`, `prec` middle; 2 call sites) and the file-local `stamp_sending_time(Clock&)` (`session.cpp:1295`, 19 send sites). The precision parameter is **non-defaulted** so the compiler forces every call site to pass `cfg_.sending_time_precision` (turning the exhaustive-sweep requirement into a compile-time guarantee — no silent missed-site emitting the wrong precision). **Verified 21-site set** (19 file-local + 2 public) + 1 test caller — enumerated in data-model E5, mechanically checkable via the `rg` below.

**Key design grounding (source sweep + reference engines, in [research.md](./research.md)):**

- **Config = enum, default millis** (Clarifications). QFcpp uses `TimestampPrecision` (int 0–9); QFJ uses `TimeStampPrecision` (enum SECONDS/MILLIS/MICROS/NANOS, default MILLIS). fixpp reuses its existing `fix_time_precision` enum + `nanos` — matches QFJ, cleaner than a raw int, already the formatter's parameter type.
- **Lenient inbound parse** (Clarifications): one grammar — bare length-17 OR `.`-at-index-17 + 1–9 ASCII digits (total 19–27); reject length-18 (empty fraction) / non-digit / >9 (the >9 case via an explicit width gate, NOT an int64 overflow trap). Pad an N-digit fraction to nanoseconds (`×10^(9−N)`); matches QFcpp (`FieldConvertors.h` accepts the same widths) and is simpler than the current strict 17/21/24 check. Emit stays at the configured standard precision.
- **Version-gating**: QFcpp/QFJ gate sub-second to FIX4.2+/FIXT.1.1. fixpp is FIX.4.4 → sub-second always permitted; the FIXT/5.0SP2 gate defers to G4.
- **OrigSendingTime(122)**: preserved verbatim on resend (it is a copy of the original `52`); only newly-stamped `SendingTime(52)` uses the configured precision.
- **Default millis ⇒ pure no-op**: with the field unset, every `52=` is the 21-char millis form, byte-identical (FR-003/SC-002).

**Bounded change: +1 core enum value, 2 formatter edits (`min_size` resize + nanos arm) + lenient parser, +1 additive `SessionConfig` field, precision threaded (non-defaulted) through 2 stamping helpers and the verified 21 call sites (19 file-local + 2 public) + 1 test caller.** No new wire field, no new error slot (the formatter reuses `decimal_buffer_too_small`; the parser reuses `wire_invalid_field_format`), no codegen, no C-ABI surface. Orthogonal to the seqnum/persistence/handshake area.

## Technical Context

**Language/Version**: C++23 (Clang; chrono, `std::expected`) — [const §II]
**Primary Dependencies**: `core::{fix_time_precision, utc_time_to_fix_string, fix_string_to_utc_time}` (extended), `session::stamp_sending_time` + the file-local `stamp_sending_time(Clock&)` (precision-threaded), `SessionConfig` (+1 field), `core::Clock`. No new third-party deps.
**Storage**: none — this is a wire-format/precision change; no persistent state, no store interaction.
**Testing**: GoogleTest; sanitizers ASan/UBSan/TSan; coverage llvm-cov; no-heap (the formatters are stack-buffer/noexcept); the existing `fix_time` round-trip + property tests extended to nanos; live interop ctest cells (skip-without-counterparty). — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1, libstdc++ → ns-resolution system_clock); Windows/MSVC (Tier 2 — `system_clock` ~100 ns ticks, documented resolution ceiling)
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension
**Performance Goals**: N/A — the formatter is a fixed-cost stack format on the existing send path; nanos adds 3 more digits, no allocation, no new suspension
**Constraints**: `noexcept`/`expected_t`; no `std::mutex` in awaitable headers ([const §XV.9] — the only header touches are the core enum value (pure chrono header, no mutex) and one `SessionConfig` enum field; adding `#include <fixpp/core/fix_time.hpp>` to `session_config.hpp` pulls a mutex-free chrono header — verify it does not perturb the `session.hpp` awaitable closure via `-L sync`); the default-millis path must be byte-identical (FR-003)
**Scale/Scope**: 1 core enum value + 2 formatter edits (`min_size` resize + nanos arm) + lenient parser + 1 `SessionConfig` field + precision threaded (non-defaulted) through 2 helpers and the verified 21 call sites (19 file-local + 2 public) + 1 test caller + unit witnesses (emit nanos/micros/millis-default; lenient parse 1–9 digits; reject malformed; round-trip ns; MaxLatency on ns; both-roles) + live interop cells. **FR-007/SC-004 (MaxLatency) require NO MaxLatency logic change** — the existing `check_sending_time` arithmetic already operates on the parsed `system_clock::time_point`; the only delta is that the ns input now parses (no new boundary code). No FIXT version-gating (G4), no store/seqnum interaction.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **NEW catalogue row `S-039`** (configurable SendingTime emit precision incl. nanos + lenient parse) `backlog → done` — distinct from `S-019` (shipped millis MaxLatency slice via 005). Exact catalogue/coverage-index delta below (Polish). Normative refs: `[FIX50SP2 §3.3] Field data types` (UTCTimestamp datatype / sub-second precision, ns expectation; version-gating → G4) + `[FIX-SL §4.2.3] Validation of SendingTime(52)` (MaxLatency, S-019's section) + `[FIX-SL §4.8.4] Possible duplicates (PossDupFlag semantics)` (OrigSendingTime(122) preserved). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: emit nanos (27-char 52=) / micros / millis-default byte-identity; lenient parse of 1–9 fraction digits incl. non-standard widths; reject non-digit/empty/>9; ns round-trip lossless; MaxLatency on ns inbound; OrigSendingTime(122) preserved; both roles | ✅ planned |
| **VII.6** Interop | extend the live QFJ/QFcpp both-role matrix: fixpp emits nanos `52=` accepted by a live engine; fixpp accepts a live engine's nanos `52=` | ✅ planned |
| **VIII.5** Allocator | formatters are stack-buffer + noexcept; nanos adds digits, no heap; the default path adds zero cost. No-heap witness on the format/parse path | ✅ planned (witnessed) |
| **IX.1** Coverage | ≥95/85 on the new nanos formatter branch + lenient-parse branches + the config-threaded stamp helpers | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on fix_time + session send-path changes + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot change. **Public-header source break (named, not "additive"):** the public `session::stamp_sending_time` decl changes 2-arg `(now, buf)` → 3-arg `(now, fix_time_precision prec, buf)` — every external caller of this public helper recompiles (source rebuild, no C-ABI). Plus +1 enum value + 1 `SessionConfig` field. Default-millis preserves wire behavior. | ✅ source rebuild (public sig change named) |
| **XI.4** Threading | the stamping runs on the existing session strand; no new concurrency surface, no callback | ✅ PASS |
| **XII.5** No-implicit-default | `sending_time_precision` defaults to `millis` explicitly (FIX 4.x parity), documented | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | no pluggable interface touched | ✅ N/A |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | core enum is in a mutex-free chrono header; `session_config.hpp` gains `#include <fixpp/core/fix_time.hpp>` (mutex-free) — verify the `session.hpp` awaitable closure is unaffected (`-L sync` / unfiltered Tier-1) | ✅ PASS (watch-item) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-06 (4 axes: parse leniency [asked], config shape, version-gating, 122 echo) engine-grounded | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan | ⚠ Gate A PENDING |

**Result**: PASS to proceed. The one wire-behavior change (outbound `52=` carries 6/9 sub-second digits when the precision knob is set) is in Complexity Tracking; it is opted-in default-millis, reuses the existing nanosecond-capable formatter, and is grounded against both live interop targets. No unjustified violations.

**Exact §VI delta (applied at Polish):**
- **Catalogue decision: ADD a NEW row, not amend S-019.** S-019 (`feature-catalogue.md:39`, `done`) is a distinct shipped slice — millis MaxLatency / SendingTime(52) *validation* via 005 (`[FIX-SL §4.2.3]`); there is **no** existing nanos/precision row to flip (the prior plan's "`backlog → done`" was false). 026 ships a separate capability (configurable emit *precision* incl. nanos + lenient parse), so a new row is cleaner and keeps S-019's provenance intact.
- `spec/feature-catalogue.md`: **add `S-039`** | OFFICIAL | session | *"Configurable SendingTime(52) emit precision incl. nanoseconds (27-char) + lenient inbound UTCTimestamp parse"* | `4.2–5.0SP2, FIXT.1.1` | `[FIX50SP2 §3.3] Field data types` (UTCTimestamp datatype / sub-second precision) + `[FIX-SL §4.2.3] Validation of SendingTime(52)` (MaxLatency) | status **`backlog → done`** | `/specify` `026-nanosecond-sendingtime` | PR/squash (pending merge) | Tests `tests/core/fix_time_test.cpp` (nanos format/round-trip; lenient 1–9 widths; reject malformed) + `tests/session/test_sending_time_precision.cpp` (emit nanos/micros; default-millis byte-identity; inbound ns; MaxLatency; 122-preserved; no-heap) | Note: *"Per-session `fix_time_precision` (default millis) selects SendingTime(52) emit precision incl. nanos (27-char); lenient inbound parse — bare-17 OR `.`+1–9 digits (19–27), reject length-18/non-digit/>9 via width gate; OrigSendingTime(122) preserved verbatim; default millis byte-identical. Distinct from S-019 (millis MaxLatency validation, 005)."* Append B-026-1.
- `spec/coverage-index.md`: amend the `§4.2.3` row (`:40`) coverage cell `S-019` → `S-019, S-039` (note: S-039 adds emit-precision + lenient-parse over S-019's millis validation); exact-set diff at Polish ([[feedback_completeness_gate_exact_set_not_subset]]).
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
                         #   CORRECT the rotted length doc-comments in the SAME edit (do not append
                         #   the nanos line to a wrong table): enum comments :43-45 say 15/19/23 →
                         #   fix to 17/21/24 + add nanos 27; format-buffer doc :55 micros 25 → 24
                         #   (+ nanos 27); accepted-length doc :73-76 micros 25 → 24 (+ lenient 19-27).
src/core/
└── fix_time.cpp         # (1) utc_time_to_fix_string: (a) resize min_size[3]={17,21,24} at :119 to
                         #     min_size[4]={17,21,24,27} — else min_size[nanos=3] is an OOB read
                         #     (UBSan array-bounds); (b) add `else if (prec == nanos)` arm at :160-171
                         #     emitting '.' + 9-digit ns_rem — else nanos falls through to 17-char
                         #     seconds. RED witness: format(t,nanos) span length == 27 under UBSan.
                         #  (2) fix_string_to_utc_time: LENIENT parse — one grammar: bare length-17
                         #     OR '.'@idx17 + 1-9 digits (19-27); scale N digits to ns by 10^(9-N);
                         #     reject length-18 empty fraction / non-digit / >9 (>9 via WIDTH GATE
                         #     before any digit-parse, NOT int64 overflow) (reuses wire_invalid_field_format).
include/fixpp/session/
├── session_config.hpp   # +1 additive field: fix_time_precision sending_time_precision = millis;
│                        #   + #include <fixpp/core/fix_time.hpp> (mutex-free; §XV.9 watch).
└── sending_time.hpp     # stamp_sending_time 2-arg (now,buf) -> 3-arg (now, prec, buf) [prec MIDDLE,
                         #   non-defaulted]; correct the doc comment :37-39 ("millis … ≥19 bytes" ->
                         #   caller-supplied precision, ≥27 for nanos).
src/session/
├── sending_time.cpp     # stamp_sending_time uses the passed precision (was hardcoded millis at :31).
└── session.cpp          # (1) the file-local stamp_sending_time(Clock&) at :1295 gains a
                         #     fix_time_precision param (NON-DEFAULTED -> compiler forces all sites).
                         #  (2) VERIFIED 21-site set passes cfg_.sending_time_precision:
                         #     - 2 public callers: :570, :1695
                         #     - 19 file-local callers: :1420,:1844,:1929,:1976,:2017,:2061,:2089,
                         #       :2241,:2296,:2390,:2425,:2470,:2682,:2792,:3304,:3585,:3639,:3822,:3872
                         #     (:1295 = definition, sending_time.cpp:31 = in-body call — not caller sites.)
                         #     Verify: rg -n "stamp_sending_time\(" src/session/*.cpp \
                         #       include/fixpp/session/sending_time.hpp tests/session/*.cpp
                         #     Test caller broken by the non-defaulted param: logon_handshake_test.cpp:667.
                         #  (3) inbound MaxLatency/122/52/LogonSent parse paths (:1826,:2054,:2084,:2772)
                         #     already call fix_string_to_utc_time -> accept ns automatically once the
                         #     parser is lenient (no extra change; no MaxLatency logic change — FR-007).
tests/core/
└── fix_time_test.cpp (extend)   # nanos format/parse round-trip; lenient 1-9 widths; reject malformed.
tests/session/
└── test_sending_time_precision.cpp (NEW)  # config emits nanos/micros; default millis byte-identity;
                                           # inbound ns parsed; MaxLatency on ns; 122 preserved; no-heap.
tests/interop/   # extend 018 fixture: nanos SendingTime emit/accept cells (both roles)
phase-9-harness/ # parent: live cells (QFJ/QFcpp)
```

**Structure Decision**: A core time-layer extension (`fix_time_precision::nanos` + the two formatter edits `min_size` resize + nanos arm + lenient parser) plus one additive `SessionConfig` enum field threaded (non-defaulted) through the two `stamp_sending_time` helpers and all verified 21 outbound call sites (19 file-local + 2 public). The inbound parse path (`fix_string_to_utc_time`) is shared, so MaxLatency (`session.cpp:1826`), `OrigSendingTime(122)` (`:2054`), and `SendingTime(52)` (`:2084`) parse paths gain ns acceptance for free once the parser is lenient — with no MaxLatency logic change (FR-007). No new modules / error slots / C surface.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| `utc_time_to_fix_string` (2 edits: `min_size` resize + nanos arm) + `fix_string_to_utc_time` made lenient | FR-001/FR-004/FR-005 — emit 9-digit + accept any width | Changes a core, widely-used time function. Hazards: (0) **`min_size[3]` is OOB for nanos=3 and there is NO nanos format arm** — both must be edited or nanos is UB + silently 17-char (see data-model E2; UBSan-RED-witnessed: `format(t,nanos)` span length == 27 under `-fsanitize=undefined,bounds`); (1) the nanos arm must emit exactly 9 zero-padded digits from `ns_rem` with no overflow of the 27-char buffer; (2) the lenient parser must scale an N-digit fraction to nanoseconds correctly (e.g. 4 digits = ×10^5), reject length-18 empty / non-digit / >9 (the >9 via a **width gate** before any parse, NOT int64 overflow — 9,999,999,999 fits in int64), and keep the round-trip lossless; (3) must not regress the existing seconds/millis/micros paths. RED witnesses: round-trip at each width 1–9 (`parse(format(t,P)) == time_point_cast<period(P)>(t)`); reject `.`, `.abc`, `.1234567890`(10 digits, width-gate); millis/micros byte-identity. |
| `sending_time_precision` threaded (non-defaulted) through 2 helpers + the verified 21 `session.cpp` call sites + 1 test caller | FR-002/FR-003/FR-006 — per-session emit precision, every outbound message | The dominant stamping path is the file-local `stamp_sending_time(Clock&)` (`:1295`) used at 19 sites (+2 public); ALL must use the configured precision or some message types silently emit the wrong precision (the half-restructure / exhaustive-sweep class, [[feedback_half_restructure_symmetric_api]]). Mitigation: the precision param is **non-defaulted** -> the compiler errors on any un-updated site (compile-time exhaustiveness, including the `logon_handshake_test.cpp:667` test caller). RED witnesses: each admin/app message type emits at the configured precision; default millis byte-identity across the suite. |
| `SessionConfig` +1 enum field + `#include fix_time.hpp` | FR-002 — the config surface | Additive field; the new include into `session_config.hpp` (-> `session.hpp`) must not drag a mutex into the awaitable closure (§XV.9). `fix_time.hpp` is mutex-free chrono, so safe — but verify via `-L sync` / unfiltered Tier-1 ([[feedback_awaitable_header_mutex_include_edge]]). |

No 4th-project / repository-pattern / speculative-abstraction violations. All rows extend the existing nanosecond-capable time formatter + an additive config field threaded compile-time-exhaustively through the existing send path, behind a default-millis knob; the wire delta is grounded against both live interop targets and RED-witnessed.

## Gate A

- PENDING — runs after this plan, before `/speckit-tasks` ([const §XVII.1]). Reviews will land at `research/reviews/{codex,opus}_026-nanosecond-sendingtime_gate_a_*`.
- Round 1 applied 2026-06-06: Codex P1=2 P2=2 P3=1; Opus post-judging P1=2 P2=5 P3=4; rewrite addresses root causes RC#1 (FR-006/122), RC#2 (21-site exact set), RC#3 (§VI new-row + Normative Refs), RC#4 (one grammar + doc-length correction), RC#5 (min_size resize + nanos branch, UBSan-witnessed), New-2 (public helper sig), New-3 (round-trip oracle). Reviews: research/reviews/codex_026-nanosecond-sendingtime_gate_a_review.md, research/reviews/opus_026-nanosecond-sendingtime_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-06: Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=1 P3=1; rewrite re-anchors UTCTimestamp to [FIX50SP2 §3.3] Field data types, drops the bare [FIX50SP2], splits the conflated S-039 Spec-ref, canonicalizes [FIX-SL §4.8.4] Possible duplicates title (Article VI §VI.2/§VI.5 exact-cite gate). Reviews: research/reviews/codex_026-nanosecond-sendingtime_gate_a_2_review.md, research/reviews/opus_026-nanosecond-sendingtime_gate_a_2_adversarial_review.md.
