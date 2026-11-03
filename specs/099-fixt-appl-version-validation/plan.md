# Implementation Plan: FIXT application version, the validation half

**Branch**: `099-fixt-appl-version-validation` | **Date**: 2026-11-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/099-fixt-appl-version-validation/spec.md`

## Gate A

- Round 1 applied 2026-11-03: Codex P1=4 P2=5 P3=0; Opus post-judging P1=5 P2=6 P3=8; rewrite addresses root causes RC#1, RC#2, RC#3, RC#4, RC#5, RC#6, RC#7. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_099-fixt-appl-version-validation_gate_a_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_099-fixt-appl-version-validation_gate_a_adversarial_review.md.

Where each root cause landed:

| Root cause | Where | How |
|---|---|---|
| RC#1 R-9's endings not derived | research R-9, R-5; data-model E-7; W-4, W-5 | re-derived by a probe per shape. Validator-level only, and said so. One FIXT pre-gate holds the 1137 disposition, the MsgType mismatch check and the past-header probe |
| RC#2 cost written for walks | research R-3; data-model E-1, E-4, E-6; NFR-003, SC-007; W-15 | one validator per distinct dictionary in a session-library route set, built in `Engine::start()`; envelope per call. The fix shape's owner changed, below |
| RC#3 no contract for the resolved version | data-model E-5; research R-11 | one E-5 paragraph; the notes' text is in R-11, to apply at Phase 6 |
| RC#4 `open()` is not a caller-visible contract on Engine routes | research R-2, R-11; data-model E-1, E-9; FR-004, FR-028; W-8, W-12 to W-14, W-20 | one check helper from `register_session` and `open()`; each surface traced; build failure `out_of_memory` |
| RC#5 witnesses organised by claim | contracts/witnesses.md | re-issued as the matrix; every FR, NFR and SC walked |
| RC#6 the peek's oracle in prose | research R-5; data-model E-7; W-21 | one clause; S's hooks; header tags only; named admin predicate |
| RC#7 registry gained mutable state | data-model E-1a | dissolved: the registry gains no mutable state, so its value semantics are not revisited |

**Found while applying, not in either review.** R-8 said no existing test sets a validating FIXT session, and the review's RC#1 reasoning repeats it. That is false: its recipe searched for `= true` and these tests assign a variable. Validating FIXT sessions run in `test_fixt_logon_establishment.cpp` and `unparseable_frame_disposition_test.cpp`, and some of them use a partial dictionary that ruling 2 now refuses. Listed, with what each needs, in research R-17.

### Round 1 — disagreements

The review marks no finding Disagree. Fix shapes not taken, each because source says otherwise or something simpler holds:

- **RC#2, N-3: "the registry owns one const validator per slot".** Declined. `[arch §2.3]` lets `dictionary` include `core` only, `tools/check_layers.py` enforces it on `src/dictionary/`, and `[arch §2.4]`'s bridge list does not cover `version_registry`. The validators are in the session library instead, one layer up. Every constraint of N-3 holds: per-call envelope, non-FIXT layout unmoved, W-15 counts heap.
- **RC#2, N-6: "keep the CAS as the path for directly constructed sessions".** Declined. With the build in `Engine::start()` nothing races. A direct-constructed session builds a private set in its own `open()`. No atomic remains.
- **RC#7, N-8, Codex 2: delete move assignment, nothrow-allocate the cache, define null-cache answers, a detail key on owner accessors.** Not needed. All of it fixes state the registry no longer gains. `fixt_route_set` is not movable at all, which satisfies Codex 2's owner-object objection where the owners now live.
- **Codex 9, "select Option B explicitly".** Declined, as the review did: R-13 is the owner's. W-18 is a conditional row with both options runnable.
- **N-5's reason.** The review left it open. Taken from the engines: 373=13 in the header and 373=14 past it, as C++ and Go; J answers differently, recorded in research R-5.
- **N-21, "reset at reconnect or disclose".** Neither. Resetting would change `negotiated_version_profile()` on a validation-off session, against ruling 1. Selection reads a separate per-connection value instead (FR-027), and that accessor keeps today's writes.
- **N-2's note that `1128` appears in no `<messages>` block of the QuickFIX files.** Kept, and the Orchestra half measured too; W-4 recomputes it.

## Summary

A FIXT.1.1 session stops using one dictionary for everything. Per frame it selects the transport dictionary for an admin message and,
for an application message, the dictionary of its resolved version (1128, else the negotiated 1137). The frame scan, the validator and
the `fromApp` parse all use that one table. A validating session refuses a message no dictionary can answer, and answers a bad 1137 the same way on every shape and role. The session dictionary
keeps answering its own version, so an overlay survives. An Engine builds each table and validator once, in `start()`. C, Python and TOML gain what a
FIXT session needs.

Decisions and rejected alternatives: [research.md](./research.md). Shapes: [data-model.md](./data-model.md). Surface:
[contracts/surface.md](./contracts/surface.md). Witnesses: [contracts/witnesses.md](./contracts/witnesses.md).

## Technical Context

**Language/Version**: C++23; C for the ABI surface; Python through SWIG.

**Primary Dependencies**: existing only (pugixml, asio, toml++). No new dependency, no pin.

**Storage**: N/A.

**Testing**: GoogleTest, real `Session` over raw frames, `tests/capi`, `bindings/python/tests`, the parent's interop harness.

**Target Platform**: Linux (clang, gcc), MSVC.

**Project Type**: library.

**Performance Goals**: non-FIXT inbound and validate within the paired band, by W-22. No per-message heap on FIXT, by W-16.

**Constraints**: non-FIXT behaviour byte-identical. C ABI additive. M validators for N sessions on M distinct dictionaries, by W-15.

**Scale/Scope**: one session-path change touching every inbound arm; four C symbols; one TOML key.

## Constitution Check

| Article | Check | Status |
|---|---|---|
| `[const §I.1]`, `[const §XVIII.1]` | FIXT.1.1 session and application layers are v1.0 scope | Pass |
| `[const §VI.4]`, `[const §VI.5]`, `[const §VI.6]` | Normative References in spec.md; §5.3.4 re-pointed so no section maps to a row it is not part of; S-026's `done` under research R-16 | Pass, tasks |
| `[const §VIII.2]`, `[const §VIII.5]` | paired bench on the hot path; no per-message allocation | Pass, W-22, W-16 |
| `[const §VIII.3]` | the same PR carries the benchmark: a new FIXT case in `on_inbound_frame_bench.cpp` | Pass, tasks |
| `[const §IX]` | coverage assessed in verify | Pass, tasks |
| `[const §X.1]`, `[const §X.6]` | C ABI change, so `/clarify`, `/analyze`, Gate A and the owner's `/plan` sign-off | **Sign-off pending** |
| `[const §X.5]` | reentrancy documented per new symbol | Pass, contracts/surface.md |
| `[const §X.7]` | no breaking change: four added symbols. The registration refusal is reachable only through the new validation setter, so no call that succeeded now fails. The non-validating form, which would be breaking, is left to the owner | Pass, owner item 5 |
| `[const §XI.7]`, Appendix A | an immutable object read across session strands is a threading trigger: the same four controls | **Sign-off pending** |
| `[const §XIV.2]` | `Validator` keeps five pure virtuals; the additions are non-virtual, on `dictionary_driven_validator` | Pass |
| `[const §XV.1]`, `[const §XV.9]` | no per-message heap; no lock anywhere. An Engine's build is in the synchronous `Engine::start()`. A direct-constructed session builds inside `open()`, a coroutine, as today's walk of S does: CPU work with no lock | Pass |
| `[const §XV.13]` | hybrid dictionary model unchanged; S stays authoritative for its version | Pass |
| `[const §XVII]` | Gate A before tasks; verify; Gate B | Pass, pipeline |
| `[const §XX]` | no article changes, so no amendment; the design-document notes are not governed by it (research R-11) | Pass |
| `[arch §2.3]`, `[arch §2.4]` | the registry owns no validator; the route set is in `session`, which may include both; `reify.cpp` is already a bridge file | Pass |

No violation to justify.

## Project Structure

```text
specs/099-fixt-appl-version-validation/
├── plan.md  research.md  data-model.md  quickstart.md  spec.md
├── contracts/{surface,witnesses}.md
└── checklists/requirements.md

