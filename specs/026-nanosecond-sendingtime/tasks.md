---
description: "Task list for 026-nanosecond-sendingtime"
---

# Tasks: Nanosecond-resolution SendingTime / OrigSendingTime (S-039, G3 slice)

**Input**: Design documents from `specs/026-nanosecond-sendingtime/`
**Prerequisites**: plan.md, spec.md, research.md (D1–D6), data-model.md (E1–E6, I-NST-1..6), contracts/sendingtime-precision.md (C1–C7), quickstart.md
**Repository root** = the library submodule (`research/G19-fix-fpml-iso20022/library/`). All paths below are submodule-relative.

**Tests**: REQUIRED and RED-first — `[const §VII]` TDD is binding for this codebase. Every behavior lands as a failing GoogleTest witness before the production change.

**Scope reality** (from plan.md Complexity Tracking + research D1–D6):
- The core already computes the sub-second remainder in nanoseconds (`src/core/fix_time.cpp:130 ns_rem`) and truncates to millis/micros; `utc_time_point = time_point<system_clock>` is ns-resolution on libstdc++ (all Linux Tier-1 profiles). The change is bounded: **+1 enum value, two formatter edits, a lenient parser, +1 additive `SessionConfig` field threaded non-defaulted through 2 helpers + the verified 21 call sites.**
- **Default `millis` ⇒ pure no-op** — every outbound `52=` stays the 21-char millis form, byte-identical (FR-003/SC-002). nanos/micros are opt-in.
- **No new wire field / error slot / codegen / C-ABI** (formatter reuses `decimal_buffer_too_small`; parser reuses `wire_invalid_field_format`). New catalogue row **S-039**.

