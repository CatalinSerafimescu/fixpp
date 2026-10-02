# Implementation Plan: Inbound frames 092 leaves out

**Branch**: `093-inbound-frame-dispositions` | **Date**: 2026-10-02 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/093-inbound-frame-dispositions/spec.md` (clarified
2026-10-02, with the plan-research owner rulings recorded there).

**Issues**: fixpp#514, #515, #516, #523, #524 (batch B22). **Depends on**: fixpp#530 (B25) merging first.

## Summary

This bundle makes five changes to fixpp's inbound path, on one shared review surface: the read pump,
`Session::on_inbound_frame`'s arms, and the 141=Y reset unit.

1. **#514: garbled frames are disregarded, not fatal.** The Framer gains an opt-in resync that the read
   pump turns on. A frame that fails framing is skipped, and framing resumes at the next `8=` after an
   SOH, independent of how reads are segmented. A frame whose third field is not 35 is disregarded in
   every state and both validation modes. Each disregard is logged, evented and counted, and the count
   is readable through a new C-ABI getter. A new establishment timeout (`logon_timeout_ms`, 10 s, zero
   refused, with C-ABI, Python and TOML surfaces) bounds the pre-Active waits this creates. It lives in
   the pump and closes the transport on expiry.
2. **#515: every admitted frame parses.** One inbound limit L is set at `open()`: the advertised 383,
   between 4 KiB and 256 KiB, else 64 KiB. It sizes the Framer limit, the carry (L plus one read), a
   per-session parse buffer allocated once, and the offset-table entry cap. All are sized for the
   densest legal layout, 3 bytes per field. A frame over L is refused at framing, before its CheckSum,
   with a close.
3. **#516: liveness refreshes on every frame that is neither garbled nor faulty.** It happens at one
   point, right after the fault checks.
4. **#523: a closing session's NotConnected and LogonSent arms act on nothing.**
5. **#524: the 141=Y reset is one store operation.** `MessageStore::reset_to` is a new non-pure virtual,
   which `MemoryStore` and `FileStore` override atomically. The manager is set first, with no yield. A
   `close()` waits, bounded, for an in-flight unit before its teardown reset.

C-ABI: one MINOR bump, two symbols, declared **BREAKING** (`[const §X.7]`).

## Technical Context

**Language/Version**: C++23 (the project toolchain). No new dependency.

**Primary Dependencies**: standalone asio (coroutines, `parallel_group` for the deadline race), std::pmr.

**Storage**: `MessageStore` implementations: `MemoryStore`, and `FileStore`'s append-only log with
temp-file-and-rename resets.

**Testing**: GoogleTest through ctest; the mock clock for timer and liveness cells; `HookedStore` for
the close-during-suspension cells; the C-ABI and Python round-trips; fuzz harnesses (libFuzzer); the
paired bench.

**Target Platform**: Linux (clang, gcc, libc++, sanitizer lanes) and Windows MSVC (the debug and ASan
sandbox).

**Project Type**: a library with a C ABI and Python bindings.

**Performance Goals**: steady-state inbound throughput unchanged within `[const §VIII.2]`'s +5 % budget.
It is expected to improve slightly, since each frame drops one or two 16 KiB stack memsets.

**Constraints**:
- `[const §VIII.5]`: no heap between parse and callback, and no per-frame allocation.
- Per-session memory rises to about (L + 4 KiB) + B(L) ≈ 6·L, stated as a formula (owner ruling).

**Scale/Scope**: about 10 source files and about 15 test files. Research R-8 lists the cells that flip.

**Unknowns**: none open. Two design values are measured at implementation, not chosen here:
`kCallbackReadHeadroom` and `kContainerSlack` (data-model E-2).

## Constitution Check

*GATE: passed before Phase 0; re-checked below after Phase 1.*

| Article | Requirement | Status |
|---|---|---|
| VII §3–4 TDD | failing test first | Planned. Every quickstart §1 cell is RED-first on the base, except the named regression guards. The RED evidence for SC-004 is the ceiling measured in quickstart §0 |
| VII §5 conformance | the FIX-TC corpus | TC 2d/2m/2t/3b/3c/3e move from diverging to conforming, with cells. TC 2i is unchanged (FR-008), and its Logout gap belongs to #534 |
| VII §7 fuzz | parser-touching code needs a fuzz harness | `fuzz_wire_framer` gains a `resync_on_garble = true` arm asserting termination and at most one garble per feed. `fuzz_transport_read_path` covers the pump |
| VII §8 test grouping | new isolation-safe tests go into a grouped bucket, selected by a label | Planned (a `093` label). The timer and coroutine cells stay standalone |
| VIII §2 perf | paired merge-base A-B-A-B within +5 % | A manual paired run of `on_inbound_frame_bench`, with validation off and on, base path padded (quickstart §0, §3) |
| VIII §5 zero-alloc | no heap between parse and callback | Held for the parse: the buffer is allocated once at `open()`, and the resync scans in place. A spill witness proves there is no hidden heap use on any lane. **Not zero before Active.** The establishment deadline race (asio parallel group) allocates its shared state per pre-Active read. That is outside the parse-to-callback window and outside FR-052's steady state, which is Active, and the existing read-path alloc guard cannot see it. It is counted by a measurement (research R-9) and stated here with its scope. B15/#497's hot-path rule is checked against it at Gate A |
| IX sanitizers / coverage | per-line coverage assessment | The `/speckit-verify` matrix. The resync, deadline and reset_to branches are covered by the cells |
| X §4 append-only enums | `core::error` | No new error code. `SessionEvent` alternatives are appended |
| X §7 ABI | C-ABI changes versioned | **MINOR bump + BREAKING declaration** (FR-051, contract C-7). `gh release list --exclude-drafts` must be empty at implementation. The C++ additions are source-compatible; `MessageStore`'s vtable changes, which needs a rebuild |
| XI concurrency | strand discipline, cancellation | The deadline race uses `await_deadline`, whose re-arm handles a clock-wide sweep (#536). `close()`'s wait copies the existing liveness-counter poll. No new detached coroutine |
| XII security | fail closed | An oversize frame closes before its CheckSum is read. Garbled bytes are never acted on. A disregard before Active is bounded by the timeout, so a hostile peer cannot hold a connection open indefinitely |
| XIV §2 | at most 5 pure virtuals per pluggable interface | `MessageStore` keeps 4; `reset_to` is non-pure |
| XV banned patterns | — | No `thread_local` buffer (the owner rejected a per-thread arena). No RTTI dispatch for `reset_to` |
| XVI §3–4 | `/clarify`, `/analyze` mandatory | `/clarify` is done (2026-10-02). `/analyze` follows `/speckit-tasks` |
| XVI §6 | the orchestrator does not implement | Implementation goes to `phase-implementer`. Mutation proofs happen in a scratch copy |
| XVII §1 Gate A | parser, session FSM, public C++ API, C ABI, message-store contract | **Required before `/speckit-tasks`** |
| XVII §7 | a local build gate before the PR | Planned. Ask before each build, one preset at a time, under the 16 GiB cap |

**Result: PASS**, with the §X.7 BREAKING declaration as a sanctioned pre-release change.

## Orchestrator decisions (open to Gate A review: NOT owner rulings)

The owner's rulings are in spec.md Clarifications. Each item below is a design choice the orchestrator
made at `/speckit-plan`. Reviewers may challenge any of them, and the owner may overrule.

- **OD-1: the bound on `close()`'s wait for an in-flight reset unit is `logon_timeout_ms`.** The owner
  ruled "with timeout" and left the value open.
  - `logout_disconnect_timeout_ms` was the first candidate, and was rejected: nothing validates it, and
    TOML accepts 0, which would expire the wait at once.
  - `logon_timeout_ms` is never 0, and the unit is part of Logon processing.
  - On expiry `close()` proceeds and records `session_event_close_reset_wait_expired` (L-4).
- **OD-2: `open()` refuses an advertised MaxMessageSize below 4096**, the acceptor's first-frame byte
  budget. This is a new BREAKING refusal for C++ callers. Alternative: no floor, accepting that an L
  below a Logon's size makes the session unusable.
- **OD-3: FR-013 reverses 070's pre-establishment exemption** for frames over L. Under R-2 such a frame
  cannot be parsed, but the peer has not seen our 383 before the Logon exchange. Alternative: keep the
  exemption for frames over L but within 64 KiB, at the cost of a second, larger buffer before Active.
- **OD-4: the Framer checks `frame_len > max_frame_bytes` before the CheckSum, for every caller**, so
  that an over-L frame is never disregarded as garbled. This changes which error an over-max frame with
  a bad CheckSum reports, everywhere.
- **OD-5: the first production log site**, with `logger_override` resolution at `open()`. FR-003's
  "log" needs it, and nothing logs from `src/` today.
- **OD-6: the establishment deadline lives in the pump**, not in a detached Session timer (research
  R-4). It closes a pre-Active connection already refused into Disconnected, which belongs to #534.
- **OD-7: garbled frames are counted and logged even after `close()` began.** They are a transport
  observation, so FR-030 does not suppress them.
- **OD-8: acceptor first-frame garbles are replayed into the Session after `open()`.** A connection that
  never yields a Session is not counted (L-6).
- **OD-9: `reset_to` gets the true targets on volatile stores too.** This may fix #538, which is
  unconfirmed, as a side effect, so #538's reproduction runs on this branch and on its base.
- **OD-10: the parse reserve is a per-call argument**, a public C++ addition, not an `OffsetTable::Config`
  field (research R-3).

## What changes for whom

| Who | What changes | Declared where |
|---|---|---|
| The FIX counterparty | A garbled frame (a bad CheckSum or BodyLength, leading junk, 35 not third) is ignored instead of ending the session, and the next frame's gap is recovered by ResendRequest. A frame over our L ends the session in every state, the pre-Logon exemption included. A pre-Active connection closes at the logon timeout. No TestRequest is sent while its recovery traffic is flowing | B&L delta; contract C-1, C-2, C-4, C-5 |
| A C-ABI or Python consumer | The same session-observable changes (C-7's BREAKING list), plus a timeout setter and a garbled-frame counter | C-ABI MINOR + BREAKING (`version.h` history) |
| A C++ consumer of `Framer` | A new opt-in config and accessor. **For every caller, an over-max frame with a bad CheckSum now reports `wire_frame_too_large`, not `wire_checksum_mismatch`** | contract C-1, C-7 |
| A C++ `MessageStore` implementer | A new non-pure `reset_to`. No change is needed. Override it for crash atomicity | `message_store.hpp` header comment; B&L L-3 |
| A C++ consumer of `SessionEvent` | Three new alternatives. A `std::visit` without a default arm must handle them | contract C-7 |
| An operator | Per-session memory ≈ 6·L, and the cost formula is in B&L. `logon_timeout_ms` is configurable; zero is refused | B&L; SC-007 |
| A C++ `SessionConfig` user | `advertised_max_message_size` below 4096 or above 262144 is now refused at `open()` | B&L |

## Project Structure

### Documentation (this feature)

```text
specs/093-inbound-frame-dispositions/
├── spec.md
├── plan.md               # this file
├── research.md           # R-1 (35-not-third), R-2 … R-8
├── data-model.md         # E-1 … E-10
├── contracts/
│   └── inbound-frame-dispositions.md   # C-1 … C-8
├── quickstart.md
└── checklists/
    └── requirements.md
