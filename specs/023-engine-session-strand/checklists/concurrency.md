# Concurrency Requirements Quality Checklist: Per-Session + Control-Plane Strand Binding

**Purpose**: Unit-test the *requirements* (spec.md) of this concurrency feature for completeness, clarity, consistency, measurability, and coverage on the serialization / memory-model / lifetime / shutdown-ordering axes — BEFORE `/speckit-implement`. Tests whether the requirements are well-written, not whether the code works.
**Created**: 2026-06-05
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [contracts/engine-session-strand.md](../contracts/engine-session-strand.md)
**Audience / depth**: Gate-B reviewer; formal release gate (Art. XI §7 concurrency-trigger feature)

> Disposition convention (consumed by `/speckit-checklist-audit`): each item → PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>. `[Gap]`/`[Ambiguity]`/`[Conflict]`/`[Assumption]` mark the quality dimension under test.

## Requirement Completeness — serialization domains

- [ ] CHK001 Are requirements defined for **both** serialization domains (per-session AND engine control-plane), with each domain's protected state enumerated rather than left as "engine-global state"? [Completeness, Spec §FR-001/§FR-011]
- [ ] CHK002 Is the **complete set** of per-session work that must run in the session domain enumerated (establishment, handshake, read/framing, dispatch, send, BOTH teardown closes), or could a reader omit one? [Completeness, Spec §FR-001/§FR-002]
- [ ] CHK003 Is the **complete set** of control-plane state enumerated (registry, stopping flag, listener table, endpoint table, counters, per-session-handle publication), so no fifth structure silently escapes the domain? [Completeness, Spec §FR-011; research R4 half-restructure]
- [ ] CHK004 Are requirements stated for **every cross-thread entry point** (any-thread send, shutdown, and the synchronous public readers), or only for send/stop? [Completeness, Spec §FR-012/§FR-014]
- [ ] CHK005 Is the publication of the per-session handles the shutdown path reads (`session`/`live_transport`) specified as a requirement, including its ordering relative to the read pump? [Completeness, Spec §FR-011; edge case "Shutdown vs connection accept"]
- [ ] CHK006 Are requirements defined for the transport I/O object's executor binding (not just the role loop), so the socket cannot silently carry a bare executor? [Completeness, Gap risk — research D5/R8; not surfaced as an FR]

## Requirement Clarity — vague terms quantified

- [ ] CHK007 Is "**serialization domain**" defined precisely enough (one boundary, no two operations overlap) to be objectively checkable, rather than an abstract synonym for "strand"? [Clarity, Spec §Key Entities]
- [ ] CHK008 Is "**non-blocking handoff**" defined (a post that never occupies/blocks a domain) so FR-006's deadlock-freedom claim is testable, not just asserted? [Clarity, Spec §FR-006]
- [ ] CHK009 Is the `stopped_` "**defined cross-thread access discipline**" stated concretely (atomic acquire/release OR control-domain read), not left as "must not remain a plain flag"? [Clarity, Spec §FR-013]
- [ ] CHK010 Is the snapshot-read cost characterization precise — does the spec avoid an absolute "lock-free" guarantee and instead state the bounded "wait-free where the STL makes it lock-free, else non-`std::mutex` STL-internal" property it can actually hold? [Clarity, Spec §FR-014]
- [ ] CHK011 Is "**bounded handle**" defined with its exact validity boundary (valid across `stop()`/registry-clear *while the Engine is alive*; NOT past `~Engine`), so the precondition is unambiguous to a caller? [Clarity, Spec §FR-008/§FR-014]

## Requirement Consistency

- [ ] CHK012 Does FR-011's "all control-plane state … mutated only within the control domain" stay consistent with FR-014's "readers read an immutable snapshot off any thread" — i.e. is the mutate-on-strand / read-via-snapshot split stated without contradiction? [Consistency, Spec §FR-011↔§FR-014]
- [ ] CHK013 Is the single permitted public change (`lookup() → shared_ptr<Session>`) described **identically** wherever it appears (FR-008, SC-004, Out of Scope, US3), with no second public change implied anywhere? [Consistency, Spec §FR-008/§SC-004/§Out-of-Scope]
- [ ] CHK014 Is `stopped()`'s discipline consistently attributed to the atomic flag (FR-013) and explicitly **excluded** from the snapshot (FR-014), so the two readers' mechanisms don't blur? [Consistency, Spec §FR-013↔§FR-014]
- [ ] CHK015 Do the two lifetime mechanisms (send-path keepalive/drain vs the `lookup()` bounded-handle debug assert) stay **distinct** in the requirements, or could they be read as one unified keepalive? [Consistency, Conflict risk — research R7; not pinned in an FR]