**Gate A convergence notes baked into the tasks** (do not regress):
- (RC#5) The formatter needs **two** named edits, one of which is UB today: **(a)** resize `min_size[3]={17,21,24}` (`fix_time.cpp:119`) → `{17,21,24,27}` — `min_size[static_cast<uint8_t>(nanos=3)]` is an out-of-bounds read (UBSan `array-bounds`); **(b)** add the missing `else if (prec == nanos)` arm (`fix_time.cpp:160-171`) — without it nanos falls through to the 17-char **seconds** form. The RED witness MUST assert `format(t, nanos)` length **== 27** (not 17) **under UBSan**.
- (RC#1) `OrigSendingTime(122)` is **preserved verbatim** on resend — `build_replay_frame` byte-copies the stored `52` into `122` (`session.cpp:~1354`); it is **never** re-stamped at the configured precision. The configured precision applies to **newly-stamped `SendingTime(52)` only** (FR-006/I-NST-4). A witness must guard this.
- (RC#4) **One parser grammar everywhere**: accept bare length-17 (no dot) OR dot-at-index-17 + 1–9 ASCII digits (total 19–27); reject length-18 (empty fraction `…SS.`), a dot elsewhere, any non-digit fraction char, and **>9 digits via an explicit width/length gate** — NOT an int64 overflow (a 10-digit value fits in int64 and won't trap). Scale an N-digit fraction to ns by `10^(9−N)`.
- (RC#2) The precision param is **non-defaulted** → the compiler errors on any un-updated call site (compile-time exhaustiveness, I-NST-6). The verified set is **21 sites = 19 file-local + 2 public**, plus **1 test caller** (`tests/session/logon_handshake_test.cpp:667`).
- (New-2) The public `session::stamp_sending_time` is **2-arg today** (`(utc_time_point, std::span<char>)`); the edit inserts `prec` as the **middle** param (public-header source-break, no C-ABI).
- (New-3) Round-trip oracle is `parse(format(t,P)) == time_point_cast<period_for(P)>(t)` **uniformly** (incl. nanos) — never a bare `== t` (a coarse-`system_clock` cross-platform flake).
- (New-4) FR-007/SC-004 require **no MaxLatency logic change** — `check_sending_time` already operates on the parsed `system_clock::time_point`; the only delta is that ns input now parses.
- (New-5) §XV.9 watch-item verified **safe** in Gate A — `fix_time.hpp` is mutex-free chrono; the new `#include` into `session_config.hpp` drags no mutex into the `session.hpp` awaitable closure. Confirm via `-L sync` / unfiltered Tier-1 at verify; do not re-litigate.

**Build-graph reality** (verified against the tree; plan.md + quickstart.md reconciled to this during the `/speckit-analyze` remediation):
- The fix_time **formatter/parser** witnesses extend `tests/session/fix_time_roundtrip_test.cpp` (target `session_fix_time_roundtrip`, registered via `add_threading_test`) — NOT a `tests/core/` file (`fixpp_core_tests` is decimal-only). It already has a `roundtrip(tp, prec)` helper + per-precision `TEST(FixTimeRoundtrip, …)` cases.
- The **session** config/emit/inbound witnesses go in a NEW `tests/session/test_sending_time_precision.cpp` (new target `session_sending_time_precision`).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Build-graph registration for the new session unit suite.

- [X] T001 Register a new GoogleTest executable in `tests/session/CMakeLists.txt`, mirroring the 024 `session_reset_on_lifecycle` pattern: `session_sending_time_precision` (source `test_sending_time_precision.cpp`), linking `fixpp_session` + `fixpp_mock_clock` + `$<TARGET_OBJECTS:session_test_support>` + `GTest::gtest`/`gtest_main`, including `${CMAKE_SOURCE_DIR}/tests`, compiling `FIXPP_TEST_HOOKS`, setting the `TSAN_OPTIONS` env + asio suppression, tagged `LABELS "026;s039;sending-time-precision"`. Create an empty placeholder `tests/session/test_sending_time_precision.cpp` so the build graph configures. The core formatter/parser witnesses extend the EXISTING `tests/session/fix_time_roundtrip_test.cpp` (target `session_fix_time_roundtrip`) — no new core target. → verify: `cmake` configures clean; both `session_sending_time_precision` (empty) and `session_fix_time_roundtrip` build.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The additive enum value + config field both user stories reference. Additive surface only — no formatter/parser/threading behavior yet, so the US1/US2 witnesses are RED on *behavior*, not compile errors.

- [X] T002 Add `nanos = 3` to the `fix_time_precision` enum in `include/fixpp/core/fix_time.hpp`, AND in the SAME pass correct the rotted length doc-comments to the real values **seconds=17 / millis=21 / micros=24 / nanos=27** — fix the enum comments (currently wrong: 15/19/23), the format buffer doc (micros currently says 25 → 24; add nanos=27), and the accepted-length doc (micros 25 → 24; add nanos 27 once lenient parse lands). No code-path change. (data-model E1; Gate A RC#4/RC#5 doc-length finding) → verify: library builds; `nanos == 3`; existing seconds/millis/micros values unchanged; the doc table reads 17/21/24/27 consistently and no longer disagrees with the parser.
- [X] T003 Add the additive field `fix_time_precision sending_time_precision = fix_time_precision::millis;` to `include/fixpp/session/session_config.hpp` (near the other emit/format-related config), with the data-model E4 doc-comment (default millis = FIX 4.x parity = byte-identical no-op; no-implicit-default `[const §XII.5]`; source-rebuild note), plus `#include <fixpp/core/fix_time.hpp>` (mutex-free chrono — §XV.9 verified safe, Gate A New-5). No threading yet. (data-model E4; contract C4) → verify: library builds; the field is a public default-`millis` member; no `cfg_loader` change; `-L sync` / unfiltered Tier-1 still green (no mutex pulled into the `session.hpp` awaitable closure).

**Checkpoint**: Foundation ready — both user stories can proceed.

---

## Phase 3: User Story 1 — Emit nanosecond-precision SendingTime (Priority: P1) 🎯 MVP

**Goal**: With `sending_time_precision = nanos`, every outbound `SendingTime(52)` is the 27-char `YYYYMMDD-HH:MM:SS.sssssssss` form sourced from the session clock; `micros` → 24-char; the **default millis is byte-identical** to today. The precision threads compile-time-exhaustively through both stamp helpers + all 21 call sites.

**Independent Test**: Configure `sending_time_precision = nanos`, emit a message, assert the `52=` field is 27 chars whose ns digits round-trip losslessly; default config → 21-char millis, byte-identical to the pre-feature golden.

### Tests for User Story 1 (RED-first)

> Core witnesses extend `tests/session/fix_time_roundtrip_test.cpp`; session witnesses go in `tests/session/test_sending_time_precision.cpp` (same-file authoring ⇒ sequential within each file).

- [X] T004 [US1] Write the **core formatter** witnesses in `tests/session/fix_time_roundtrip_test.cpp` (extend, reuse the `roundtrip()` helper — **but first add `case fix_time_precision::nanos: return time_point_cast<nanoseconds>(t);` to the `trunc()` switch (`:59-67`)**, which currently has only seconds/millis/micros arms and **falls through to `return t;` (bare equality)** for nanos; without the arm the round-trip witness (2) would pass for the wrong reason on a ns-resolution `system_clock` — the exact New-3 flake): (1) **`FixTimeNanos_Format_Is27CharsUnderUBSan`** — `utc_time_to_fix_string(t, nanos, buf)` returns a span of length **== 27** (assert `== 27`, explicitly NOT 17 — this single assertion guards BOTH the missing-`else if` 17-char fall-through AND the `min_size[3]` OOB; the target MUST be exercised under UBSan so the OOB read fails RED); the 9 fraction digits equal the sub-second ns of `t` (contract C2; data-model E2 + RED witness; **SC-001**); (2) **`FixTimeNanos_RoundTrip_Lossless`** — `parse(format(t, nanos)) == time_point_cast<nanoseconds>(t)` (NOT bare `== t`, New-3; **FR-005**); (3) **`FixTime_MillisMicros_ByteIdentical_NoRegression`** — `format(t, millis)` / `format(t, micros)` byte-identical to the pre-feature output; the seconds path unchanged (**FR-003/SC-002**). (quickstart 1,2,5)
- [X] T005 [US1] Write the **session emit** witnesses in `tests/session/test_sending_time_precision.cpp`: (4) **`SendingTimePrecision_Nanos_Emits27Char52`** — a session with `sending_time_precision = nanos` → every outbound admin/app message's `52=` field is 27 chars, ns-sourced from the clock (contract C5; **SC-001**); (5) **`SendingTimePrecision_Micros_Emits24Char52`** — `micros` → 24-char `52=` (spec AC US1.2); (6) **`SendingTimePrecision_DefaultMillis_ByteIdentical`** — default config (unset) → `52=` is the 21-char millis form, byte-identical to the pre-feature golden, across multiple message types (**FR-003/SC-002**, the zero-regression gate). Reuse the 021/022/024 `session_test_support` fixtures + `extract_field`/`peek_outbound`.
- [X] T006 [US1] Build + run `session_fix_time_roundtrip` + `session_sending_time_precision`; **confirm RED** on *behavior*: the nanos format witness fails because nanos currently emits 17 chars (and trips the `min_size[3]` OOB under UBSan); the session emit witnesses fail because every site still hardcodes millis. The millis/micros byte-identity witness (T004(3)) and the default-millis session witness (T005(6)) should already PASS (they characterize the unchanged paths). → verify: failures are behavioral/UBSan, not compile errors — assert the specific RED signals (format length 17 not 27; `52=` millis regardless of config); fix fixture wiring first if it fails to compile.

### Implementation for User Story 1

> Production sites: `src/core/fix_time.cpp` (formatter), `include/fixpp/session/sending_time.hpp` + `src/session/sending_time.cpp` (public helper), `src/session/session.cpp` (file-local helper `:1295` + the 21 call sites).

- [X] T007 [US1] Implement the **two formatter edits** in `src/core/fix_time.cpp` `utc_time_to_fix_string`: (a) resize `constexpr std::size_t min_size[3] = {17,21,24};` at `:119` → `min_size[4] = {17,21,24,27};` (or a `switch`/computed size covering nanos) so `min_size[static_cast<uint8_t>(nanos=3)]` is no longer an OOB read; (b) add an explicit `else if (prec == fix_time_precision::nanos)` arm in the format if/else (`:160-171`) writing the full 9-digit zero-padded `ns_rem.count()` after the `.`, total length `17+1+9 = 27`. Do not perturb the seconds/millis/micros arms. (data-model E2; contract C2; Gate A RC#5) → verify: T004 (1)(2) GREEN **under UBSan** (27-char, no `array-bounds` report); T004 (3) millis/micros still byte-identical.
- [X] T008 [US1] Change the **public** helper signature in `include/fixpp/session/sending_time.hpp` + `src/session/sending_time.cpp`: `stamp_sending_time(utc_time_point now, std::span<char> buf)` → `stamp_sending_time(utc_time_point now, fix_time_precision prec, std::span<char> buf)` (prec **NON-DEFAULTED**, middle param); the body uses `prec` (was hardcoded `millis` at `sending_time.cpp:31`); correct the helper's doc-comment (`sending_time.hpp:37-39`: "millis … ≥19 bytes" → "configured precision … ≥27 bytes for nanos"). Update the **2 public call sites** (`session.cpp:570`, `:1695`) to pass `cfg_.sending_time_precision`, and the **1 test caller** (`tests/session/logon_handshake_test.cpp:667`) to pass an explicit precision. (contract C5; data-model E5 / Gate A New-2/RC#2) → verify: library + tests build; the public helper takes 3 args; both prod callers + the test caller pass a precision.
- [X] T009 [US1] Change the **file-local** helper at `src/session/session.cpp:1295` `stamp_sending_time(fixpp::core::Clock& clock)` → `stamp_sending_time(fixpp::core::Clock& clock, fixpp::core::fix_time_precision prec)` (prec **NON-DEFAULTED**, uses `prec` instead of the hardcoded `millis`), and update **all 19 file-local call sites** to pass `cfg_.sending_time_precision`. The non-defaulted param makes the compiler error on any missed site (compile-time exhaustiveness, I-NST-6). Mechanically verify the full set with `rg -n "stamp_sending_time\(" src/session/*.cpp include/fixpp/session/sending_time.hpp tests/session/*.cpp` (expect 19 file-local + 2 public + 1 test caller, all updated; the `:1295` definition + the `sending_time.cpp:31` body are not call sites). (data-model E5 21-site set; research D4; Gate A RC#2) → verify: library builds (a build error would mean a missed site); T005 (4)(5) GREEN; T005 (6) default-millis byte-identical.
- [X] T010 [US1] Build + run the US1 subset GREEN; confirm the default-millis byte-identity witness (T005(6)) + the core millis/micros byte-identity witness (T004(3)) stay GREEN and no existing session/wire/time test regressed — the **SC-002** all-default byte/semantics-identity gate. → verify: US1 witnesses green; `ctest -L "session|wire"` + `session_fix_time_roundtrip` no regression.

**Checkpoint**: US1 complete — emit nanos/micros 52= at the configured precision (all 21 sites), default millis byte-identical. MVP delivered.

---

## Phase 4: User Story 2 — Accept inbound nanosecond-precision timestamps (Priority: P1)

**Goal**: The inbound parser leniently accepts any 1–9-digit sub-second fraction (`SendingTime(52)` and `OrigSendingTime(122)`), regardless of the local emit precision, so a peer's nanos (or any non-standard-width) timestamp is parsed, not rejected; MaxLatency operates correctly on the ns instant; `OrigSendingTime(122)` is preserved verbatim on resend.

**Independent Test**: Feed an inbound 27-char nanos `52=` → parses (no Reject), the instant matches to ns; a 4/5/7/8-digit fraction also parses; `.`/`.12a`/`.1234567890` reject; MaxLatency at the boundary decides correctly; a PossDup resend's `122` echoes the original instant (not re-stamped).

### Tests for User Story 2 (RED-first)

> Core lenient-parse witnesses extend `tests/session/fix_time_roundtrip_test.cpp`; session inbound witnesses go in `tests/session/test_sending_time_precision.cpp`.

- [X] T011 [US2] Write the **core lenient-parse** witnesses in `tests/session/fix_time_roundtrip_test.cpp`: (1) **`FixTimeParse_LenientWidths_1to9`** — `parse("YYYYMMDD-HH:MM:SS.D…")` for each fraction width N = 1..9 yields the ns-scaled instant (e.g. `.1234` → +123 400 000 ns, i.e. `×10^(9−N)`); the bare 17-char (no dot) form still parses (**FR-004/SC-003**); (2) **`FixTimeParse_RejectMalformed`** — `…SS.` (empty fraction, length-18), `…SS.12a` (non-digit), `…SS.1234567890` (10 digits) ALL → `wire_invalid_field_format`; assert the 10-digit case is rejected by the **width gate** (length/width ≤ 27 / N ≤ 9), not by arithmetic (a 10-digit value fits in int64) (**FR-008/SC-005**; Gate A RC#4); (3) **`FixTimeParse_Nanos27_RoundTrip`** — the 27-char nanos form parses to `time_point_cast<nanoseconds>(t)`. (quickstart 3,4; contract C3; data-model E3)
- [X] T012 [US2] Write the **session inbound** witnesses in `tests/session/test_sending_time_precision.cpp`: (4) **`InboundNanos52_ParsedNotRejected`** — feed an inbound message whose `52=` is a 27-char nanos timestamp → parsed + processed, NO malformed-field Reject (**SC-003**); (5) **`MaxLatency_OnNanosInbound_BoundaryCorrect`** — an inbound ns `52=` at the MaxLatency boundary → correct accept/reject; assert this needs **no new boundary logic** — the existing `check_sending_time` arithmetic runs on the parsed instant (**FR-007/SC-004**; Gate A New-4); (6) **`OrigSendingTime122_PreservedVerbatim_OnResend`** — on a PossDup resend of a message originally stamped at millis, the resent `OrigSendingTime(122)` echoes the **stored original** `52` bytes/instant and is NOT re-stamped at the (possibly nanos) configured precision (**FR-006/I-NST-4**; Gate A RC#1 — guards `build_replay_frame` at `session.cpp:~1354`). (quickstart 8,9,10; contract C3/C7)
- [X] T013 [US2] Build + run; **confirm RED** for the lenient-parse + inbound-ns witnesses (the strict parser rejects length 27 and non-17/21/24 widths today). The 122-preserved witness (T012(6)) should already PASS (resend already byte-copies 122) — it is the **zero-regression guard** that the parser/threading changes must not break. → verify: behavioral RED (parse rejects ns input), not compile errors.

### Implementation for User Story 2

> Production site: `src/core/fix_time.cpp` `fix_string_to_utc_time` (`:181-185` length gate + `:277-287` ns compose). The session inbound paths (`session.cpp:1826` MaxLatency, `:2054` 122, `:2084` 52, `:2772` LogonSent) call the shared parser — they gain ns acceptance for free, no per-site change.

- [X] T014 [US2] Implement the **lenient parser** in `src/core/fix_time.cpp` `fix_string_to_utc_time`: replace the strict `len != 17 && len != 21 && len != 24` gate (`:183-184`) with the one grammar — accept bare length-17 (no dot) OR a dot at index 17 followed by N digits with **1 ≤ N ≤ 9** (total length 19–27); reject length-18 (empty fraction), a `.` anywhere other than index 17, any non-digit fraction char, and **N > 9 via an explicit width/length gate evaluated BEFORE any `parse_digits`** (a 10-or-more-digit fraction makes total length ≥ 28; reject on **total length > 27** — equivalently fraction width > 9 — NOT a literal `== 28` check, which would miss an 11-digit length-29 input; and reject on length/width, not on overflow — a 10-digit value fits in int64). Scale the N-digit fraction to nanoseconds by `10^(9−N)` via the existing `ns_sub` compose (`:277-287`). Do not change the existing seconds/millis/micros accept behavior (17/21/24 remain valid as the N∈{0,3,6} cases). (data-model E3; contract C3; Gate A RC#4) → verify: T011 GREEN (all widths 1–9 + the three reject cases + nanos round-trip); existing seconds/millis/micros parse byte/semantics-identical.
- [X] T015 [US2] Confirm the inbound session paths accept ns **for free** and 122 is never re-stamped: source-read `session.cpp:1826/2054/2084/2772` to confirm each threads the parsed `system_clock::time_point` unchanged (no MaxLatency logic change — FR-007), and `build_replay_frame` (`:~1341-1358`) still byte-copies the stored `52` into `122` (no re-format). No production edit expected here — this task is the verification that T014 alone satisfies US2. (Gate A RC#1/New-4) → verify: T012 (4)(5)(6) GREEN; T012(6) (122 preserved) GREEN proving no resend regression.
- [X] T016 [US2] Build + run the full 026 suite GREEN; confirm no regression across `core|session|wire` labels and that the millis-default byte-identity (T005(6)) + 122-preserved (T012(6)) zero-regression guards stay GREEN. → verify: full suite green; `ctest -L "session|wire"` + `session_fix_time_roundtrip` no regression.

**Checkpoint**: US1 + US2 both functional; S-039 behaviors delivered — emit + accept nanos, default millis byte-identical, 122 preserved.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: No-heap proof, live interop (both roles), catalogue/coverage/B&L close-out (applied at Polish per the 020/021/022/024 precedent), and the binding local verify mirror.

- [X] T017 [P] Author a **no-heap** witness `SendingTimePrecision_NoHeapOnFormatParse` in `tests/session/test_sending_time_precision.cpp` asserting zero global-heap allocation across `utc_time_to_fix_string` + `fix_string_to_utc_time` at **every** precision (seconds/millis/micros/nanos) and across the lenient-parse widths, verified under the **mallocnesia LD_PRELOAD** interceptor (the binding gate per [[feedback_tracking_pmr_resource_false_pass]] — not "covered by existing discipline"). (quickstart 11; plan §VIII.5; contract — I-NST-5)
- [X] T018 [P] Add the live **fixpp-emits-nanos** interop cell in **`tests/interop/happy/hp_fix44_nanos_sendingtime_test.cpp`** (NEW), registered in `tests/interop/CMakeLists.txt` via `fixpp_add_interop_test(NAME interop_026_nanos_sendingtime_test GROUP happy TLS SOURCES happy/hp_fix44_nanos_sendingtime_test.cpp LABEL "interop;026;interop-happy;polish;sending-time;nanos")` — mirroring the 024 `interop_024_reset_on_logon_test` precedent (both roles value-parameterized over counterparty × role in this ONE file). Skip-without-counterparty (FR-023 guard, CI always green). Emit cell: a fixpp session with `sending_time_precision = nanos` sends a 27-char `52=`; a live QFcpp/QFJ peer accepts it (no Reject). Parent `phase-9-harness/` runs the counterparty. Run under normal + ASan/UBSan/TSan; same skip guard as the 018/024 cells. (quickstart "Live interop"; contract C7.1 / SC-001)
- [X] T019 [P] Add the live **fixpp-accepts-nanos** cell to the **SAME** `tests/interop/happy/hp_fix44_nanos_sendingtime_test.cpp` (the accept-role parameterization of the T018 cell; same `interop_026_nanos_sendingtime_test` registration — per the 024 one-file-both-roles precedent), skip-without-counterparty: a live QFcpp/QFJ peer configured for NANOS sends a 27-char `52=`; fixpp parses + processes it and MaxLatency is computed correctly. Same sanitizer matrix + skip guard. (contract C7.2 / SC-003/SC-004)
- [X] T020 [P] Update `spec/feature-catalogue.md`: **add the new row S-039** | OFFICIAL | session | *"Configurable SendingTime(52) emit precision incl. nanoseconds (27-char) + lenient inbound UTCTimestamp parse"* | version span `4.2–5.0SP2, FIXT.1.1` | refs `[FIX50SP2 §3.3] Field data types` (UTCTimestamp datatype/sub-second precision) + `[FIX-SL §4.2.3] Validation of SendingTime(52)` | status **`backlog → done`**, cite 026 | with the completion note + the "distinct from S-019 (millis MaxLatency validation, 005)" gloss. Leave **S-019 untouched**. (plan §VI delta; FR + contract) Append B-026-1.
- [X] T021 [P] Update `spec/behaviors-and-limitations.md`: add **B-026-1** (per-session `fix_time_precision` selects SendingTime(52) emit precision incl. nanos (27-char); lenient inbound parse of any 1–9 fraction digits; OrigSendingTime(122) preserved verbatim; default millis byte-identical no-op) and **L-026-1** (achieved sub-second resolution is bounded by the platform `system_clock::period` — full ns only where the clock provides it, e.g. coarser on MSVC ~100 ns; the wire FORMAT is always 9 digits when nanos is selected; FIXT/version-gating of sub-second precision deferred to G4). (plan §VI delta)
- [X] T022 [P] Update `spec/coverage-index.md`: amend the `§4.2.3` coverage cell (`:40`) `S-019` → `S-019, S-039` (S-039 adds emit-precision + lenient-parse over S-019's millis validation), asserting an **exact-set** diff (the cell gains exactly S-039; nothing else changes) ([[feedback_completeness_gate_exact_set_not_subset]]). (plan §VI delta)
- [X] T023 Update `library/CLAUDE.md` active-feature pointer for 026 (status → IMPLEMENTED; next = `/simplify` → `/speckit-verify` → Gate B) per the merge-bookkeeping convention.
- [ ] T024 Run the full local Tier-1 verify mirror (`/speckit-verify`): ASan/**UBSan**/TSan on the formatter + parser + the 21 threading sites + the interop cells — **UBSan on the nanos format path is the binding gate** for the Gate A RC#5 `min_size[3]` OOB finding (the T004(1) witness must run under UBSan and be green post-fix); coverage ≥95/85 on the nanos formatter branch + the lenient-parse width branches + the config-threaded helpers; the §XV.9 watch-item (UNFILTERED Tier-1 or `-L sync`); confirm default-millis byte-identity. Produces `.specify/decisions/026-nanosecond-sendingtime-verify.md` — the required evidence for `/gate-b`. (plan Constitution Check IX.1/IX.2/XV.9; pipeline step 17)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (T001)**: no dependencies — start immediately.
- **Foundational (T002, T003)**: after Setup. T002 (enum + doc-lengths) + T003 (config field) block both user stories.
- **US1 (T004–T010)**: after T002+T003. Internal: T004 → T005 (RED tests) → T006 (confirm RED, UBSan) → **T007 → T008 → T009** (impl: formatter, then public helper, then file-local helper + 21 sites) → T010 (GREEN + no regression).
- **US2 (T011–T016)**: after T002 (needs `nanos`). Internal: T011 → T012 (RED tests) → T013 (confirm RED) → **T014** (lenient parser) → T015 (confirm inbound-paths/122 free) → T016 (GREEN + no regression). US2's parser change is independent of US1's formatter/threading — they touch disjoint code paths in the same `fix_time.cpp` file.
- **Polish (T017–T024)**: after US1 + US2. T017–T022 are `[P]` (distinct files/regions); T023 sequential; T024 verify runs last.

### User Story Independence

- **US1 (P1, emit)** = the formatter nanos branch + the config-threaded send path. **US2 (P1, accept)** = the lenient parser + inbound paths. Both are co-equal P1 for real interop. They share `fix_time.cpp` but touch disjoint functions (`utc_time_to_fix_string` vs `fix_string_to_utc_time`) and are independently testable. US1 is the MVP (emit is the feature's reason to exist) and ships/validates alone; US2 makes interop bidirectional.

### Within stories

- US1: tests RED before impl; **T007 (formatter) before T008/T009 (threading)** — the session emit witnesses need the formatter to produce 27 chars. T008 (public helper) and T009 (file-local + 19 sites) are sequential (both edit `session.cpp` call sites).
- US2: tests RED before impl; T014 (parser) is the single production change; T015 is verification-only.

### Parallel Opportunities

- US1 (T004–T010) and US2's parser core (T011–T016) touch disjoint functions and can largely interleave once T002 lands (US2 needs only the `nanos` enum value, not the formatter/threading).
- Polish tasks **T017, T018, T019, T020, T021, T022** are `[P]` against each other EXCEPT T018/T019, which share one value-parameterized interop fixture (`hp_fix44_nanos_sendingtime_test.cpp`, both roles — per the 024 precedent) and so are sequential w.r.t. each other. Distinct files: the no-heap cell (T017), the shared interop fixture (T018+T019), catalogue (T020), B&L (T021), coverage-index (T022).

---

## Parallel Example

```text
# After T001–T003 (setup + foundation):
US1 (emit):    T004 → T005 → T006 → T007 → T008 → T009 → T010
US2 (accept):  T011 → T012 → T013 → T014 → T015 → T016   (needs only T002)

# Polish in parallel once both stories land:
T017 | (T018 → T019) | T020 | T021 | T022   (T018/T019 share one interop fixture → sequential; the rest [P], distinct files)
```

---

## Implementation Strategy

### MVP First (User Story 1)

1. T001–T003 (setup + enum + config field) → US1 (emit nanos/micros, all 21 sites compile-time-exhaustive, default millis byte-identical). The outbound-precision core ships first — it is the feature's reason to exist.

### Incremental Delivery

1. Setup + foundation + US1 → emit nanos delivered (MVP), default millis byte-identical.
2. US2 → lenient inbound parse (any 1–9 width) + MaxLatency on ns + 122 preserved → interop bidirectional.
3. Polish → no-heap green, live interop cells green (both roles), catalogue/coverage/B&L updated (new row S-039 → done), `/speckit-verify` evidence (incl. the binding UBSan nanos-format gate) produced for Gate B.

---

## Notes

- `[P]` = different files, no dependencies. Same-file test authoring (T004/T011 in `fix_time_roundtrip_test.cpp`; T005/T012/T017 in `test_sending_time_precision.cpp`) and the same-region `session.cpp`/`fix_time.cpp` impl tasks are deliberately **not** `[P]`.
- `[Story]` label maps each task to US1 / US2; Setup / Foundational / Polish carry no story label.
- RED-first is binding (`[const §VII]`): verify witnesses fail on *behavior* before the production change. The **nanos-format-length witness (T004(1)) MUST run under UBSan** — it is the only thing that RED-proves the `min_size[3]` OOB and the 17-char fall-through that Gate A surfaced; do not let it pass for the wrong reason.
- The only production surface is the `nanos` enum value + the two formatter edits + the lenient parser + the additive `SessionConfig` field + the 2 stamp-helper signature changes (precision non-defaulted) + the 21 call sites — no new module, codegen, error slot, or C-ABI. The `OrigSendingTime(122)` resend path is untouched (byte-copy preserved).
- **Build-graph note** (reconciled during `/speckit-analyze`): the fix_time witnesses extend `tests/session/fix_time_roundtrip_test.cpp` (target `session_fix_time_roundtrip`); plan.md Project Structure + quickstart.md were corrected from the earlier `tests/core/fix_time_test.cpp` / `fixpp_core_tests` path (which does not exist — that target is decimal-only).
- Next pipeline steps after `/speckit-tasks`: `/speckit-analyze` → `/speckit-checklist` → `/speckit-checklist-audit` (MANDATORY gate before `/speckit-implement`) → `/speckit-implement` → `/simplify` → `/speckit-verify` → Gate B (per `.specify/pipeline.md`).
```
