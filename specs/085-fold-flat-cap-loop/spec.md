# Feature Specification: Fold the Flat Per-Instance Cap Loop into the Nesting-Aware Traversal

**Feature Branch**: `085-fold-flat-cap-loop`

**Created**: 2026-08-03

**Status**: Draft

**Input**: User description: "Fold the redundant flat per-instance cap loop in `OffsetTable::group()` (`src/wire/offset_table.cpp:584-594`) into the nesting-aware `consume_group_extent` traversal. Resolves GitHub issue #214 — leg 2 of the deferred fix recorded in `spec/behaviors-and-limitations.md` L-063-4."

**Closes**: fixpp#214.

---

## Context — what the two loops are and why one of them is redundant

A repeating group's **delimiter** is the tag that opens every instance. `OffsetTable::group(no_tag)` answers two questions about such a group: *where does its byte extent end* and *does any single instance exceed `max_group_entries_per_instance`* (a defence-in-depth DoS cap). Today it answers them with **two separate walks over the same entries**.

**Walk 1 — `consume_group_extent` (`src/wire/offset_table.cpp:442-528`), nesting-aware.** It consumes exactly `declared` instances, descending into any member tag that itself heads a group in the child membership context (`:491-499` at the instance-opening delimiter, `:510-518` for later members), and it **already applies the per-instance cap** at `:521-524`, setting `overflow` on breach. `group()` calls it at `:575` and converts `overflow` into `err_group_too_large` at `:576-578`.

**Walk 2 — the flat cap loop (`:584-594`).** It re-walks the same `[first, group_end]` range with a **flat** boundary test — `entries_[k].tag == delim` at `:586`, no notion of nesting depth — and applies the same cap at `:591`. This is the one remaining flat, wire-derived instance-boundary rule inside `group()`.

`group()` is not a cold path: `group_slices_status` calls it at `:667` for every group materialization, so Walk 2 runs on every group access that reaches the dictionary path.

### Why Walk 2 cannot fire when Walk 1 ran — the redundancy argument

This feature's central claim is that on the dictionary path Walk 2 is **provably unreachable as an error return**, not merely "usually right". The argument has four steps, each checkable at source:

1. **Both walks use the same `delim`.** `group():551` computes `delim = entries_[first].tag` with `first = count_idx + 1U` (`:545`). `consume_group_extent():458` computes `delim = entries_[first].tag` with the same `first` (`:450`). Identical value, same frame.
2. **Every boundary Walk 1 recognises, Walk 2 also recognises.** Walk 1 opens an instance only at an index whose tag equals `delim` (the `while` condition at `:477`). Walk 2 fires a boundary at *every* `k > first` with `entries_[k].tag == delim`, plus at `k == group_end`. So Walk 2's boundary set is a **superset** of Walk 1's on `[first, group_end]`.
3. **Therefore every Walk 2 segment is a subdivision of some Walk 1 instance**, and `inst_count` at `:590` is always **≤** the `(k - inst_start)` that `:521` measured for the enclosing instance.
4. **Walk 1 runs first and returns early on breach.** If any Walk 2 segment exceeded the cap, the Walk 1 instance containing it exceeded it too, so `group()` already returned `err_group_too_large` at `:577` and `:584` was never reached.

The subdivision in step 3 is not hypothetical: on the **485 contexts** where the outer delimiter is itself a nested group's count tag (083 leg (c) — FIX50SP2 240 + Orchestra 245), Walk 2 fires boundaries *inside* the nested extent that Walk 1 correctly treats as interior. That is exactly the direction that makes Walk 2's segments smaller, never larger.

Every degenerate exit of Walk 1 is also safe. `declared == 0` returns `first` (`:465-467`), so `group_end == first` and Walk 2's single iteration measures `0`. The depth cap sets `overflow` (`:446-449`) and exits at `:577`. Walk 1's three remaining early returns — `first >= entries_.size()` (`:451-453`), dict-free (`:454-456`), and delimiter-not-a-member (`:461-462`) — are each unreachable from `group()`, which guards the same conditions first at `:546`, `:553` and `:566`.

### Why Walk 2 nevertheless cannot simply be deleted

`group()` has a **dict-free fallback branch** at `:579-582`, taken when `opaque_dict_ == nullptr || group_member_fn_ == nullptr`. On that branch `consume_group_extent` is never called and `group_end` is set to `entries_.size()`. Walk 2 is therefore the **only** per-instance cap enforcement non-dictionary callers get. Deleting it outright would be a silent fail-open regression on that path.