include/fixpp/dict/{version_registry,version_profile,dictionary}.hpp
include/fixpp/wire/{validator,parser}.hpp
include/fixpp/session/{session,session_config,engine}.hpp
include/fix/c_api/{session,engine,message,version}.h
src/dictionary/{version_registry,dictionary,reify}.cpp
src/session/{session,engine}.cpp, fixt_route_set.{hpp,cpp}, peek_fixt_route.hpp, fixt_session_check.hpp, admin_messages.cpp
src/capi/{config,engine,message_read,message_write}.cpp, capi_internal.hpp
src/config/selector_resolver.cpp
bindings/python/{fixpp.i,tests/,tests/wheel/test_import_surface.py}
tests/session/test_099_*.cpp, tests/capi/capi_099_*, tests/config/test_099_*
tests/support/session_test_access.hpp
tests/session/{test_fixt_logon_establishment,unparseable_frame_disposition_test}.cpp   (research R-17)
tests/perf/   (W-16)  tests/dictionary/   (W-17, W-21)
tests/abi/golden/fixpp_capi_symbols.txt, tests/capi/version_test.cpp
bench/session/on_inbound_frame_bench.cpp
tests/interop/happy/, tests/interop/conversation/fixt_app_script.yaml, parent phase-9-harness/   (W-18, per the ruling)
.specify/{2b-wire,2c-codegen,2i-capi,api-contract}.md
spec/, brain/, parent trackers
```

`src/capi/session.cpp` stays out: `register_session` is the change, and its existing `translate` returns the refusal.

## Phases

1. **Dictionary and validator** (E-1a, E-2, E-3, E-4): W-17, W-21's unit half; the existing validator tests stay green; W-3's unit half; `validator_bench`.
2. **Route set and check** (E-1, E-9), `Engine::start()`, `register_session`, `open()`, the role loops' log line: W-8, W-15, W-20.
3. **Peek and selection** (E-6, E-7): W-21.
4. **Arms**: scan, pre-gate, gate, interpret, dispatch (R-7, R-8, R-9); the test changes of research R-17: W-1 to W-7, W-9, W-16, W-19.
5. **Accessor** (E-5), including clone and reify: W-10. Outbound pin: W-11.
6. **C ABI, Python, TOML**: W-12, W-13, W-14. **Apply the `Updated (099-…)` notes of research R-11 to `.specify/2b-wire.md`, `2c-codegen.md`, `2i-capi.md`, and the four symbols to `api-contract.md`.**
7. **Live cell**: W-18, per the owner's ruling. Bench: W-22.
8. **#651 comments, records, brain**: W-23. Tasks: S-026 to `done` under research R-16's condition, with its live-evidence sentence; L-041-2, L-033-1, L-097-1 to the closed file under theirs; `coverage-index.md` §5.3.4 re-pointed, and §4.3.7, §5.3 and the FIXT section's S-026 wording corrected; L-033-5 and the `open()` guard's comment corrected; the new L-rows and B-rows of quickstart.md.

Phases 1–3 precede 4; 5 and 6 are parallel after 4. Mutants run after each phase's witnesses, from a separate worktree.

## Owner sign-off (`[const §X.6]`) — pending

The `/plan` sign-off, including these. The design is written with each recommendation; none is decided.

1. **R-13, the live cell.** Recommended: Option B, one peer-to-fixpp 35=BN, without a republish first. Alternative: Option A, the arm flip. Consequence of A: it satisfies NFR-005's words but cannot go RED on selection, and S-026's row must say its live evidence is Logon-only.
2. **R-8, an application frame before Logon completes.** Recommended: on a FIXT session the establishment arms validate admin frames only, so the frame disconnects. Alternative: C++'s form, validate against S and Reject in the arm. Consequence of the alternative: one condition with two endings by shape, as today. All three engines are now traced: Go disconnects without validating; C++ Rejects an invalid frame and disconnects a valid one; J, with no 1128, logs an error and stays in the arm until the Logon timeout (read from source, default settings, one event strategy read).
3. **N-1, the 1137 check's order.** Recommended: on a validating FIXT session it runs ahead of the validate gate, in both arms, as C++ and J read 1137 first. Alternative: gate first. Consequence of the alternative: an absent 1137 stays in the arm until the Logon timeout, against ruling 1's "a disconnect", and `1137=99` disconnects on T and stays on L.
4. **The registration refusal.** Ruling 2 says "fails at `open()`". Recommended, in addition: `Engine::register_session` refuses a validating FIXT session failing the check, through the same helper. Alternative: `open()` only. Consequence of the alternative: a C, Python or TOML host gets success, and the session silently never comes up, because the role loops discard an `open()` failure; `contracts/surface.md`'s refusal rows cannot happen.
5. **A non-validating FIXT session with a bad config** (today's guards). Recommended: stays refused at `open()` only, with a log line, and a live L-row, because ruling 3 says additive. Alternative: refuse it at registration too. Consequence of the alternative: `fixpp_session_open` fails where it returned success, a declared breaking change under `[const §X.7]`, though it only affects a session that never came up.
6. **`[2c §5]` commitment 1, "non-negotiable".** Recommended: `Updated (099-…)` notes that ratify the shipped `fixpp_msg_version` and add `fixpp_msg_get_resolved_appl_ver_id`, text in research R-11. Alternative: implement the three-byte tag as a declared breaking change. Not a `[const §XX]` amendment: no article states it.
7. **An overlay's header Length+Data pair on a frame routed to another table.** Recommended: disclose as an L-row. Measured: a `34=7` inside the Data is read as MsgSeqNum, on a validating session too. Alternative: refuse such a config, or compose the two tables' hooks; both are more design. Own-version traffic is unaffected.

For the owner's information, consequences of the design above: `Engine::start()` now blocks for the walks on an engine with a FIXT session, about 0.5 s at `-O3` and 3.3 s at `-O0` for FIX50SP2 and FIX Latest (research R-3). A validating FIXT session on a partial session dictionary, which used to work, is refused, which follows from FR-003's "all seven admin messages" and changes existing tests (research R-17).
