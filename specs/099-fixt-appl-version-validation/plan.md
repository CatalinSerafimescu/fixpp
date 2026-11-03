# Implementation Plan: FIXT application version, the validation half

**Branch**: `099-fixt-appl-version-validation` | **Date**: 2026-11-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/099-fixt-appl-version-validation/spec.md`

## Gate A

- Round 1 applied 2026-11-03: Codex P1=4 P2=5 P3=0; Opus post-judging P1=5 P2=6 P3=8. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_099-fixt-appl-version-validation_gate_a_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_099-fixt-appl-version-validation_gate_a_adversarial_review.md.
- Round 2 applied 2026-11-03: Codex P1=3 P2=3 P3=0; Opus post-judging P1=3 P2=1 P3=7. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_099-fixt-appl-version-validation_gate_a_2_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_099-fixt-appl-version-validation_gate_a_2_adversarial_review.md.
- Round 3 exhausted the first loop 2026-11-03: Codex P1=2 P2=3 P3=0; Opus post-judging P1=0 P2=2 P3=8. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_099-fixt-appl-version-validation_gate_a_3_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_099-fixt-appl-version-validation_gate_a_3_adversarial_review.md.
- The owner chose to re-run `/clarify` and `/plan`, ruled every sign-off item, and this plan applies round 3's residuals. Loop 2 starts from this commit.
- Loop 2 round 1 applied 2026-11-03: Codex P1=2 P2=8 P3=2; Opus post-judging P1=0 P2=2 P3=7; rewrite addresses root causes RC#1, RC#2, RC#3, RC#4, RC#5. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_099-fixt-appl-version-validation_gate_a_l2r1_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_099-fixt-appl-version-validation_gate_a_l2r1_adversarial_review.md.
- Loop 2 round 2 converged 2026-11-03: Codex P1=0 P2=3 P3=1; Opus post-judging P1=0 P2=0 P3=4; P3 fixes applied without a further review, on the owner's approval. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_099-fixt-appl-version-validation_gate_a_l2r2_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_099-fixt-appl-version-validation_gate_a_l2r2_adversarial_review.md.

### Loop 2 round 1 — disagreements

- **Codex 1's extension of W-16 to a first and a reconnect Logon: not applied.** The Logon arm allocates at `c0a828ef` for reasons unrelated to this feature, so that cell would be RED regardless. Measured (`s dictionaries alloc`, that round's review), one frame: a Heartbeat 3 `operator new` calls, an acceptor Logon 5, an initiator Logon 4, a Logon with a 40-byte 553 6; the extra is the harness's capture, the mock clock's sleeper node, and a `std::string` in `on_inbound_not_connected_`. The shipped scope starts at the first Active (L-093-13, B-544-1). Its other half, the false cost sentence in NFR-002, is applied: `reserve` in `open()`.
- **Codex 7's repeated-35 point: not applied.** Not a defect. E-8's "the view's 35" can only mean `get`, which is first-wins, and `reify` reads `view.get<35>()`. On `0…D` both say admin with `Unknown`. A clone and a reify parse the same bytes the same way, so they agree too.

### Loop 2 round 2 — disagreements

- **Codex 2's proposed cell and mutant: declined.** Its `R 1/385 A` on every shape is false on L, whose gate answers `R 5/1130 A` first (`gate_a_l2r2/mtz2.log`, under the parent's `research/probes/099_fixt_session_today/`), and owner item 3 has a double fault follow dictionary order. No shape-independent priority exists to pin, so E-10's order is deleted, not witnessed.
- **Codex 3's new reader cells: declined.** No reader it names can differ from the view (`gate_a_l2r2/nh.log`): each reads the one `OffsetTable::group_slices` of a parse over the one table, so store equality plus one parse covers them, and W-3 (f) already catches a re-parse that drops membership.

## Summary

A FIXT.1.1 session stops using one dictionary for everything. Per frame it selects the transport dictionary for an admin message and, for an application message, the dictionary of its resolved version: 1128, else the peer's Message Type Default for that MsgType, else the negotiated 1137. The frame scan, the validator and the `fromApp` parse all use that one table. The session dictionary judges the header fields it declares on every route, and its header Length+Data pairs hold on every route. A validating session refuses a message no dictionary can answer, and answers a bad 1137 or 1130 with a Reject and a disconnect on every shape and role. What each state does with each input is data-model E-7 and E-10. The session dictionary keeps answering its own version, so an overlay survives. An Engine builds each table and validator once, in `start()`. C, Python and TOML gain what a FIXT session needs, and `fixpp_msg_version` becomes the resolved version, a declared break.

Decisions and rejected alternatives: [research.md](./research.md). Shapes: [data-model.md](./data-model.md). Surface: [contracts/surface.md](./contracts/surface.md). Witnesses: [contracts/witnesses.md](./contracts/witnesses.md).

## Technical Context

**Language/Version**: C++23; C for the ABI surface; Python through SWIG.

**Primary Dependencies**: existing only (pugixml, asio, toml++). No new dependency, no pin.

**Storage**: N/A.

**Testing**: GoogleTest, real `Session` over raw frames, `tests/capi`, `bindings/python/tests`, the parent's interop harness.

**Target Platform**: Linux (clang, gcc), MSVC.

**Project Type**: library.

**Performance Goals**: non-FIXT inbound and validate within the paired band, by W-22. No per-message heap on FIXT, by W-16.

**Constraints**: non-FIXT behaviour as SC-005 states. C ABI additive except `fixpp_msg_version`. M validators for N sessions on M distinct route tables, by W-15.

**Scale/Scope**: one session-path change touching every inbound arm; three C symbols, the constants and one declared break; one TOML key; one vendored dictionary edit.

## Constitution Check

Derived from the articles a parser-touching change on the session path triggers, with 098's rows as the template, and not from this plan's own steps. `Tasks` means a task carries it; each W row is in contracts/witnesses.md.

| Clause | Check | Status |
|---|---|---|
| `[const §I.1]`, `[const §XVIII.1]` | FIXT.1.1 session and application layers are v1.0 scope; no version added | Pass |
| `[const §I.3]` | S-026 moves to `done` with its witnesses, under research R-16's condition | Tasks |
| `[const §VI.4]`, `[const §VI.5]`, `[const §VI.6]` | Normative References in spec.md; §5.3.4 now delivered by S-026, so its mapping is true | Pass, tasks |
| `[const §VII.3]`, `[const §VII.4]` | every witness RED first (on `c0a828ef`, or by its named mutant where that tree cannot compile it) | Tasks, W-1 to W-25 |
| **`[const §VII.7]`** | **Applies.** `peek_fixt_route` and `scan_logon_msg_type_defaults` are new walks over untrusted bytes, and the validator gains an envelope branch. No existing harness drives a FIXT session. A FIXT arm in `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` and an envelope arm in `tests/fuzz/fuzz_wire_validator.cpp`, seeded with R-9's, R-18's and E-10's inputs in the existing corpus directories, replayed by `ctest -L fuzz` in CI, and a ten-minute campaign from the feature's tree, local because no workflow runs one (T135) | Tasks, W-24 |
| **`[const §VII.8]`** | Isolation-sensitive, each its own executable and `add_test`: W-15 (heap count, TSan arm), W-16 (allocation interceptor), W-20 (fail-at-N global `operator new`), W-12 and W-14's TOML live cell (threads and a socket). The rest of W-14 stays in `config_045_tests`. Everything else is isolation-safe: W-1 to W-11, W-19, W-21 and W-25 go in an existing grouped session or dictionary bucket, selected by a `099` label. W-8's Engine is pumped on one thread, and tasks.md T007's process-lifetime route set is immutable, so those cells share no state; T007 owns its cost and `session_pure_tests`' `TIMEOUT` | Tasks |
| `[const §VIII.2]`, `[const §VIII.5]` | paired bench on the hot path; no per-message allocation | W-22, W-16 |
| `[const §VIII.3]` | the same PR carries the benchmark: a new FIXT case in `on_inbound_frame_bench.cpp` | Tasks |
| `[const §IX.1]`, `[const §IX.2]`, `[const §IX.4]` | touched modules at thresholds or a per-line assessment; ASan, UBSan, TSan; clang-tidy, clang-format, cppcheck | At verify |
| `[const §X.1]`, `[const §X.6]`, Appendix A | triggered: ABI surface change, threading, wire format and validator, session FSM. All four controls: `/clarify` done; `/analyze` after tasks; Gate A; the owner's `/plan` sign-off | Sign-off done, 2026-11-03 |
| `[const §X.5]` | one reentrancy token per new or changed symbol; `tools/check_capi_reentrancy.sh` enforces it | W-12 |
| **`[const §X.7]`** | **A declared break.** `fixpp_msg_version`'s struct layout and result change. No public release exists (`gh release list --exclude-drafts`, 2026-11-03), so the bump is MINOR. Its four bullets are tasks: MINOR to 14; BREAKING at the declaration, in `version.h`, the PR description and the B&L delta; every in-repository consumer in the same PR; the four controls. The rest is additive, and the registration refusal is reachable only through the new validation setter, so no other call that succeeded now fails. `fixpp_engine_start`'s new failure and wait need an entry with a default (data-model E-1), which C could not set before this feature, so neither reaches a call that could already be made | Tasks, W-12 |
| `[const §XI.7]`, Appendix A | an immutable object read across session strands is a threading trigger: the same four controls. `Engine::start()` blocking (the list below) and `fixpp_msg_version`'s `thread-safe` token (`[2i §4.6]`, owner item 6) are threading claims under this row, and T140 points here | Sign-off done, 2026-11-03 |
| **`[const §XII.9]`** | **Security-affecting.** Owner item 3 gives the initiator S3's block, and any fault that block answers sits ahead of mTLS: on a direct-constructed initiator, a Logon with a bad 1137 or any block fault of data-model E-10, from an off-list identity, draws a Reject and no authorization event. On an Engine route the reconnect step refuses the identity first, read from source and not driven (research R-18). An acceptor is not spared: `99` on L already draws a Reject with no authorization event at `c0a828ef`, and data-model E-7's `NC-h` gives an absent 1137 the same on every shape, now with a disconnect, Engine route included. The peer learns which tag of its own Logon was refused; an operator's monitoring loses the `auth` event. The same four controls | Sign-off done, 2026-11-03 |
| `[const §XIII.3]` | the role loop's new log line is outside session scope, on a bare strand, so it reads `co_await fixpp::current_trace_context`, as that clause requires of such a path | Tasks |
| `[const §XIV.2]` | `Validator` keeps five pure virtuals; the additions are non-virtual, on `dictionary_driven_validator` | Pass |
| `[const §XV.1]`, `[const §XV.9]` | no per-message heap, in the scope L-093-13 and B-544-1 state: from the first Active, so a Logon is outside it and E-10's storage is reserved in `open()` regardless; no `std::mutex` and no lock on any path this feature adds: the route set is immutable once handed out (data-model E-1), per-connection state is strand-confined (E-6), and a C or Python call keeps its token or GIL row (contracts/surface.md) | W-16, pass |
| `[const §XV.13]` | hybrid dictionary model unchanged; S stays authoritative for its version | Pass |
| `[const §XVI.3]`, `[const §XVI.4]` | `/clarify` ran before this plan; `/analyze` after tasks | Pass, tasks |
| `[const §XVI.6]` | the orchestrator writes no library code; `phase-implementer` does. Mutants in a separate worktree | Tasks |
| `[const §XVII.1]`, `[const §XVII.7]`, `[const §XVII.8]` | Gate A before tasks; the local pre-PR build; `/speckit-verify`, with `$T` substituted for the main checkout its SKILL.md `cd`s to (tasks.md's Execution rules), before Gate B | Pass, pipeline |
| `[const §XX]` | no article changes, so no amendment; the design-document notes are not governed by it (research R-11) | Pass |
| `[arch §2.3]`, `[arch §2.4]` | the registry owns no validator; the route set is in `session`, which may include both; `reify.cpp` is already a bridge file | Pass |

No violation to justify. `[const §X.7]` and `[const §XII.9]` are declared, with their controls scheduled.

## Project Structure

```text
specs/099-fixt-appl-version-validation/
├── plan.md  research.md  data-model.md  quickstart.md  spec.md
├── contracts/{surface,witnesses}.md
└── checklists/{api-abi,concurrency,config,nfr,requirements,test-integrity,wire-session}.md