## Acceptance Criteria Quality — measurability

- [ ] CHK016 Is the SC-002 deterministic-witness standard stated as an objectively decidable bar (a one-sided park → TSan race on **every** pre-change run, GREEN 100% post-change) rather than "deterministic" left undefined? [Measurability, Spec §SC-002]
- [ ] CHK017 Does SC-002 explicitly **forbid** the self-defeating reproducer (a bidirectional latch that creates a happens-before edge and suppresses the race), so the criterion can't be met by a witness that proves nothing? [Measurability, Spec §SC-002; research D6]
- [ ] CHK018 Is SC-004's "no unintended API/ABI change" measurable via a named diff tool expectation (exactly one recorded `lookup()` diff, no other), not a subjective "no surprising change"? [Measurability, Spec §SC-004]
- [ ] CHK019 Is SC-001's "**across repeated runs**" quantified (a minimum repetition count) or does it leave "repeated" to interpretation? [Measurability, Spec §SC-001 — Ambiguity]
- [ ] CHK020 Are the perf acceptance bounds (±5%) tied to the **two-hop** send path and the snapshot read/publish cost specifically, so the criterion measures the paths this feature actually changed? [Measurability, Acceptance — plan §Perf Goals/V-6; not an SC in spec.md]

## Scenario Coverage — concurrency scenario classes

- [ ] CHK021 Are **steady-state** MT requirements covered (round-trip + ordering correct, no two same-session ops overlap)? [Coverage, Primary — Spec §US1 AS-2/§FR-005]
- [ ] CHK022 Are **teardown-vs-in-flight-read** requirements covered (close serialized with the completing read)? [Coverage, Exception — Spec §Edge Cases/§FR-002]
- [ ] CHK023 Are **teardown-vs-in-flight-send** requirements covered (shutdown overlapping an outbound send for the same session)? [Coverage, Exception — Spec §Edge Cases]
- [ ] CHK024 Are **re-entrant send** requirements covered, including the post-`stop()` clean-fast-fail disposition (not only the no-deadlock happy path)? [Coverage, Recovery — Spec §Edge Cases/§FR-006]
- [ ] CHK025 Are **cross-session parallelism** requirements covered as an explicit non-guarantee (unrelated sessions still progress; serialization is per-session, not engine-global)? [Coverage, Spec §FR-004]
- [ ] CHK026 Are **shutdown-vs-connection-accept** control-plane requirements covered (registry/listener clear vs accept-loop publish)? [Coverage, Exception — Spec §Edge Cases/§FR-011]
- [ ] CHK027 Are **any-thread-send-vs-shutdown** requirements covered (resolve registry inside the domain, fail cleanly if shutdown began, never read a half-cleared registry)? [Coverage, Exception — Spec §Edge Cases/§FR-012]
- [ ] CHK028 Are **synchronous-reader-vs-shutdown** requirements covered (consistent immutable snapshot; a handle taken just before clear keeps its session alive)? [Coverage, Exception — Spec §Edge Cases/§FR-014]
- [ ] CHK029 Are **idle-established-session-at-shutdown** requirements covered (close-to-wake preserved, now inside the session domain)? [Coverage, Edge Case — Spec §Edge Cases/§FR-002]

## Edge Case & Ordering Coverage

- [ ] CHK030 Is the **stop-before-publish** ordering hole covered as a requirement (a role loop reaching its publish *after* `stop()` began must take a stopped disposition and not pump), or only the publish-before-stop direction? [Coverage, Spec §Clarifications r3 / contract V-12; verify it reads as a requirement, not only a witness]
- [ ] CHK031 Is the post-join terminal-close ordering specified (terminal close **after** join + send-drain, **before** registry clear) so the documented liveness-loop-drain is preserved? [Completeness, Spec §FR-002; data-model E-4 INV-4b]
- [ ] CHK032 Are requirements stated for the publication handles' release on **every** loop-exit path (normal/cancel/error), or only normal exit? [Edge Case, Gap — implied by FR-011 publication; not explicit in spec.md]
- [ ] CHK033 Is the snapshot-staleness boundary specified (republish after **every** control-plane mutation), so a reader cannot observe a permanently-stale snapshot? [Coverage, Spec §FR-014]

