# Implementation Plan: FIXT application version, the validation half

**Branch**: `099-fixt-appl-version-validation` | **Date**: 2026-11-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/099-fixt-appl-version-validation/spec.md`

## Gate A

- Round 1 applied 2026-11-03: Codex P1=4 P2=5 P3=0; Opus post-judging P1=5 P2=6 P3=8; rewrite addresses root causes RC#1, RC#2, RC#3, RC#4, RC#5, RC#6, RC#7. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_099-fixt-appl-version-validation_gate_a_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_099-fixt-appl-version-validation_gate_a_adversarial_review.md.

- Round 2 applied 2026-11-03: Codex P1=3 P2=3 P3=0; Opus post-judging P1=3 P2=1 P3=7; rewrite addresses root causes RC#1, RC#2, RC#3, RC#4, RC#5. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_099-fixt-appl-version-validation_gate_a_2_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_099-fixt-appl-version-validation_gate_a_2_adversarial_review.md.

Where each root cause of round 1 landed:

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

Where each root cause of round 2 landed:

| Root cause | Where | How |
|---|---|---|
| RC#1 endings written from the design's order | data-model E-7; research R-18; W-4, W-5, W-9; owner items 2, 3 | one per-arm table. Its "today" half measured through a real `Session` at `c0a828ef`, with a seeded row; its "after" half labelled design. R-8's and R-9's endings, E-7's pre-gate list and surface.md's behaviour table deleted in its favour |
| RC#2 requirements unqualified next to a disclosure | FR-008, FR-009, FR-010, FR-012, FR-025, FR-026, FR-027, SC-004, SC-007; data-model E-5; owner items 7 to 11 | each limit stated in the FR or SC it narrows and in its owner item, with a cell pinning the literal limit |
| RC#3 a witness names a mechanism without checking it reaches the code | contracts/witnesses.md's mutant table; W-12, W-20; data-model E-9 | each mutant names the code it changes and the cell that goes RED on that alone |
| RC#4 Constitution Check written from the plan | below | derived from the articles a parser-touching session-path change triggers, on 098's row list; a fuzz harness that drives a FIXT session's peek |
| RC#5 hand-written C-ABI inventory | research R-11; W-12; contracts/surface.md | 098's recipe; the freeze manifest with a RED-first cell; the format registry; "as `fixpp_msg_get_msg_type`" |

### Round 2 — disagreements

**Measured differently from the review.**

- **N-3's "a named pin" and "one input now has two endings by validation setting".** At `c0a828ef`, validating, absent 1137 plus a stale 52 already answers 373=1, 371=1137 and stays (research R-18), so that split predates every option. What an option moves is T's `99`, `abc`, `4` and empty variants, and stay-to-disconnect.
- **N-3's wrong-BeginString and mTLS points against the pre-gate.** Both are narrower: gate-faulted frames already answer ahead of `interpret_logon`, and L with `99` already Rejects an off-list identity with no event. Still true of the pre-gate for T's cells, and stated in owner item 3.
- **N-16 item 1's "single-fault endings are then one per input".** True on the wire. Not on NextNumIn: `99` and `abc` keep it on L and consume it on T and A. Stated in owner item 3. And "a double fault follows dictionary order" hides a case the review's example does not show: absent 1137 plus an undeclared tag stays in the arm.
- **Codex 3's fix.** Moving the application check ahead of the mismatch is not enough, because `interpret_logon` reads the last 35. Measured: `D…A` carrying Logon fields establishes a validation-off session. S1 tests the third field and the mismatch together, and disconnects itself.

**Fix shapes not taken.**

- **Codex 1, a reset at the connection boundary and a reconnect witness.** Declined, as the review did: FR-027 is rewritten to what the design does, since selection in an establishment arm does not read the slot, so such a witness cannot go RED.
- **N-3: "give a validating initiator the block the acceptor already has".** Not at the acceptor's position relative to mTLS: on the initiator that position is after the 141=Y reset unit, so a reply with `141=Y` and a bad 1137 would durably reset both stores and then be refused. It goes right after `check_inbound`. It costs the mTLS order on a double fault and the consume split.
- **N-16 item 4, one negotiated member.** Only the owner can take it; not on his list unless he asks, as the review says.
- **N-16 item 8, merging the close mutants.** Partly. M-12 is folded into M-12a to M-12c, M-37 into W-13's surface check, and M-26 goes with the pre-gate. M-1 stays, because SC-001 names it. M-11, M-34 and M-35 stay: each changes a different line and has its own RED cell, which is what RC#3 asks for. RC#3 also adds mutants (M-5d per site, M-6b, the check's clause mutants), so the table grows. The cost is met differently: one reused mutant worktree, batched by binary, instead of one worktree per mutant.
- **N-8.** The first of its two choices: 370 joins the peek's set, so 370 has one ending in every field order. Leaving the peek and pinning both orders would keep two endings for one input, against the owner's standing rule.

**N-16's other simplifications are all taken:** one `shared_ptr` for the route set, no public `Session` parameter, no omission rule, the second 35 computed on the cold path, and `transport()` shaped as `get()`.

## Summary

A FIXT.1.1 session stops using one dictionary for everything. Per frame it selects the transport dictionary for an admin message and,
for an application message, the dictionary of its resolved version (1128, else the negotiated 1137). The frame scan, the validator and
the `fromApp` parse all use that one table. A validating session refuses a message no dictionary can answer, and answers a bad 1137 with a Reject and a disconnect on every shape and role. What each state does with each input is one table, data-model E-7. The session dictionary
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

Derived from the articles a parser-touching change on the session path triggers, with 098's rows as the template, and not from this plan's own steps. `Tasks` means a task carries it; each W row is in contracts/witnesses.md.

| Clause | Check | Status |
|---|---|---|
| `[const §I.1]`, `[const §XVIII.1]` | FIXT.1.1 session and application layers are v1.0 scope; no version added | Pass |
| `[const §I.3]` | S-026 moves to `done` with its witnesses, under research R-16's condition | Tasks |
| `[const §VI.4]`, `[const §VI.5]`, `[const §VI.6]` | Normative References in spec.md; §5.3.4 re-pointed so no section maps to a row it is not part of | Pass, tasks |
| `[const §VII.3]`, `[const §VII.4]` | every witness RED first (on `c0a828ef`, or by its named mutant where that tree cannot compile it) | Tasks, W-1 to W-24 |
| **`[const §VII.7]`** | **Applies.** `peek_fixt_route` is a new walk over untrusted bytes on every frame of a FIXT session, and the validator gains an envelope branch. No existing harness drives a FIXT session. A FIXT arm in `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` and an envelope arm in `tests/fuzz/fuzz_wire_validator.cpp`, seeded with R-9's and R-18's inputs in the existing corpus directories, replayed by `ctest -L fuzz`, and a ten-minute campaign at verify | Tasks, W-24 |
| **`[const §VII.8]`** | Isolation-sensitive, each its own executable and `add_test`: W-15 (heap count, TSan arm), W-16 (allocation interceptor), W-20 (fail-at-N global `operator new`). Everything else is isolation-safe: W-1 to W-11, W-19 and W-21 go in an existing grouped session or dictionary bucket, selected by a `099` label | Tasks |
| `[const §VIII.2]`, `[const §VIII.5]` | paired bench on the hot path; no per-message allocation | W-22, W-16 |
| `[const §VIII.3]` | the same PR carries the benchmark: a new FIXT case in `on_inbound_frame_bench.cpp` | Tasks |
| `[const §IX.1]`, `[const §IX.2]`, `[const §IX.4]` | touched modules at thresholds or a per-line assessment; ASan, UBSan, TSan; clang-tidy, clang-format, cppcheck | At verify |
| `[const §X.1]`, `[const §X.6]`, Appendix A | triggered: ABI surface change, threading, wire format and validator, session FSM. All four controls: `/clarify` done; `/analyze` after tasks; Gate A; the owner's `/plan` sign-off | **Sign-off pending** |
| `[const §X.5]` | one reentrancy token per new symbol; `tools/check_capi_reentrancy.sh` enforces it | W-12 |
| `[const §X.7]` | no breaking change: four added symbols. The registration refusal is reachable only through the new validation setter, so no call that succeeded now fails. The non-validating form, which would be breaking, is left to the owner | Pass, owner item 5 |
| `[const §XI.7]`, Appendix A | an immutable object read across session strands is a threading trigger: the same four controls | **Sign-off pending** |
| `[const §XII.9]` | not a security-affecting feature under the recommendations: mTLS order is unchanged. Under owner item 3's Option 1 a new Reject reaches an off-list peer; the same four controls are running anyway | Pass, owner item 3 |
| `[const §XIII.3]` | the role loop's new log line is outside session scope, on a bare strand, so it reads `co_await fixpp::current_trace_context`, as that clause requires of such a path | Tasks |
| `[const §XIV.2]` | `Validator` keeps five pure virtuals; the additions are non-virtual, on `dictionary_driven_validator` | Pass |
| `[const §XV.1]`, `[const §XV.9]` | no per-message heap; no `std::mutex` and no lock anywhere | W-16, pass |
| `[const §XV.13]` | hybrid dictionary model unchanged; S stays authoritative for its version | Pass |
| `[const §XVI.3]`, `[const §XVI.4]` | `/clarify` ran before this plan; `/analyze` after tasks | Pass, tasks |
| `[const §XVI.6]` | the orchestrator writes no library code; `phase-implementer` does. Mutants in a separate worktree | Tasks |
| `[const §XVII.1]`, `[const §XVII.7]`, `[const §XVII.8]` | Gate A before tasks; the local pre-PR build; `/speckit-verify` before Gate B | Pass, pipeline |
| `[const §XX]` | no article changes, so no amendment; the design-document notes are not governed by it (research R-11) | Pass |
| `[arch §2.3]`, `[arch §2.4]` | the registry owns no validator; the route set is in `session`, which may include both; `reify.cpp` is already a bridge file | Pass |

No violation to justify. Re-checked after the round 2 design: Option 3 removes the pre-gate step, and no row depended on it.

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
src/session/{session,engine}.cpp, session_engine_access.hpp, fixt_route_set.{hpp,cpp}, peek_fixt_route.hpp, fixt_session_check.hpp
src/log/format_registry.cpp   (the role loops' log line)
src/capi/{config,engine,message_read,message_write}.cpp, capi_internal.hpp
src/config/selector_resolver.cpp
bindings/python/{fixpp.i,tests/,tests/wheel/test_import_surface.py}
tests/session/test_099_*.cpp, tests/capi/capi_099_*, tests/config/test_099_*
tests/support/session_test_access.hpp
tests/session/{test_fixt_logon_establishment,unparseable_frame_disposition_test}.cpp   (research R-17)
tests/perf/   (W-16)  tests/dictionary/   (W-17, W-21)
tests/abi/golden/fixpp_capi_symbols.txt, tests/capi/version_test.cpp, tools/capi_freeze.sha256   (research R-11's recipe)
tests/fuzz/{fuzz_session_recovery_admin_parse,fuzz_wire_validator}.cpp, tests/fuzz/corpus/{session_recovery_admin_parse,wire_validator}/   (W-24)
bench/session/on_inbound_frame_bench.cpp
tests/interop/happy/, tests/interop/conversation/fixt_app_script.yaml, parent phase-9-harness/   (W-18, per the ruling)
.specify/{2b-wire,2c-codegen,2i-capi,api-contract}.md
spec/, brain/, parent trackers
```