dictionaries/FIXT11.xml, dictionaries/README.md   (FR-032)
include/fixpp/dict/{version_registry,version_profile,dictionary,reify}.hpp
include/fixpp/wire/{validator,parser}.hpp
include/fixpp/session/{session,session_config,engine}.hpp
include/fix/c_api/{session,engine,message,dict,version}.h
src/dictionary/{version_registry,dictionary,reify}.cpp
src/session/{session,engine}.cpp, session_engine_access.hpp, fixt_route_set.{hpp,cpp}, peek_fixt_route.hpp, fixt_session_check.hpp, logon_msg_type_defaults.hpp
src/log/format_registry.cpp   (the role loops' log line)
src/capi/{config,engine,message_read,message_write}.cpp, capi_internal.hpp
src/config/selector_resolver.cpp
bindings/python/{fixpp.i,tests/,tests/wheel/test_import_surface.py}
tests/session/test_099_*.cpp, tests/wire/test_099_*.cpp, tests/capi/capi_099_*, tests/config/test_099_*
tests/{session,dictionary,wire,capi,config,perf}/CMakeLists.txt   (the `099` label and each new executable)
tests/capi/message_read_test.cpp   (research R-17, the declared break)
tests/support/session_test_access.hpp
tests/session/{test_fixt_logon_establishment,unparseable_frame_disposition_test}.cpp   (research R-17)
tests/codegen/{read_tier_byte_diff_test.cmake,test_082_class_xml_consistency_test.cpp}, specs/003-dictionary-codegen/contracts/golden/vt11_Messages.golden.hpp   (research R-17, FR-032's edit)
tests/dictionary/{reused_tag_census_test,xml_enum_codeset_test,required_scope_census_test}.cpp, tools/quickfix_required_golden/{golden,golden_groups}.csv   (research R-17, FR-032's edit)
tests/perf/   (W-16)  tests/dictionary/   (W-17, W-21)
tests/abi/golden/fixpp_capi_symbols.txt, tests/capi/version_test.cpp, tools/capi_freeze.sha256   (research R-11's recipe)
tests/fuzz/{fuzz_session_recovery_admin_parse,fuzz_wire_validator}.cpp, tests/fuzz/corpus/{session_recovery_admin_parse,wire_validator}/   (W-24)
bench/session/on_inbound_frame_bench.cpp
tests/interop/happy/, tests/interop/conversation/fixt_app_script.yaml, tests/interop/cell_results.yaml, parent phase-9-harness/   (W-18)
.specify/{2b-wire,2c-codegen,2i-capi,api-contract,architecture}.md
spec/, brain/, parent trackers
```

`src/capi/session.cpp` stays out: `register_session` is the change, and its existing `translate` returns the refusal. `src/session/admin_messages.cpp` stays out: `interpret_logon` already takes hooks, and the group has its own scan.

Comments the design falsifies, corrected with the code: `include/fixpp/dict/reify.hpp`'s and `src/dictionary/reify.cpp`'s resolution order; `include/fix/c_api/message.h`'s paragraph naming `fixpp_msg_version`'s `appl_ver_id` member, and its declaration's "tag 8 + optional tag 1137".

## Phases

1. **Dictionary and validator** (E-1a, E-2, E-3, E-4): FR-032's `FIXT11.xml` edit and README; research R-17's inventory for it; W-17, W-21's unit half; the existing validator tests stay green; W-3's unit half; `validator_bench`.
2. **Route set and check** (E-1, E-9), the `{d, S}` tables, `Engine::start()` and its comments in `engine.hpp`, `src/session/engine.cpp` and `src/capi/engine.cpp`, `session_engine_access::adopt_routes`, `register_session`, `open()`, the role loops' log line and its `format_registry.cpp` entry: W-8, W-15, W-20, whose direct arm gains `open()`'s reservation in phase 5.
3. **Peek and selection** (E-6, E-7): W-21.
4. **Arms**: scan; S1 to S3; A1 to A3; dispatch (R-7, E-7); the test changes of research R-17: W-1 to W-7, W-9, W-16, W-19. **W-24's two fuzz arms and their seeds.**
5. **Message Type Default** (E-10), `open()`'s reservation, selection's step 6, the block, S2's list, `reify` taking the byte: W-25.
6. **Accessor** (E-5), including clone and reify: W-10. Outbound pin: W-11.
7. **C ABI, Python, TOML** (E-8), research R-11's inventory and the break's four bullets, the `tools/capi_freeze.sha256` refresh: W-12, W-13, W-14. **Apply the `Updated (099-…)` notes of research R-11 to `.specify/2b-wire.md` and `2c-codegen.md`, and the three symbols, the constants and the break to `2i-capi.md` and `api-contract.md`.**
8. **Live cell**: W-18. Bench: W-22.
9. **#651 comments, records, brain**: W-23. Tasks: S-026 to `done` under research R-16's condition, with its live-evidence sentence; L-041-2, L-033-1, L-097-1 to the closed file under theirs; `coverage-index.md`'s §4.3.7, §5.3 and FIXT-section S-026 wording corrected; L-033-5 and the `open()` guard's comment corrected; the new L-rows and B-rows of quickstart.md.

Phases 1–3 precede 4; 5 follows 4; 6 and 7 are parallel after 5. Mutants run after each phase's witnesses, from a separate worktree.

## Owner sign-off (`[const §X.6]`)

Each item is ruled, on 2026-11-03, in spec.md's last Clarifications session. The owner confirmed every item on 2026-11-03, among them item 3, with 1130 and 372 under it, and item 8a, at the measured cost its row states. Items 14 to 16 were ruled the same day, final, on `/speckit-analyze`'s open items, and are recorded here, not in spec.md.

| # | Item | Ruling | What it relaxes or adds |
|---|---|---|---|
| 1 | Live cell | One peer-to-fixpp 35=BN, no republish first (item 16 says what "first" means) | A republish, if needed, returns to the owner |
| 2 | Application or mismatched frame before Logon | Disconnect, nothing sent | FR-009, FR-025, FR-026's Rejects do not apply in the two establishment arms. Departs from C++'s Reject on an invalid frame, and from J's silent wait |
| 3 | 1137 order | Gate first; a gate Reject naming it on a Logon disconnects; the initiator gains the block | A double fault follows dictionary order. An absent 1137 plus an undeclared tag draws a Reject naming that tag and stays until the Logon timeout. `99` and `abc` keep NextNumIn on L and consume it on T and A. The same holds for 1130 and 372, through FR-031: a gate Reject naming either on a Logon disconnects and keeps NextNumIn, and the block's consumes it. On a direct-constructed initiator any fault S3's block answers sits ahead of mTLS: `[const §XII.9]` |
| 4 | Registration | `Engine::register_session` refuses a validating FIXT session failing the check, as `open()` does | Adds to ruling 2's wording |
| 5 | Non-validating bad config | Refused at `open()` only, logged | An Engine host gets a log line, not a return value (L-row) |
| 6 | `fixpp_msg_version` | `[2i §4.6]`'s three bytes, a declared break, no separate getter | Narrows ruling 3's "additive" for that symbol |
| 7 | Overlay's header pair on another route | Honoured, as QuickFIX | A route table per (S with a header pair, other dictionary); S's pair wins a collision (B-row) |
| 8 | Message Type Default | Implemented, inbound | Beyond every QuickFIX engine. No live cell can witness it. Not implemented: fixpp's own Logon declaring defaults, `385=R`, 1406, 1131 (L-row) |
| 8a | `FIXT11.xml` | The group and three fields added, nothing else | "Verbatim" gets a stated exception. Measured by mutation on the final placement (research R-17): five of 55 distinct test binaries pin the file (`codegen_082_class_xml_consistency_test`, `codegen_determinism_test`, `dictionary_pure_tests`, `dictionary_required_scope_census_test`, `required_scope_parity_test`), a spec-owned golden and a QuickFIX-derived golden among them, plus the four vt11 hashes, and the overlay patch passes. Each fails loudly and passes outside the namespace, and adopting J's whole file would trip the same pins. A compile-time pin on the regenerated headers shows only in a real build |
| 9 | Repeated MsgType | Reject in LogonReceived and Active | A non-validating session keeps the split, and `D…A` with Logon fields still establishes it; LogoutSent takes `D…5` as the reply; a non-FIXT validating session keeps it (to file) |
| 10 | 1128 past the header | Reject 373=14, 371=1128 | A dictionary declaring 1128 in a body would be false-rejected; none vendored does, and W-4 recomputes that |
| 11 | Partial session dictionary | Refused unless `FIXT11.xml` is registered | Ruling 2's wording read strictly; research R-17's tests change |
| 12 | Who judges a routed frame's header | S, every tag it declares except 35, 1128, 1129, 1156 | Matches QuickFIX on T |
| 12a | A header tag S declares, past the body | Judged by S and passes | Departs from QuickFIX's 373=14 (B-row) |
| 13 | A malformed 384 or 385 on a Logon | Stays: `R 5/that tag A`, NextNumIn kept. S2's list stays 1137, 1130 and 372 | A single-field fault like `108=abc`, not one of FR-031's, so `R 5/384 A` has two endings: `MT-p`'s and `MT-e`'s (data-model E-10) |
| 14 | Builds | "Any build, -j8": no asking before each build, `-j8` on all of them, this feature | tasks.md's T001 records it quoted, and `J=${J:-8}` follows the rule in its Execution rules. MSVC takes `.sandbox-lock` first; never a Windows and a Linux build at once. Overrides the `-j2` of the parent's `phases/phase-4/parallel-worktrees.md` for this feature only |
| 15 | Harness landing | After merge, as 095: uncommitted in the parent working copy until the PR merges, a file copy kept, nobody staging a `phase-9-harness/` path before then; one post-merge parent commit with the gitlink, explicit-path staging and an `ls-tree` check; harness tests green, publish digest recorded, matrix re-dispatched | `interop-live` is expected red on the two cells during the PR; it is advisory. tasks.md ports 095's T099 and T100 and its file-copy rule, adapted |
| 16 | Republish | "No republish first" means the pre-merge attempt only. The automatic publish the post-merge commit fires is fine, and its digest is recorded | Narrows item 1 |

Choices made in the design and not put to the owner, each following a ruling or the standard's text: research R-19's list for Message Type Default, research R-20's scope and collision rule for the pair, and research R-4's NoHops metadata, which keeps today's answer on T and L.

For the owner's information, consequences of the design, not choices:

- `Engine::start()` now blocks for the walks on an engine with a FIXT session, about 0.5 s at `-O3` and 3.3 s at `-O0` for FIX50SP2 and FIX Latest (research R-3). A host calling it from a handler on `exec` stalls that thread for as long; its comments change to say so.
- Measured at `c0a828ef`: with validation off, `35=D … 98=0|108=30|1137=9|35=A` establishes a session of either role, because `interpret_logon` reads the last 35. Ruling 1 keeps validation-off unchanged, so 099 discloses it and it is to be filed.
- 370 is treated as a header tag on a FIXT session whatever the route. QuickFIX C++ refuses it (research R-5; B-row).