## Memory-Model & Lifetime Requirements

- [ ] CHK034 Does the spec state the memory-ordering requirement for the stopping flag (acquire/release) rather than leaving ordering implicit? [Completeness, Spec §FR-013]
- [ ] CHK035 Is the `lookup()`-handle-vs-`~Engine` UAF hazard documented as a requirement (the handle borrows engine-owned config; must not outlive the Engine), with a debug-assert obligation? [Completeness, Spec §FR-008/§FR-014]
- [ ] CHK036 Is the debug `~Engine` zero-outstanding-handle check specified at the level of *what property it proves* (no outstanding returned handle) rather than a mechanism that cannot hold it (a bare snapshot `use_count()`)? [Clarity, Spec §FR-014; research round-4/5 lease correctness]
- [ ] CHK037 Are the two lifetime mechanisms' **release-build** behaviors specified (lease/counter compiled out; bounded-handle contract then by caller obligation), so there's no implied release-mode overhead or enforcement? [Completeness, Spec §FR-014]

## Non-Functional Requirements

- [ ] CHK038 Are single-threaded backward-compatibility requirements measurable (existing suite green, **no rewrites**, behavior identical) and bounded to the one recorded API delta? [Measurability, Spec §FR-007/§SC-003/§US3]
- [ ] CHK039 Is the limitation-lift (multi-threaded "supported") gated in the requirements on the **exact full witness set** under a clean sanitizer matrix — and does the spec name that exact set rather than "the witnesses"? [Completeness, Spec §FR-010/§SC-005; exact-set guard]
- [ ] CHK040 Are sanitizer findings (ASan/UBSan/**TSan**) named as the acceptance vocabulary for the correctness criterion, consistent with the documented content-quality exception? [Consistency, Spec §FR-003/§SC-001; requirements.md exception]

## Dependencies & Assumptions

- [ ] CHK041 Is the assumption that the **default config already selects per-session strand** (so no new configuration is required for safe MT behavior) stated and validated? [Assumption, Spec §Assumptions/§FR-008]
- [ ] CHK042 Is the assumption that **both domains are strands over the same executor with only non-blocking handoffs** documented as the basis for the no-deadlock claim? [Assumption, Spec §Assumptions/§FR-006]
- [ ] CHK043 Is the reuse of the **existing strand primitive for both domains** (no new lock/abstraction) stated as a requirement, not just an implementation choice? [Dependency, Spec §FR-009]
- [ ] CHK044 Is the `direct_executor` opt-out's MT-safety explicitly placed **out of scope** so its exclusion isn't read as an unmet requirement? [Assumption, Spec §Out of Scope/§Assumptions]

## Ambiguities & Conflicts

- [ ] CHK045 Is there any residual wording that still implies "no public API change" (pre-round-2) conflicting with the now-accepted `lookup()` safening? [Conflict, Spec §FR-008 history]
- [ ] CHK046 Does the spec avoid implying the snapshot uses the project's unshipped `atomic_shared_ptr` (whose `std::mutex` fallback is barred from the awaitable header), pinning instead the standard primitive? [Ambiguity, Spec §FR-014; research D-SNAP / Art. XV]
- [ ] CHK047 Is "deterministic" used consistently to mean "RED on every pre-change run via a one-sided park" and never to imply a stronger two-sided-latch guarantee the seam cannot provide? [Consistency, Spec §SC-002/§US2]

## Notes

- This checklist tests the **requirements**, not the implementation. Items are dispositioned by `/speckit-checklist-audit` (the mandatory gate before `/speckit-implement`).
- Traceability: ≥80% of items cite a spec section or a `[Gap]`/`[Ambiguity]`/`[Conflict]`/`[Assumption]` marker. Items flagging a quality concern that is resolved in plan/research/contracts but **not** in spec.md (CHK006/015/020/030/032) are the highest-signal audit candidates — confirm the spec is the authority or record a DD-DECIDED pointer.
- Sibling checklist: [requirements.md](./requirements.md) (spec-quality, green).