`src/capi/session.cpp` stays out: `register_session` is the change, and its existing `translate` returns the refusal. `src/session/admin_messages.cpp` stays out under owner item 3's recommendation: `interpret_logon` already takes hooks, and nothing is factored out of it.

## Phases

1. **Dictionary and validator** (E-1a, E-2, E-3, E-4): W-17, W-21's unit half; the existing validator tests stay green; W-3's unit half; `validator_bench`.
2. **Route set and check** (E-1, E-9), `Engine::start()` and its two `engine.hpp` comments, `session_engine_access::adopt_routes`, `register_session`, `open()`, the role loops' log line and its `format_registry.cpp` entry: W-8, W-15, W-20.
3. **Peek and selection** (E-6, E-7): W-21.
4. **Arms**: scan; S1 to S3; A1 to A3; dispatch (R-7, E-7); the test changes of research R-17: W-1 to W-7, W-9, W-16, W-19. **W-24's two fuzz arms and their seeds.**
5. **Accessor** (E-5), including clone and reify: W-10. Outbound pin: W-11.
6. **C ABI, Python, TOML**, research R-11's inventory, the `tools/capi_freeze.sha256` refresh: W-12, W-13, W-14. **Apply the `Updated (099-…)` notes of research R-11 to `.specify/2b-wire.md`, `2c-codegen.md`, `2i-capi.md`, and the four symbols to `api-contract.md`.**
7. **Live cell**: W-18, per the owner's ruling. Bench: W-22.
8. **#651 comments, records, brain**: W-23. Tasks: S-026 to `done` under research R-16's condition, with its live-evidence sentence; L-041-2, L-033-1, L-097-1 to the closed file under theirs; `coverage-index.md` §5.3.4 re-pointed, and §4.3.7, §5.3 and the FIXT section's S-026 wording corrected; L-033-5 and the `open()` guard's comment corrected; the new L-rows and B-rows of quickstart.md.