```

### Source code: the files expected to change (re-derive at implementation)

```text
include/fixpp/wire/framer.hpp          # Config::resync_on_garble, garble_record, last_garble()
src/wire/framer.cpp                    # resync pass, cross-feed SOH bit, frame_len check before CheckSum
include/fixpp/wire/offset_table.hpp    # per-call reserve argument (E-3)
src/wire/offset_table.cpp
include/fixpp/wire/parser.hpp          # reserve pass-through
include/fixpp/session/session.hpp      # L, parse span, counter, accessors, reset_unit_in_flight_
src/session/session.cpp                # open() validation + allocation; C-2 steps 0, 1, 3; liveness writer; reset unit; close() wait; logger resolution
include/fixpp/session/session_config.hpp  # logon_timeout_ms
include/fixpp/session/session_event.hpp   # three alternatives
include/fixpp/session/message_store.hpp   # reset_to (non-pure); header count comments become conditions
include/fixpp/session/memory_store.hpp    # reset_to override
include/fixpp/session/file_store.hpp
src/session/file_store.cpp                # reset_to override (temp + rename with target counters)
src/session/engine.cpp                    # pump: L-sized Framer and carry, garble accounting, deadline race; first-frame garble replay
src/session/read_first_frame_bounded.hpp  # resync-aware drain, {offset, len, garbles}, bounded by the deadline
src/config/toml_config_loader.cpp, src/config/scalar_mappers.cpp   # logon_timeout_ms
include/fix/c_api/session.h, src/capi/config.cpp, src/capi/session.cpp, include/fix/c_api/version.h
tests/abi/golden/fixpp_capi_symbols.txt, tools/capi_freeze.sha256
bindings/python/fixpp.i                   # %apply for the getter, GIL table row
tests/support/session_test_access.hpp     # buffer-shrink accessor (FR-014)
tests/session/…, tests/wire/…, tests/capi/…, tests/fuzz/fuzz_wire_framer.cpp, bindings/python/tests/…
spec/behaviors-and-limitations.md (+ -closed.md), brain/components/{session,inbound-message-path,wire,message-store-quiescence}.md, brain/log.md
```

**Structure decision**: the existing single-library layout. No new target, except possibly a test bucket
label.

## Implementation phases (for `/speckit-tasks`)

The order follows dependencies. Each phase writes its cells RED first, then the change, then GREEN.

- **P0: baselines and census** (quickstart §0): the paired bench, the per-lane ceiling, and the scratch
  census of non-canonical builders, followed by fixing those builders. No production change.
- **P1: Framer** (C-1): the opt-in resync, the cross-feed bit, `last_garble()`, the `frame_len` reorder,
  the Framer unit cells with every split, and the fuzz arm. Every other caller is unchanged with the
  flag off; the reorder's effect on Framer tests is updated deliberately.
- **P2: L and parse capacity** (C-3, FR-010 to FR-014):
  - `open()` validation and L;
  - the per-session buffer with the spill witness, and the per-call reserve;
  - `max_offset_entries` from L;
  - the 10 late sites moved to the buffer;
  - the dense-L and boundary cells on every lane;
  - FR-014's defence cell, and the re-based `LateSite_*`.
  - `test_066` and `test_070` are updated.
- **P3: pump** (C-1, C-4):
  - the L-sized Framer and carry, and garble accounting into the Session;
  - logger resolution, the counter and the event;
  - the first-frame read's resync-aware drain and replay;
  - the establishment deadline race, the TC cells, the resync cells and the timeout cells.
  - `FramerFailureClosesEstablishedSession_*` flips.
- **P4: arms** (C-2, C-5): step 1, the 35-not-third disregard, in every arm; step 3, the liveness move;
  the B-005-7, W1 and D-8 cells flip; the SC-005 cells.
- **P5: #523** (C-2 step 0). It lands before P6, because FR-041 depends on it on the initiator.
- **P6: #524** (C-6): `reset_to` and both overrides with crash-injection cells; the unit's manager-first
  rewrite; `reset_unit_in_flight_` and `close()`'s bounded wait; `HookedStore` forwarding; the #524
  cells, with and without a teardown reset, overriding and default-body stores, and wait expiry; the
  dead `teardown_reset_done_` stops removed.
- **P7: public surface** (C-7): `SessionConfig`, TOML, the C setter and getter, the golden, the freeze
  hashes, reentrancy, `version.h` MINOR + BREAKING history, and the Python typemap and GIL row with
  their cells.
- **P8: docs**: B&L (L-004-4 closed; B-005-7 narrowed; new B and L rows from C-8; L-092-6 and L-518-1
  updated); brain component pages and the log; header comments naming 093 where a decision is
  superseded (`L-004-4`'s wontfix, 092's D-8, #518's in-unit stops).
- **P9: `/simplify`, then `/speckit-verify`** (the full matrix plus MSVC), then `/gate-b`.

## Post-design Constitution re-check

- **VIII §5** holds only if B(L) truly bounds the parse. The spill witness and the per-lane peak cell make
  that measurable, not assumed.
- **XI**: `close()`'s wait is bounded, so it cannot add a hang. The deadline race is pre-Active only.
- **X §7**: the BREAKING list in C-7 is complete against the spec's FR-051, the pre-Active close added.
- **XII**: the reorder means no over-L frame takes the disregard path.

Re-check result: **PASS.**

## Complexity Tracking

| Item | Why it is needed | Simpler alternative rejected because |
|---|---|---|
| Five issues in one bundle | They share the read pump, `on_inbound_frame`'s arms and the reset unit. #514 needs #515's L for its limit, and #524 needs #523's guard on the initiator | Separate PRs would review the same arms five times and land in a forced order anyway |
| A cross-feed bit in the Framer | FR-002 must not depend on segmentation | "`8=` anywhere" would resync inside data payloads more often; "start of buffer" depends on segmentation |
| `close()`'s bounded wait | The owner ruling makes custom stores safe without breaking them | (a) disclosing the residual was rejected; (c) an "unsupported" error code touches the C-ABI error map |

## Gate A

*(Filled by `/gate-a`.)*
