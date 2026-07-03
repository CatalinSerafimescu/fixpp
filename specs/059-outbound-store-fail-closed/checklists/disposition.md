# Disposition & Data-Integrity Requirements Checklist: 059-outbound-store-fail-closed

**Purpose**: Validate the QUALITY of the requirements for the outbound store-failure disposition — completeness of the failure taxonomy, clarity of the fatal-vs-tolerate scoping, consistency across spec/plan/research/data-model/contract, and coverage of the policy/edge cases. "Unit tests for the English", not for the code.
**Created**: 2026-07-03
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [research.md](../research.md) · [data-model.md](../data-model.md) · [contracts/store-then-emit-disposition.md](../contracts/store-then-emit-disposition.md)
**Audience/Depth**: Gate-B reviewer / formal release gate.

## Requirement Completeness (is the failure taxonomy fully specified?)

- [x] CHK001 Are requirements defined for EVERY outbound-retain failure class the store can return on the send path — `store_io_failure`, `store_seqnum_out_of_order`, `store_capacity_exhausted`? [Completeness, Spec §FR-004] — PASS: FR-004 lists all three classes and states the classifier is durability, not error code; data-model row 3 and contract §2/Error-channel table enumerate identically; research D1 rationale confirms fail-closed is uniform across all three (rejects a per-error-code split).
- [x] CHK002 Is the disposition of the cancellation-class `store_cancelled` return explicitly specified (excluded from fail-closed) rather than left implicit under "all failures"? [Completeness, Spec §Edge Cases · research §D7] — PASS: spec Edge Cases ("Cancellation is not a store failure") + FR-005 + data-model row 2 + contract §2 all state `store_cancelled` is checked and excluded BEFORE the persistence gate; research D7 sources it to the two `store()`-path return sites, both verified at `memory_store.hpp:145` / `file_store.cpp:1028`.
- [x] CHK003 Are requirements specified for BOTH store-durability arms (persistent → fail-closed; volatile → logged-then-proceed), and is the classifier (store durability, not error code) stated? [Completeness, Spec §FR-001/FR-003/FR-004] — PASS: FR-001 (persistent), FR-003 (volatile), FR-004 (classifier = durability) are each explicit; `store_is_persistent_` capture verified at `session.cpp:1154` (single capture at `open()`, matches A-1).
- [x] CHK004 Is the retain-before-transmit ordering requirement (fatal decision precedes any transmit) documented as a normative MUST, not just described? [Completeness, Spec §FR-002 · data-model INV-059-2] — PASS: FR-002 uses "MUST NOT be transmitted... precedes transmission"; data-model INV-059-2 restates as an invariant; contract §3 codifies the same ordering in the post-condition list.
- [x] CHK005 Are the reconnect-recovery requirements specified for all three sequence-reset policies (plain / `reset_on_logon` / `bilateral_strict`)? [Coverage, Spec §US3 · quickstart W3] — PASS: US3 AC1/AC3/AC4 cover plain / `reset_on_logon` / `bilateral_strict` respectively; quickstart W3 Variants A/B/C mirror the same three; data-model INV-059-3 states the policy-scoped outcome for each.
- [x] CHK006 Is the documentation-amendment requirement (L-008-2 narrowed to volatile; persistent leg fails closed) captured as a deliverable requirement, not just a note? [Completeness, Spec §FR-009] — PASS: FR-009 is a normative MUST; data-model "Documentation deltas" table ties the L-008-2 edit to FR-009 and `tasks.md` T014 schedules the edit at `/implement` (not yet landed — expected pre-implementation; confirmed current `behaviors-and-limitations.md:190` still reads the pre-fix volatile-only wording, consistent with Gate-A-done/pre-implement status).

## Requirement Clarity (is each term measurable / unambiguous?)