Phases 1–3 precede 4; 5 and 6 are parallel after 4. Mutants run after each phase's witnesses, from a separate worktree.

## Owner sign-off (`[const §X.6]`) — pending

The `/plan` sign-off, including these. The design is written with each recommendation; none is decided. Each item says what it relaxes, and that FR or SC says so too.

1. **R-13, the live cell.** Recommended: Option B, one peer-to-fixpp 35=BN, without a republish first. Alternative: Option A, the arm flip. Consequence of A: it satisfies NFR-005's words but cannot go RED on selection, and S-026's row must say its live evidence is Logon-only. Relaxes: under A, what S-026's `done` claims.
2. **R-8, an application frame before Logon completes.** Recommended: S1. On a validating FIXT session an establishment arm disconnects, sending nothing, on any frame whose third field is not admin or whose last MsgType differs from its third field's (E-7 `NC-a` to `NC-d`, `LS-a` to `LS-d`). Alternative: C++'s form, validate against S and Reject in the arm. Consequence of the alternative: one condition with two endings by shape, as today, and a third-field `D`, last-field `A` frame carrying Logon fields must be stopped some other way. Relaxes: FR-009, FR-025 and FR-026's Rejects do not apply in those two arms. All three engines are traced in research R-8; none gives one ending to all of it.
3. **The 1137 check's order.** Correction: this item used to say C++ and J read 1137 first. Both read BeginString first and throw on a mismatch; both then read 1137 ahead of validating, but only absence matters there; neither checks the value ahead of validation (research R-9).
   - **Recommended, Option 3:** gate first; a gate Reject naming 1137 on a Logon in an establishment arm disconnects; a validating initiator gains the block the acceptor has. SendingTime, posture and mTLS keep today's order among themselves. Why: it is the simplest, with no new step and no factoring, and it meets ruling 1 for every single fault. Cost: a double fault follows dictionary order, so `99` plus a stale 52 answers 10/52 on T and A and 5/1137 on L, and **an absent 1137 plus an undeclared tag answers 2/that tag and stays in the arm until the Logon timeout** (measured), where ruling 1's disconnect does not fire; `99` and `abc` keep NextNumIn on L and consume it on T and A, observable only on an initiator's reconnect; an initiator's block sits ahead of mTLS, unlike the acceptor's, so a bad 1137 from an off-list peer draws a Reject there. Placing it behind mTLS would let a `141=Y` reply with a bad 1137 durably reset both stores before the refusal.
   - **Alternative, Option 1:** the check runs ahead of the gate, both arms, as round 1 recommended. One ending per input on every shape, NextNumIn kept throughout, a double fault always answers 1137. Cost: it overrules T's order of SendingTime, posture and mTLS, so `99` from an off-list identity draws a Reject with the authorization event never raised, on T too, and it adds a pre-gate step and the `scan_logon_default_appl_ver_id` factoring. A wrong BeginString plus `99` draws a Reject unless the step also gains a BeginString guard.
   - **Alternative, Option 2:** gate first and nothing added. Consequence: an absent 1137 stays in the arm until the Logon timeout, against ruling 1's "a disconnect".
   - `W_StaleSendingTime_Beats1137Gate` stays green under all three: it runs non-validating. Its input already answers differently on a validating session (research R-18). The comparison, per input, is research R-9's table. Relaxes: under Option 3, FR-010's "one ending" is for single faults and on the wire, and its "a disconnect" does not reach a Logon whose gate Reject names another tag.