Nesting-awareness is unachievable without a dictionary — membership is what tells the walk a tag opens a nested group — so the dict-free branch keeps a flat cap check. That residual flatness is a **limitation to record**, not a defect this feature repairs.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - One traversal owns instance-boundary and cap accounting (Priority: P1)

An application reads a repeating group from a parsed FIX message through `MessageView::group<>()` or the C-ABI top-level group getter. The engine determines the group's extent and enforces the per-instance entry cap from a **single nesting-aware traversal**, not from one nesting-aware traversal followed by a second flat re-walk of the same entries.

**Why this priority**: This is the issue's stated acceptance and the whole reason the feature exists. It removes the last flat instance-boundary rule from `group()` and eliminates a redundant O(extent) walk from a path taken on every group materialization. Nothing else in the feature is meaningful without it.

**Independent Test**: Fully testable on its own — run the existing wire read suite plus the census pin against a build with the second walk removed from the dictionary path, and confirm every returned slice, extent, `group_index`, reserve bound and error disposition is bit-identical to the pre-change engine across all ten shipped dictionaries.

**Acceptance Scenarios**:

1. **Given** a message carrying a repeating group on any shipped dictionary, **When** the application requests that group's slices, **Then** the returned slices, instance count and extent are identical to those the pre-change engine returned.
2. **Given** a message whose outer group delimiter is itself a nested group's count tag (one of the 485 such contexts), **When** the application requests the group, **Then** the outer group still spans all of its declared instances and the nested entries are still interior to their instance — unchanged from the post-083 behaviour.
3. **Given** a message with a repeating group whose instance entry count exceeds `max_group_entries_per_instance`, **When** the application requests that group, **Then** the request fails with the group-too-large disposition, as before.
4. **Given** any message accepted by the pre-change engine, **When** the same message is read after the change, **Then** no group request that previously succeeded now fails, and no group request that previously failed now succeeds.

---

### User Story 2 - Non-dictionary callers keep their cap (Priority: P2)

A caller uses an `OffsetTable` built without a dictionary (no membership callback available). Such a caller still gets per-instance cap enforcement against a hostile or malformed frame — the DoS defence is not silently removed as a side effect of tidying the dictionary path.

**Why this priority**: Fail-open on a security cap is a worse outcome than the redundancy this feature removes. It is P2 rather than P1 only because it is a **preservation** requirement: the behaviour already exists and the feature must not lose it.

**Independent Test**: Fully testable on its own — drive a dict-free `OffsetTable` with a frame whose flat instance segmentation exceeds the cap and assert the group-too-large disposition, then mutate the delivered code to drop the check and confirm the same test goes RED.

**Acceptance Scenarios**:

1. **Given** a dict-free `OffsetTable` and a frame whose flat segmentation puts more than `max_group_entries_per_instance` entries between two delimiter occurrences, **When** the group is requested, **Then** it fails with the group-too-large disposition.
2. **Given** a dict-free `OffsetTable` and a frame within the cap, **When** the group is requested, **Then** it succeeds and returns the same extent the pre-change engine returned.
3. **Given** the delivered dict-free cap check is removed by deliberate mutation, **When** scenario 1's test runs, **Then** it fails — proving the pin is load-bearing rather than vacuous.

---

### User Story 3 - The remaining flatness is recorded honestly (Priority: P3)

An operator or maintainer reading `spec/behaviors-and-limitations.md` sees that L-063-4's leg 2 is delivered, that leg 1 remains descoped with its evidence intact, and that two flat instance-boundary rules **survive on purpose** — the dict-free cap check in `group()`, and the instance splitter in `group_slices()` — each with the reason it was not changed.

**Why this priority**: Documentation, not behaviour. It is nonetheless required: the issue names the L-063-4 update as acceptance, and leaving a reader to infer that "leg 2 delivered" means "no flat rules remain" would be a false claim in the operator-facing artifact.

**Independent Test**: Testable by inspection of the updated L-063-4 row against the delivered source.

**Acceptance Scenarios**:

1. **Given** the updated L-063-4 row, **When** a reader looks for leg 2's disposition, **Then** it is recorded as DELIVERED by this feature with the redundancy argument's conclusion stated.
2. **Given** the updated L-063-4 row, **When** a reader looks for leg 1's disposition, **Then** the descope-with-evidence text from 083 is intact and not weakened.
3. **Given** the updated L-063-4 row, **When** a reader asks which flat instance-boundary rules remain, **Then** both surviving sites are named with their justification.

---

### Edge Cases