- [x] CHK007 Is "fatal-when-persistent" defined concretely (transition to Disconnected + propagate the error + do-not-transmit), not left as a vague adjective? [Clarity, Spec §FR-001] — PASS: FR-001 spells out all three components verbatim (transition, propagate via the same return channel, precede-transmit) plus who owns the transition (send caller, not the retain step).
- [x] CHK008 Is "the session fails closed" expressed as an observable end-state (Disconnected AND un-retained frame absent from the wire), so it is objectively checkable? [Measurability, Spec §US1 AC1 · SC-002] — PASS: US1 AC1 states both conjuncts explicitly ("transitions to disconnected AND the failing message is NOT transmitted"); SC-002 restates as the GREEN criterion; quickstart W1 GREEN section gives the concrete assertion (transport saw nothing for `k`).
- [x] CHK009 Is the US3 "resume cleanly / from the durable counter" requirement quantified (next stamped seq == durable store counter == peer-last-seen+1) rather than described qualitatively? [Clarity, Spec §FR-007 · data-model INV-059-3] — PASS: data-model INV-059-3 states the quantified equality `peek_outbound() == durable-store-counter == peer-last-seen + 1`; US3 AC2 restates the same equality in prose.
- [x] CHK010 Is the volatile-store "unchanged" requirement pinned to an objective baseline ("byte-for-byte as `main`"), not "behaves the same"? [Measurability, Spec §FR-003 · SC-003] — PASS: FR-003 says "continue to behave exactly as on `main` today"; SC-003 says "byte-identical behaviour to `main`" and additionally requires existing volatile-store tests stay unchanged and passing — an objective regression baseline.
- [x] CHK011 Is the boundary of the `bilateral_strict` (default policy) case stated precisely — bounded by pre-existing L-029-3, with US3 clean-recovery NOT claimed there? [Clarity, Spec §Clarifications · quickstart W3 Variant C] — PASS: Clarifications + US3 AC4 state the boundary precisely (default policy, `34=k` with `141=Y`, pre-existing deferred L-029-3, "clean recovery NOT claimed"); quickstart W3 Variant C repeats the same boundary as a regression-guard, not a clean-recovery assertion.

## Requirement Consistency (do the artifacts agree?)