4. **The registration refusal.** Ruling 2 says "fails at `open()`". Recommended, in addition: `Engine::register_session` refuses a validating FIXT session failing the check, through the same helper. Alternative: `open()` only. Consequence of the alternative: a C, Python or TOML host gets success and the session silently never comes up, because the role loops discard an `open()` failure; `contracts/surface.md`'s refusal rows cannot happen. Relaxes: nothing; it adds to ruling 2's wording.
5. **A non-validating FIXT session with a bad config** (today's guards). Recommended: stays refused at `open()` only, with a log line, and a live L-row, because ruling 3 says additive. Alternative: refuse it at registration too. Consequence of the alternative: `fixpp_session_open` fails where it returned success, a declared breaking change under `[const §X.7]`, though it only affects a session that never came up. Relaxes: under the recommendation, FR-004 stops at validating sessions.
6. **`[2c §5]` commitment 1, "non-negotiable".** Recommended: `Updated (099-…)` notes that ratify the shipped `fixpp_msg_version` and add `fixpp_msg_get_resolved_appl_ver_id`, text in research R-11. Alternative: implement the three-byte tag as a declared breaking change. Not a `[const §XX]` amendment: no article states it. Relaxes: the design document's word.
7. **An overlay's header Length+Data pair on a frame routed to another table.** Recommended: disclose as an L-row. Measured: a `34=7` inside the Data is read as MsgSeqNum, on a validating session too; a peer already controls MsgSeqNum, so the misread grants nothing new. Own-version traffic is unaffected. Alternative: refuse such a config at `open()`, which needs a header-scoped form of `table_view::has_nonstandard_pair()`; or compose the two tables' hooks, which needs a collision rule. Consequence of the alternatives: a config that loads today is refused, or more design. **Relaxes: FR-008, FR-012 and SC-004**, each of which states the limit; W-3 (f) pins it.
8. **FR-024, Message Type Default.** Recommended: not implemented, as in all three engines; `coverage-index.md` §5.3.4 goes to `—` with a disposition naming a live L-row. Alternative: implement it from Logon's 372, 1130 and 1410. Consequence of the recommendation, measured: a peer that sends it on a validating session is refused at Logon where the transport dictionary is `FIXT11.xml` (373=2, 371=384, stays in the arm), as today; on FIX Latest it establishes and then draws a Reject for a message valid in the version it meant. Relaxes: S-026 does not claim §5.3.4.
9. **FR-025, a repeated MsgType.** Recommended: in LogonReceived and Active, a validating FIXT session rejects a frame whose last MsgType differs from its third field's. Alternative: as today, validated by the third field and dispatched by the last. Consequence of the alternative: measured, `0…D` reaches `fromApp` validated as a Heartbeat, `D…5` logs the session out, and `D…3` is consumed in silence. Relaxes: a non-validating session keeps all of that, and `D…A` with Logon fields still establishes it; LogoutSent keeps taking `D…5` as the reply. A non-FIXT validating session keeps the split too (N-14; to file).
10. **FR-026, a 1128 past the header.** Recommended: Reject 373=14, 371=1128, ahead of the validator. Alternative: let it pass, as the validator does. Consequence of the alternative: a delivered validated message can have one version by routing and another by `dict::reify`. Relaxes: a dictionary declaring 1128 in a message body would be false-rejected; none vendored does, and W-4 recomputes that.
11. **A partial session dictionary is refused.** Ruling 2 says "defines admin messages". FR-003 reads that as all seven, and a Logon that accepts 1137. Recommended: as FR-003, so a validating FIXT session on a partial S with no FIXT11 registered, which used to work for the messages S declares, is refused. Alternative: accept any S with a Logon. Consequence of the alternative: a Heartbeat S does not define draws a false Reject on a live session. Consequence of the recommendation: the tests in research R-17 change. Relaxes: ruling 2's wording is read strictly.

For the owner's information, consequences of the design above, not choices:

- `Engine::start()` now blocks for the walks on an engine with a FIXT session, about 0.5 s at `-O3` and 3.3 s at `-O0` for FIX50SP2 and FIX Latest (research R-3). A host calling it from a handler on `exec` stalls that thread for as long; its comments change to say so.
- Measured at `c0a828ef`: with validation off, `35=D … 98=0|108=30|1137=9|35=A` establishes a session of either role, because `interpret_logon` reads the last 35. Ruling 1 keeps validation-off unchanged, so 099 discloses it and it is to be filed.
- 370 is treated as a header tag on a FIXT session whatever the route. QuickFIX C++ refuses it (research R-5; B-row).