- **A group declaring zero instances.** The nesting-aware traversal consumes no extent; the removed walk measured a zero-length segment. The delivered path must return the same empty group, not a cap breach.
- **A group whose count field is the last entry in the table.** Handled before either walk is reached; the delivered path must not change that disposition.
- **Nesting deeper than the depth cap.** The nesting-aware traversal already reports overflow and `group()` already converts it; removing the second walk must not change which error is returned or the order in which it is decided.
- **A group whose delimiter is also a nested group's count tag.** The removed walk fired boundaries *inside* the nested extent. Removing it must not change the extent, the slices, or the cap outcome for any of the 485 shipped contexts of this shape.
- **A declared count that lies (declares more instances than the frame carries).** The traversal is hard-bounded by the entry count; the delivered path must remain fail-closed with no change to the bound.
- **A dict-free table whose group extent runs to the end of the entry table.** The surviving flat check measures its last segment to `entries_.size()`, so trailing non-group fields count toward that segment. This is **pre-existing looseness** and must be preserved verbatim, not tightened, by this feature.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: On the dictionary path, `OffsetTable::group()` MUST derive its per-instance cap accounting from the single nesting-aware extent traversal, and MUST NOT perform a second, flat re-walk of the group's extent.
- **FR-002**: The removal in FR-001 MUST be behaviour-preserving on the dictionary path for every input: no frame may change its returned slices, instance count, extent, `group_index`, reserve bound, or error disposition as a result of it. The redundancy argument in Context §"Why Walk 2 cannot fire" is the basis of this requirement and MUST be restated in the delivered source as the justification comment at the removal site.
- **FR-003**: Per-instance cap enforcement on the **dict-free** fallback path MUST be preserved with its current semantics — same delimiter source (wire-derived), same segmentation, same extent bound (`entries_.size()`), same error disposition. This feature MUST NOT tighten, loosen, or re-scope it.
- **FR-004**: The cap MUST remain reachable and MUST still fire on **both** paths after the change. A frame exceeding `max_group_entries_per_instance` MUST return the group-too-large disposition on the dictionary path and on the dict-free path.
- **FR-005**: The regression pin for FR-004 MUST be proven load-bearing by mutation — with the delivered check removed, the pin MUST go RED. A cap pin that has never been observed RED does not discharge FR-004.
- **FR-006**: `spec/behaviors-and-limitations.md` L-063-4 MUST be updated to record leg 2 as DELIVERED by this feature, MUST preserve leg 1's descope-with-evidence disposition from 083 unweakened, and MUST NOT reopen fixpp#180.
- **FR-007**: The updated L-063-4 row MUST name the flat instance-boundary rules that **survive** this feature — the dict-free cap check in `group()` and the instance splitter in `group_slices()` (`src/wire/offset_table.cpp:712-733`) — each with the reason it was not changed, so "leg 2 delivered" cannot be read as "no flat rules remain".
- **FR-007a**: That same record MUST state that the two surviving sites do not merely share a flat *shape* — they resolve their delimiter from **different sources**. `group()` and `consume_group_extent` derive `delim` from the wire (`:551`, `:458`); the `group_slices()` splitter derives it from the dictionary's per-context store (`:704-711`), which is 083's change and moved 330 contexts' delimiters. The asymmetry is pre-existing and outside this feature's scope, but "flat" alone understates it and MUST NOT be the only word used.
- **FR-008**: No public or exported surface may change: no header signature, no exported symbol, no error enum value, no C-ABI version. `consume_group_extent`'s signature and contract MUST be unchanged.
- **FR-009**: The change MUST NOT introduce any allocation, and MUST NOT increase the work performed per `group()` call on either path.

### Key Entities