- [x] CHK012 Does the disposition decision table (data-model) agree with the FRs (spec) and the contract on all six rows (success / `store_cancelled` / persistent-genuine / volatile-genuine / thrown-abort / other-throw)? [Consistency, data-model §Disposition table · contract] — PASS: cross-checked row-by-row — data-model's 6 rows map 1:1 to contract §"Retain step post-conditions" items 1–6 and to FR-001/002/003/004/005/007/008 by requirement column; no row mismatch found.
- [x] CHK013 Is the "no internal FSM transition inside `store_then_emit`; caller-owned transition (transport-failure parity)" statement consistent everywhere it appears (research D3, data-model, contract, plan)? [Consistency, research §D3] — PASS: identical framing verified in spec.md FR-001 ("owned by the send caller... not fired from inside the retain step"), plan.md summary step 1, research D3, data-model row 3/table, and contract §3 — no contradicting statement found anywhere in the bundle.
- [x] CHK014 Is the reconcile mechanism described identically across artifacts as a targeted `set_next_outbound(durable_k)` that does NOT touch `hydrated_` (no residual "re-hydrate / clear latch" framing)? [Consistency, research §D4 · data-model row 1] — PASS: targeted grep for `hydrat|callee-owned|re-hydrat|marked for re-hydration` across spec/plan/research/data-model/quickstart/contracts returned only (a) explicit negative/correct statements ("does NOT clear hydrated_", "NOT touched", "NOT re-seeded via hydrate()"), (b) historical Gate-A round-1 resolution notes describing the REJECTED design as history (plan.md RC#1/RC#3, research D4 "Why NOT clear hydrated_"/"Alternatives rejected"), and (c) unrelated existing-mechanism descriptions (`ensure_hydrated_`/`persist_inbound_advance_` factual references). No live/prescriptive residual of the rejected design found.
- [x] CHK015 Do the requirements consistently assert "no new public API / wire / error code / layout change" (A-4), with the reused error enumerators named? [Consistency, Spec §A-4 · plan Constitution Check] — PASS: spec A-4 states it with the three reused enumerators named (`store_io_failure`/`store_seqnum_out_of_order`/`store_capacity_exhausted`); plan Constitution Check Art. X row says "N/A — no `c_api.h` change, no abidiff delta"; contract "Error channel" table names all four reused codes (incl. `store_cancelled`) and states "no additions".

## Acceptance Criteria Quality (are the success criteria verifiable?)

- [x] CHK016 Does SC-001 specify a RED-on-pre-fix cascade that is objectively falsifiable (post-k loss + stuck durable counter + restart-recovers-below), including proof the fault was actually injected? [Measurability, Spec §SC-001 · quickstart W1] — PASS: SC-001 lists all three falsifiable conjuncts; quickstart W1 step 0 requires asserting the seam's probe count fired before asserting the cascade (V5 "RED-for-the-right-reason"), so a miswired/no-op seam cannot pass as RED. This is a test-design requirement, correctly not yet executed pre-implementation.
- [x] CHK017 Does each user story carry an Independent Test that can validate it in isolation (US1 fail-closed, US2 volatile-unchanged, US3 reconcile)? [Acceptance Criteria, Spec §US1–US3] — PASS: US1/US2/US3 each carry an explicit "Independent Test" field naming a self-contained scenario; quickstart W1/W2/W3 realize each 1:1.
- [x] CHK018 Is SC-006 (every caller dispositioned) expressed as a checkable claim (an enumerated census with a pass rule), not an open-ended "all callers handled"? [Measurability, Spec §SC-006 · research §D3 census] — PASS: SC-006 states the exact pass rule (26 sites, classified into 9 broad-guard / 1 narrow-guard / 16 swallow); research D3 and the contract's "Caller contract" section give the full line-number census (`9305e69`) for all 26, independently re-derived and confirmed at Gate-A round 2 by both reviewers (per `.specify/decisions/059-outbound-store-fail-closed-gatea.md`).

## Scenario & Edge-Case Coverage (are boundary requirements defined?)

- [x] CHK019 Are requirements defined for the in-session-reconnect vs process-restart distinction (both must end consistent with the durable counter)? [Coverage, Spec §US1 AC2 · US3] — PASS: US1 AC2 covers process-restart (recovered durable counter vs peer-last-seen), US3 covers in-process reconnect (resume from durable counter); both explicitly distinguished by name throughout spec/research/quickstart.
- [x] CHK020 Is the best-effort-reconcile-read-failure path specified (read fails → skip reconcile, still fail-closed — degrade to safe repeating-disconnect, not a wedge)? [Edge Case, data-model row 3a] — PASS: data-model row 3 sub-step (b) + the paragraph below the table state verbatim "if the durable read fails, the disconnect still occurs (degrades to the safe repeating-disconnect, not a wedge)"; contract §3 restates "Reconcile failure is non-fatal — the caller-owned disconnect still proceeds." quickstart flags this as one of two hard-to-witness coverage arms for `/tasks` (T013), appropriately deferred to implementation, not a spec gap.
- [x] CHK021 Is the requirement to capture the store error BEFORE the reconcile read specified, so a reconcile-time abort cannot pre-empt the returned code? [Edge Case, research §D2 capture-before-reconcile] — PASS: research D2 "Capture-before-reconcile (NEW-P3)" is explicit and gives the failure mode it prevents (a shutdown-race throw during the reconcile read pre-empting the returned code); data-model row 3(a) and contract §3 both order "capture `err` first" before the reconcile read.
- [x] CHK022 Are the best-effort / swallow-and-continue caller sites' behaviour requirements stated (their disposition is the pre-existing transport-failure behaviour, in-scope-to-document not to re-engineer)? [Coverage, contract §Caller contract] — PASS: FR-006 states this explicitly ("Sites that intentionally swallow the return... exhibit the identical post-swallow behaviour... a pre-existing disposition... left as-is"); contract §Caller contract + research D3 give the full 16-site list with worked examples (`:4963`, `:2656`/`:2680`, `:3176`).
- [x] CHK023 Is the app-veto exclusion specified for the broadened `Session::send` guard (an `app_do_not_send`/`app_payload_malformed` must NOT be treated as fail-closed)? [Edge Case, research §D2 · quickstart coverage] — PASS: research D2 "Consequence for the narrow-guard caller" and the contract's caller-contract entry both state the broadened guard keeps app-veto codes non-fatal; quickstart "Cross-cutting gates" mandates a discriminating witness for exactly this (app-veto leaves session `Active`); source-verified the app-veto returns occur pre-store at `session.cpp:4463-4469`, structurally unreachable by the store-class guard broadening.

## Non-Functional Requirements

- [x] CHK024 Is the "no hot-path perf regression" requirement stated with a rationale (happy path byte-identical; failure path cold), so no bench is silently owed? [NFR, plan §Technical Context · Art. VIII] — PASS: plan Technical Context "Performance Goals" states the rationale explicitly (happy path byte-identical, new branch only on failure return, no new alloc/lock); plan Constitution Check Art. VIII row concludes "No bench needed" with the same rationale; quickstart repeats it as a cross-cutting gate.
- [x] CHK025 Are the sanitizer/thread-safety requirements specified (store path is strand-confined + `async_mutex`-guarded; tests run on `thread_pool(≥2)`)? [NFR, plan §Constitution Check Art. IX/XI · quickstart] — PASS: plan Constitution Check Art. IX (Tier-1 ASan/UBSan/TSan) and Art. XI §5 (store-write path always mutex, unchanged) both present; quickstart "Cross-cutting gates" mandates `thread_pool(≥2)` for the store-failure tests, citing the applicable lesson (`feedback_single_threaded_harness_masks_strand_races`); tasks.md T005/T015 carry the same requirement into test execution.

## Dependencies & Assumptions (documented and validated?)

- [x] CHK026 Is the assumption that the `store_is_persistent_` durability signal is reliable (single capture at open, false for MemoryStore/null) documented as a dependency? [Assumption, Spec §A-1 · research grounded facts] — PASS: spec A-1 states it; research "Grounded facts" gives the sourced detail (`session.cpp:1154`, `false` for `MemoryStore`/null-store, `true` for `FileStore`) — source-verified: `store_is_persistent_ = cfg_.store_factory->yields_persistent_store();` at exactly `session.cpp:1154`.
- [x] CHK027 Is the reachability assumption (a durable-store retain failure — ENOSPC/EIO — is an ordinary operational fault) stated so the fail-closed disposition's production relevance is justified? [Assumption, Spec §A-2] — PASS: spec A-2 states this explicitly; the perf-investigation finding (`outbound-store-retention-review.md` P1-1 "Reachability") independently corroborates it as an ordinary operational fault, not contrived.
- [x] CHK028 Is the scope boundary explicit — S-P1-1 + S-P2-1 only; S-P2-2 / S-P3 / other clusters excluded — so no adjacent hardening is silently assumed in scope? [Assumption, Spec §A-5] — PASS: spec A-5 names S-P2-2/S-P3/other-clusters as explicitly out; Edge Cases "Non-send store failures out of scope" restates the boundary (resend-read/`retrieve`, reset handling, mid-traversal reset guard); tasks.md "Scope" line repeats the same boundary.

## Ambiguities & Conflicts

- [x] CHK029 Is there any residual conflict between the L-008-2 amendment (persistent now fails closed) and the still-open L-029-3 bound under `bilateral_strict` — are the two limitations clearly delimited so they don't read as contradictory? [Conflict, Spec §FR-009 · quickstart W3 Variant C] — PASS: no contradiction — L-008-2 governs the retain-failure disposition (volatile vs persistent, this feature's subject); L-029-3 governs a distinct pre-existing cold-open Logon malformation under `bilateral_strict` unrelated to retain failures. Clarifications/A-3/US3 AC4/quickstart Variant C all state 059 "does not worsen" L-029-3 and explicitly does NOT claim to fix it — delimited, not contradictory. Both anchors independently spot-verified: L-008-2 at `spec/behaviors-and-limitations.md:190`, L-029-3 at `:1252-1265`.
- [x] CHK030 Does any requirement still imply the rejected "callee-owned `Disconnected`" or "clear `hydrated_`" design (a stale-framing conflict that would mislead implementation)? [Conflict, research §D3/D4 — must be absent] — PASS (confirmed absent): exhaustive grep for `hydrat|callee-owned|re-hydrat|marked for re-hydration|record_state_transition_` across spec.md/plan.md/research.md/data-model.md/quickstart.md/contracts/*.md, every hit checked for polarity. All hits are either (a) explicit rejections of the design ("Why the original design was wrong", "Alternatives rejected — Clear `hydrated_`"), (b) correct final-design statements ("NOT touched", "does NOT clear"), or (c) factual references to unrelated existing mechanisms (`persist_*_advance_`, `ensure_hydrated_`, the 16 pre-existing swallow-site continuations). No live/prescriptive residual of the rejected design found anywhere in the bundle. This closes the Gate-A RC#3 finding.

## Notes

- Check items off as the requirement quality is confirmed: `[x]`. This checklist is audited by `/checklist-audit` (pipeline step 9) which dispositions each item (SPEC-FIXED / DD-DECIDED §X / WAIVED) and blocks `/speckit-implement` until clear.
- Traceability: ≥80% of items carry a `[Spec §…]` / `[research §…]` / `[data-model …]` / `[Gap]` / `[Conflict]` reference.

## Audit Result

**Audited**: 2026-07-03. **Auditor context**: this is a requirements-QUALITY audit performed after Gate A sign-off but BEFORE `/speckit-implement` — `(void)store_r;` is still live in `src/session/session.cpp:4790` and `behaviors-and-limitations.md:190` still reads the pre-fix volatile-only L-008-2 wording. That is the expected pre-implementation state, not a defect; every item below was dispositioned against requirement quality (completeness/clarity/consistency/coverage), never against whether code/doc edits have landed yet.

| Disposition | Count |
|---|---|
| PASS | 30 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 30 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None. All 30 items' answers are stated directly in the audited spec.md/research.md/data-model.md/contract bundle (spec.md is not silent on any item that would otherwise require deferring to an external frozen-authority doc), so PASS (not DD-DECIDED) is the correct disposition throughout. Traceability to the Gate-A convergence record (`.specify/decisions/059-outbound-store-fail-closed-gatea.md`) is noted as corroborating evidence in CHK018/CHK030 but is not load-bearing — the same facts are independently present in the audited artifacts.

### WAIVED items

None. No Completeness/Clarity/Consistency item required a waiver, and no non-Completeness/Clarity/Consistency item needed one either — all 30 items resolved to PASS.

### Realizability sub-check

`data-model.md:1-3` states explicitly: "No new data types. This feature changes a *disposition* (control flow over existing state)." Confirmed by full read of data-model.md/contracts/store-then-emit-disposition.md: the change is entirely control-flow over pre-existing state (`SeqnumManager::next_outbound_`, the store's own counter, `store_is_persistent_`, `hydrated_`, FSM state) — no new struct/class is introduced, held, or returned by value. **Verdict: N/A** — the forward-declared-incomplete-type / defaulted-special-member trap this sub-check guards against does not apply to this feature.

### Anchors spot-verified (all resolve in the signed-off revision)

- `spec/behaviors-and-limitations.md:190` — L-008-2 (bounded `MemoryStore` past capacity, wontfix) — resolves.
- `spec/behaviors-and-limitations.md:1252-1265` — L-029-3 (`bilateral_strict` cold-open malformed Logon, documented/DEFERRED) — resolves.
- `src/session/session.cpp:4734` — `Session::store_then_emit` definition — resolves.
- `src/session/session.cpp:4790` — `(void)store_r;` swallow (pre-fix, as expected) — resolves.
- `src/session/session.cpp:4046` — `Session::send`'s `== dispatch_aborted` narrow guard (the one residual site) — resolves.
- `src/session/session.cpp:4463-4469` — app-veto (`app_do_not_send`/`app_payload_malformed`) returns, pre-store — resolves (cited range `:4468-4470` in the bundle is accurate to within a line).
- `src/session/session.cpp:1154` — `store_is_persistent_` single capture at `open()` — resolves exactly.
- `src/session/session.cpp:776`, `:816` — `cfg_.reset_on_logon` gate and `initr_reset_seqnum` computation — resolve exactly.
- `include/fixpp/session/seqnum_manager.hpp:140` — `SeqnumManager::set_next_outbound(n)` — resolves exactly.
- `include/fixpp/session/session_config.hpp:231`, `:251` — `bilateral_strict` default / `reset_on_logon = false` default — resolve exactly.
- `include/fixpp/session/memory_store.hpp:145`, `src/session/file_store.cpp:1028` — the two `store_cancelled` return sites (both on the `!async_lock()` / drain path) — resolve exactly.
- `.specify/decisions/059-outbound-store-fail-closed-gatea.md` — Gate A convergence record, `gate-a-done`, 2-round Codex+Opus, RC#1/RC#2/RC#3 all addressed — resolves, `.specify/` local-only as expected.
- `phases/phase-9/perf-investigation/findings/outbound-store-retention-review.md` — P1-1/P2-1 source finding — resolves.

**Design-doc revision**: this feature has no Phase-2 anchor doc; the authoritative design is `research.md` (D1–D7, Gate-A converged 2026-07-03) + the Gate-A convergence record above — both verified current against the reviewed tree (submodule HEAD `9305e69` at time of research, no drift found against the live tree at audit time for any anchor checked above).

### CodeGraph / source lookups performed

All symbol verification for this audit was done via direct source `grep`/`Read` against the live tree (stronger than an existence search — full signatures and call sites confirmed), not via the `mcp__codegraph__*` tools: `Session::store_then_emit`, `Session::send`, `SeqnumManager::set_next_outbound`/`peek_outbound`/`reset_to_one`, `store_is_persistent_`/`yields_persistent_store`, `store_cancelled` return sites, `reset_seqnum_policy`/`reset_on_logon` defaults, app-veto (`app_do_not_send`/`app_payload_malformed`) return sites. All confirmed to exist with the shape the spec bundle claims.

### Re-run `/speckit-analyze`?

**NO.** Zero `SPEC-FIXED` dispositions were applied — no artifact was edited during this audit. The prior `/speckit-analyze` (referenced in plan.md Constitution Check as "scheduled") remains valid; nothing invalidates it here.

### Verdict: GREEN

Pipeline.md step 9 satisfied. All 30 `disposition.md` checklist items disposed (`[x]` + inline tag), zero un-dispositioned boxes, zero Completeness/Clarity/Consistency item WAIVED, every cited anchor spot-verified. `/speckit-implement` (step 10) may proceed.