- **Group extent**: the half-open entry range a repeating group occupies, computed by the nesting-aware traversal and returned as `group_index`. Unchanged by this feature.
- **Per-instance entry cap**: `max_group_entries_per_instance`, a configured defence-in-depth bound on how many entries one group instance may contain. Its *enforcement site* is what this feature consolidates; its *value and meaning* are unchanged.
- **Instance boundary rule**: the predicate deciding where one group instance ends and the next begins. Two exist today — nesting-aware (dictionary-driven) and flat (wire-derived). This feature removes the flat one from the dictionary path only.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Across all ten shipped dictionaries, the delimiter census pin `DelimiterCensus.RedCountsReconcileWithSpecBaseline` (`tests/dictionary/delimiter_census_test.cpp:476`) and all seven `TypedReadSplitAgreement.*` tests (`tests/wire/typed_read_split_agreement_test.cpp`: six `ExtentWalkDescendsAtNestedGroupDelimiter_*` legs plus `OutOfScopeWireProbesUnchanged`) pass unchanged — same counts, same baseline, no fixture edits made to accommodate the change. *(Both names verified present at `main` = `c1564dd2`, 2026-08-03, rather than inherited from the issue body.)*
- **SC-002**: Zero group requests change outcome. For every frame in the existing wire, C-ABI read, and nested-group test corpora, the returned slice set, instance count, extent and error disposition are identical before and after the change.
- **SC-003**: The per-instance cap fires on the dictionary path — a frame exceeding the cap returns the group-too-large disposition — and the pin asserting it is proven RED when the enforcement is mutated away. The existing pin `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` (`tests/wire/offset_table_test.cpp:198-235`) already covers the positive direction and MUST stay green; its companion `WireOffsetTable.DoSCapPerInstanceAllowsAggregateOverCap` (`:164-196`) MUST stay green too, pinning that an aggregate over the cap is still accepted when no single instance breaches.
- **SC-004**: The per-instance cap fires on the **dict-free** path under the same two conditions as SC-003 (fires when exceeded; pin proven RED under mutation). No such pin exists today — the two named in SC-003 both drive a `table_view` and therefore exercise the dictionary path only — so this criterion is discharged by a **new** test, not by an existing one.
- **SC-005**: On the dictionary path, `OffsetTable::group()` contains exactly **one** traversal of a group's entries. Discharged by source inspection — the second walk is absent from the dictionary branch — with the performance consequence carried by SC-006's benchmark. *(Deliberately not stated as an entries-visited count: no such counter exists, and adding one to a hot path to satisfy a success criterion would be scope creep.)*
- **SC-006**: A benchmark covering group materialization shows no regression against the pre-change baseline, and the measurement is carried in the PR rather than asserted in prose.
- **SC-007**: Zero change to exported symbols, public headers, error enum values and the C-ABI version, verified by the existing ABI-hygiene gates.
- **SC-008**: The full sanitizer matrix (ASan/UBSan/TSan) and the static-analysis gate pass with no new findings attributable to this change.
- **SC-009**: L-063-4 states leg 2 DELIVERED, retains leg 1's descope evidence, names both surviving flat rules, and contains no dangling reference — verified by inspection against the delivered source line numbers.

## Assumptions

- **A-001**: The redundancy argument in Context §"Why Walk 2 cannot fire" was derived by the orchestrator from `src/wire/offset_table.cpp` at `main` = `c1564dd2` on 2026-08-03. `/speckit-plan` MUST re-verify each of its four steps against the tree it plans on, rather than inherit the conclusion. If any step fails, FR-002's behaviour-preservation claim is void and the feature must be re-scoped, not patched.
- **A-002**: Leg 1 of L-063-4 stays descoped. 083 measured its target population as empty across all ten dictionaries and showed a literal implementation would break the 485-context shape that *is* reachable. This feature does not re-open it, and the `group_slices()` splitter at `:712-733` therefore stays flat.
- **A-003**: fixpp#180 is closed and stays closed. 072-nested-group-hardening delivered what it asked for; the splitter residual was never its deliverable.
- **A-004**: `group()` is on the group-read hot path (reached from `group_slices_status:667` on every materialization), so Article VIII §3 applies and a benchmark ships **in this PR**. It is planned in rather than waited for at Gate B. The expected direction is a small improvement, since the change removes work; the bench exists to prove no regression, not to claim a win.
- **A-005**: The dict-free path's existing looseness — its last segment extending to `entries_.size()` and thus counting trailing non-group fields — is inherited behaviour, not a defect introduced or repaired here. It is preserved and recorded.
- **A-006**: No configuration surface, no compatibility mode, and no runtime switch is introduced. The consolidation is unconditional on the dictionary path.
- **A-006a**: The cap is fixture-reachable. `max_group_entries_per_instance` defaults to `4096` (`include/fixpp/wire/offset_table.hpp:28`) but is a per-parse `OffsetTable::Config` member (`:95`), and three existing tests already drive it down to `3` via `tight_cfg`. FR-004/FR-005/SC-004 are therefore constructible without an oversized frame and without touching the default. *(Verified 2026-08-03 — checked because a cap that were global-and-fixed would have made SC-004 undischargeable, and that is a `/plan`-time discovery, not an `/implement`-time one.)*
- **A-007**: `/speckit-verify` runs clang-only locally; the gcc-release and MSVC legs are CI-only. Given the change is a code removal with no platform-specific construct, no platform-divergent behaviour is expected — but the CI legs remain the authority, not the local run.
