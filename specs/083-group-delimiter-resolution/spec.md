# Feature Specification: Group Delimiter Resolution

**Feature Branch**: `083-group-delimiter-resolution`

**Created**: 2026-07-30

**Status**: Draft

**Input**: User description: "Fix the group-delimiter resolution defect family (closes fixpp#210 and fixpp#208)."

**Closes**: fixpp#210 (context-store delimiter is global-by-construction) and fixpp#208 (leading-nested-group delimiter: loader one-level component scan + `consume_group` false-rejection). The user scope decision of 2026-07-30 took the **whole delimiter axis** rather than splitting it, because a document-order delimiter regression pin cannot hold across all ten dictionaries while either half remains unfixed.

---

## Context — what a "delimiter" is and why this is one defect, not three

In FIX, a repeating group's **delimiter** is the tag that must open every instance of that group. It is defined as the group's **first member in declaration (document) order**. A receiver counts instances by counting delimiter occurrences; get the delimiter wrong and instance counting collapses.

The engine resolves a delimiter in three places, and all three are wrong in a different way:

1. **Which context the delimiter is applied to.** The runtime keys group metadata by `(msg_type, ancestor-path, no_tag)`, but the delimiter fed into that store is looked up **globally, first-seen-wins**. A `NumInGroup` tag reused across messages with different leading members therefore gets one message's delimiter applied to all of them.
2. **How the delimiter is found inside the declaration.** The loaders scan a group's first child only **one level deep** when that child is a component, so a component whose own first child is a nested group is skipped and a later scalar is chosen instead — or nothing is found at all.
3. **What the receiver does when the delimiter is itself a nested group's count tag.** The instance scanner consumes the delimiter without descending into the nested group it opens, so the nested group's body is read as if it belonged to the outer group, the scan breaks, and instance counting fails.

Fixing (2) without (3) converts a wrong-delimiter defect into a **false rejection across 232 measured contexts**. That coupling is why this is a single feature.

---

## Clarifications

### Session 2026-07-30

- Q: If a reference engine's delimiter disagrees with declaration order for a given group, which wins? → A: Declaration order always wins. The interop gate is **observational** — divergences are measured and recorded as behaviour/limitation rows and release-note entries, but never alter resolution. No compatibility mode, no config surface, no branch on the inbound validation path.
- Q: How is the C-ABI construction-path delimiter check reconciled with the validation-path delimiter? → A: Thread the context into the construction path. `validate_group_grammar` already recurses through nested instances and the accumulator already carries the message type, so it maintains the ancestor path as it descends and uses the context-keyed lookup. Both paths then apply one identical rule; no exported ABI signature changes.
- Q: What should the loader do when a group declaration has no resolvable first member even after recursion? → A: Reject the load by default (mirroring the loader's existing fail-closed disposition), **with an explicit opt-in tolerant mode** that downgrades to warn-and-skip for callers loading untrusted or partial dictionaries. Both behaviours are tested and documented.
- Q: Is the typed-read instance splitter (wire-derived delimiter, may mis-split when the delimiter tag reappears at depth) in scope? → A: **Fully in scope** — investigated and fixed in this feature, so nothing delimiter-adjacent is left live.

---

## Baseline measurement *(established on `main` @ `0539b56d` before any change)*

Method: for every group context in every dictionary, compare the runtime's resolved delimiter and member set against an **independent** raw-document walk, across all ten shipped dictionaries (nine QuickFIX-XML + the Orchestra FIX Latest repository).

| dictionary | contexts | wrong delimiter | of those, delimiter is a nested group | polluted member sets | unregistered contexts |
|---|---:|---:|---:|---:|---:|
| FIX40 | 6 | 0 | 0 | 0 | 0 |
| FIX41 | 10 | 0 | 0 | 0 | 0 |
| FIX42 | 38 | 8 | 0 | 4 | 0 |
| FIX43 | 235 | 4 | 0 | 0 | 0 |
| FIX44 | 823 | 10 | 0 | 6 | 0 |
| FIX50 | 1114 | 10 | 0 | 6 | 0 |
| FIX50SP1 | 1309 | 12 | 0 | 6 | 0 |
| FIX50SP2 | 25897 | 261 | 232 | 14 | 30 |
| FIXT11 | 8 | 0 | 0 | 0 | 0 |
| Orchestra FIX Latest | 26806 | 30 | 0 | 16 | 0 |
| **total** | | **335** | **232** | **52** | **30** |

Findings that shape the scope:

- **The wrong delimiter (335) is the primary defect; the polluted member set (52) is a secondary symptom.** A context's member set is polluted only where the wrongly-chosen delimiter is *not already* a declared member there. Everywhere else the delimiter is still wrong and the group still mis-parses, silently.
- **No member is ever missing** (0 in every dictionary), and where pollution exists the extra tag is **exactly the wrongly-resolved delimiter, a single tag, in 100% of cases**. Correcting the delimiter therefore removes all 52 pollutions *by construction*, because a correctly-resolved delimiter is necessarily a declared member of its own context.
- **The defect is cross-loader.** Orchestra has no broken scan and no unregistered groups, yet still shows 30 wrong-delimiter contexts from cause (1). A fix applied to one loader only would be a half-restructure.
- **Affected contexts are not obscure.** Every message below is a measured wrong-delimiter context, with names resolved from the dictionary's own `<messages>` block:
  - `NoOrders(73)` — 8 FIX50SP2 messages: `NewOrderList(E)`, `AllocationInstruction(J)`, `AllocationReport(AS)`, `AllocationInstructionAlert(BM)`, `Confirmation(AK)`, `ConfirmationRequest(BH)`, `ListStatus(N)`, `TradeAggregationRequest(DW)`.
  - `NoExecs(124)` — 6 messages, the `Collateral*` family: `CollateralRequest(AX)`, `CollateralAssignment(AY)`, `CollateralResponse(AZ)`, `CollateralReport(BA)`, `CollateralInquiry(BB)`, `CollateralInquiryAck(BG)`.
  - `NoMDEntries(268)` — `MarketDataIncrementalRefresh(X)`, and additionally `MarketDataSnapshotFullRefresh(W)` on Orchestra.
  - `NoQuoteEntries(295)` — `MassQuote(i)`, `MassQuoteAck(b)`. `NoBidComponents(420)` — `BidResponse(l)`.
- **FIX43 shows 4 wrong delimiters but 0 pollution**, confirming the two symptoms are independent and that a member-set-only fix would leave the primary defect untouched.
- **The 335 figure excludes the 30 unregistered contexts.** Those contexts resolve no delimiter at all today, so they cannot be scored for delimiter *correctness* and were counted separately. Once FR-006 makes their three parent groups register, they enter the delimiter population for the first time. Their declared delimiters — `1499→453`, `1669→1529`, `1919→1920` — are **themselves nested-group count tags**, so these 30 are **additional cases of the reception defect in Story 2**, over and above the 232 already measured. Story 2 must therefore be sized against 232 *plus* these 30, and the pin's post-fix denominator is 365, not 335. This is the one figure in this table that is a projection rather than a measurement, and the first task of the feature is to convert it into one.

Affected `NumInGroup` tags, by cause:

- **Context divergence** (cause 1): 73, 124, 146, 268, 295, 420, 2428, 2474 — plus 1677 and 1772 on Orchestra.
- **Broken one-level scan** (cause 2), FIX50SP2 only: 1677, 1772, 40204, 41599, 42060 (wrong delimiter), and 1499, 1669, 1919 (no delimiter found at all → group never registers, taking nested children 1529, 1534, 1540, 1559, 1918, 1920 with them).

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A counterparty's schema-valid message stops being rejected (Priority: P1)

An integrator runs a session against a counterparty that sends a repeating group in a message where that group's `NumInGroup` tag is also used elsewhere in the dictionary with a different leading member. Today the engine resolves the delimiter from whichever message the loader happened to read first, so it looks for the wrong opening tag, fails to count instances, and rejects a message the dictionary plainly permits. After this feature the engine resolves the delimiter from the group's own declaration in that message, and the message is accepted.

**Why this priority**: This is the user-visible defect. It produces spurious session-level rejections of legal traffic on 335 measured contexts across every shipped dictionary, in messages as common as market-data snapshots and order-list entries. Everything else in this feature exists to make this fix correct or to keep it fixed.

**Independent Test**: Build a wire message for a context whose declared delimiter differs from the dictionary-global first-seen one (FIX44 `AX` / `NoExecs(124)` is the cleanest), submit it to strict validation, and observe acceptance. Build the equivalent message for a *first-seen* context of the same `NumInGroup` tag and observe that it was already accepted — the asymmetry between those two is the entire defect, expressed in two assertions.

**Acceptance Scenarios**:

1. **Given** a dictionary where a `NumInGroup` tag is declared with different leading members in two different messages, **When** a schema-valid multi-instance message arrives for the context that is *not* the first-seen one, **Then** the engine accepts it, rather than rejecting with an unexpected-tag error.
2. **Given** the same dictionary, **When** a schema-valid message arrives for the first-seen context, **Then** the engine still accepts it — the fix must not trade one context's correctness for another's.
3. **Given** a message that is genuinely invalid because it opens a group instance with a tag that is not that context's declared delimiter, **When** it is validated, **Then** the engine still rejects it. Correct resolution must not become permissiveness.

---

### User Story 2 - A group whose first member is itself a group parses correctly (Priority: P1)

A counterparty sends multiple instances of a repeating group whose declared first member is a nested repeating group — a shape that exists in FIX 5.0 SP2 and is already live on every Orchestra/FIX-Latest session. Today the receiver consumes the opening tag without descending into the nested group it opens, reads the nested group's body as if it belonged to the outer group, aborts the scan after one instance, and rejects on an instance-count mismatch. After this feature the receiver descends, counts every instance, and accepts.

**Why this priority**: P1 alongside Story 1, and a **blocking prerequisite** for it. Correcting how the delimiter is *found* (Story 3) makes eight `NumInGroup` tags resolve to a nested group's count tag for the first time. Without descend-at-delimiter, that correction converts a wrong-delimiter defect into a false rejection across 232 measured FIX50SP2 contexts — strictly worse than today. A further **30 contexts** join them once Story 3 makes their parent groups register, since all three of those groups' declared delimiters are nested-group count tags too. This story must land before, or atomically with, Story 3.

**Independent Test**: Construct a dictionary in which an outer group's delimiter is a nested group's count tag, then validate a two-instance message. Today it is rejected while the one-instance form is accepted; after the fix both are accepted. The single-instance case passing is precisely why this has gone unnoticed.

**Acceptance Scenarios**:

1. **Given** an outer group whose delimiter is a nested group's count tag, **When** a two-instance message arrives, **Then** it is accepted with an instance count of two.
2. **Given** the same shape, **When** a one-instance message arrives, **Then** it is still accepted — the previously-passing case must not regress.
3. **Given** the same shape, **When** a message declares a count that does not match the instances present, **Then** it is still rejected. Descending must not weaken instance-count enforcement.
4. **Given** a nesting depth at the engine's supported limit, **When** a message at that depth arrives, **Then** behaviour is well-defined and bounded rather than unbounded recursion.

---

### User Story 3 - Groups the runtime silently ignored become known to it (Priority: P2)

An integrator uses the generated typed builders to construct a message containing one of three FIX 5.0 SP2 groups whose declaration consists solely of a component that itself contains only a nested group. Today the loader finds no first member for these, records no delimiter, and silently drops the group from the runtime dictionary — so the engine's own strict validation rejects a message its own code generator produced. After this feature the loader resolves the member by recursing, the group registers, and the runtime agrees with the code generator.

**Why this priority**: P2 — it is a genuine correctness and self-consistency defect (the runtime registers 502 FIX50SP2 groups while the generator emits 505), and it is a *silent* drop with no diagnostic, but it affects three groups rather than the hundreds in Stories 1 and 2.

**Independent Test**: Count registered groups for FIX50SP2 before and after; assert the three named groups resolve a non-zero delimiter and that their nested children become reachable.

**Acceptance Scenarios**:

1. **Given** FIX50SP2, **When** the dictionary is loaded, **Then** all three previously-dropped groups register with the delimiter their declaration specifies.
2. **Given** the same load, **When** the registered-group total is compared against the code generator's emitted total, **Then** the two agree, and the size of the change is explained by naming the groups that account for it rather than by asserting a bare number.
3. **Given** a group declaration that genuinely has no resolvable member, **When** it is loaded, **Then** the loader still surfaces that condition rather than dropping it silently.

---

### User Story 4 - The delimiter cannot silently regress again (Priority: P2)

A maintainer changes the loader, the dictionary set, or the code generator. Today **nothing anywhere pins any delimiter** — which is exactly why this defect family survived across every shipped dictionary and both loaders, through a suite that includes 78 passing collision-membership cases. After this feature, a regression in delimiter resolution fails the build.

**Why this priority**: P2 — it ships no user-facing behaviour, but without it the same class of defect recurs undetected. The existing collision-membership guards must not be counted as coverage here: their discriminator tag is derived independently of the delimiter and is not necessarily the offending tag, so their green is a proxy gap rather than proof of absence.

**Independent Test**: Deliberately reintroduce a global-first-seen delimiter resolution and confirm the pin fails; restore and confirm it passes.

**Acceptance Scenarios**:

1. **Given** all ten dictionaries, **When** the pin runs, **Then** every group context's resolved delimiter equals its declaration-order first member, with no carve-out or exclusion list.
2. **Given** the pin, **When** its expected values are derived, **Then** they come from a source independent of the code under test, and a sample is corroborated against a third independent authority — so the pin cannot pass by mirroring the implementation's own logic.
3. **Given** a reintroduced global-first-seen resolution, **When** the pin runs, **Then** it fails. A pin never observed failing proves nothing.

---

### User Story 5 - Reading a group back yields the same instances that validated (Priority: P3)

An integrator validates an inbound message, then reads its repeating group through the typed-read API. Today the read path infers instance boundaries from the wire rather than from the dictionary, so where a delimiter tag legitimately reappears at greater nesting depth the two can disagree — a message that validated as N instances may read back as a different number, silently. After this feature both paths split on the same dictionary-defined boundary.

**Why this priority**: P3 — it is the last place in the engine where a delimiter is resolved by a different rule, and leaving it would mean this feature fixed every delimiter path but one. It is reachable on Orchestra sessions today. It is P3 rather than P1 because, unlike Stories 1 and 2, **no defect has yet been measured here** — fixpp#208 flagged it as adjacent and explicitly unverified. The first work in this story is therefore to measure, not to fix.

**Independent Test**: Take a shape whose delimiter tag reappears at greater depth, validate it to establish the instance count, then read it back through the typed API and compare.

**Acceptance Scenarios**:

1. **Given** a group whose delimiter tag also occurs at greater nesting depth inside its own instances, **When** the message is validated and then read back, **Then** both report the same instance count and the same instance boundaries.
2. **Given** the characterisation in FR-021a finds no reachable mis-split, **When** that conclusion is recorded, **Then** it is supported by the shapes actually tried, not asserted — the point of this story is to replace an unverified note with evidence either way.

---

### Edge Cases

- A `NumInGroup` tag reused in the *same* message under two different ancestor paths — the ancestor path, not just the message type, must discriminate.
- A group whose declared first member is a nested group **that is itself declared via a component** — both the recursion and the descent must compose.
- A group declaration that resolves to no member at all: must be surfaced, not silently dropped (this is what makes the current three-group drop invisible).
- A delimiter tag that legitimately reappears *inside* a group instance at greater depth — instance boundaries must not be mis-split.
- Nesting at and beyond the engine's supported depth limit — bounded, defined behaviour, no unbounded recursion.
- A context present in the independent walk but absent from the runtime store: must be distinguishable from a context that resolves to the wrong value. (The runtime falls back to a global lookup on a context miss, which makes a miss *look* like a wrong answer; the baseline measurement above had to discriminate these explicitly, and 10 of the 42 contexts originally reported in fixpp#210 were this artefact.)
- Dictionaries with zero affected contexts (FIX40, FIX41, FIXT11) must stay at zero — the fix must not introduce churn where there was no defect.

---

## Requirements *(mandatory)*

### Functional Requirements

**Delimiter resolution**

- **FR-001**: The engine MUST resolve a repeating group's delimiter from that group's own declaration in its own context, identified by message type and ancestor group path, rather than from a dictionary-global first-seen value.
- **FR-002**: The resolved delimiter MUST be the group's first member in declaration (document) order.
- **FR-003**: Resolution MUST recurse through component references to any depth when locating the first member, rather than inspecting only a component's immediate children.
- **FR-004**: When a group's first member in declaration order is itself a repeating group, its count tag MUST be the resolved delimiter.
- **FR-005**: Both dictionary sources — the QuickFIX-XML loader and the Orchestra loader — MUST implement FR-001 through FR-004 with equivalent behaviour. A change to one without the other is not an acceptable partial delivery.
- **FR-006**: A group declaration for which no member can be resolved even after FR-003's recursion MUST NOT be silently omitted from the runtime dictionary. The default disposition is **fail-closed**: the load is rejected with a diagnostic naming the offending group, mirroring how the loader already treats every other structural violation.
- **FR-006a**: A caller loading an untrusted or partial dictionary MUST be able to opt in to a **tolerant mode** that downgrades FR-006 to warn-and-skip, leaving the group unregistered and the load successful. Tolerant mode MUST be explicit opt-in; the default MUST remain fail-closed.
- **FR-006b**: Both dispositions MUST be tested — a rejection witness under the default and a skip-with-diagnostic witness under tolerant mode — and both MUST be documented for operators. Before FR-006 is enabled, all ten shipped dictionaries MUST be confirmed to still load under the default, so the fail-closed path cannot regress the shipped set.

**Reception**

- **FR-007**: When an instance-opening delimiter is itself a repeating group's count tag, the receiver MUST descend into that nested group and resume the outer scan one past the nested group's extent.
- **FR-008**: FR-007 MUST NOT weaken instance-count enforcement, required-member enforcement, or extent termination; a message whose declared count disagrees with the instances present MUST still be rejected.
- **FR-009**: Descent MUST remain bounded by the engine's existing nesting-depth limit.

**Membership**

- **FR-010**: A group context's member set MUST contain exactly the tags that context's declaration declares — no injected tag.
- **FR-011**: The load-bearing source comment asserting that the per-context member set "stays exact regardless" of divergent delimiters MUST be corrected; it is currently false and was the reason the defect was not caught by inspection.

**Regression pin**

- **FR-012**: A regression pin MUST assert, for every group context in all ten shipped dictionaries, that the resolved delimiter equals the declaration-order first member — with no carve-out, exclusion list, or per-dictionary exemption.
- **FR-013**: The pin's expected values MUST be derived independently of the implementation under test, and a documented sample MUST be corroborated against a third independent authority (the code generator's own group-order data, or the recorded cross-loader values).
- **FR-014**: The pin MUST be demonstrated to fail when the defect is reintroduced, and that demonstration MUST be recorded.
- **FR-015**: Per-context member-set exactness MUST be asserted by the same pin rather than as a separate assertion, since FR-002 makes it a consequence of FR-012 rather than an independent property.
- **FR-016**: The existing collision-membership guards MUST NOT be cited as evidence of delimiter correctness.

**Consistency and disclosure**

- **FR-017**: The count of registered groups per dictionary MUST agree with the count the code generator emits for the same dictionary, and any change to that count MUST be justified by naming the groups responsible.
- **FR-018**: The C ABI's message-construction path MUST resolve a group's delimiter using the same context-keyed rule as inbound validation, so that a message the builder accepts is never rejected by the engine's own validation on delimiter grounds, and vice versa. The construction-side check already walks nested group instances recursively and already has the message type available, so it MUST carry the ancestor path through that walk rather than falling back to a context-free lookup.
- **FR-018a**: FR-018 MUST NOT change any exported C ABI signature. The ABI is GA-frozen; this is a behaviour change reachable through the existing surface, not a surface change.
- **FR-019**: Behaviour changes visible to existing integrators MUST be recorded as operator-facing behaviour/limitation entries and in the release notes, including the C-ABI construction change for the five groups whose delimiter moves.
- **FR-020**: Because every member of the affected groups is schema-optional, **any** delimiter choice rejects some shape another engine accepts. Declaration order (FR-002) is nonetheless **authoritative and unconditional**: where an external reference engine resolves a different delimiter, that divergence MUST be recorded as an operator-facing behaviour/limitation row and a release-note entry, and MUST NOT change the engine's resolution. The per-release interop gate is therefore **observational** for this feature — it measures and documents divergence, it does not arbitrate it.
- **FR-020a**: No compatibility or leniency mode MAY be introduced for delimiter resolution. There is to be no per-session configuration surface and no conditional branch on the inbound validation path for this behaviour, so that resolution stays derivable from the dictionary alone and FR-012's pin can remain carve-out-free.
**Typed-read instance splitting**

- **FR-021**: The typed-read path's group-instance splitter MUST divide a group's instances on the same boundaries the dictionary's per-context delimiter defines. It currently infers the boundary from the wire rather than from the dictionary, which can mis-split when the delimiter tag legitimately reappears at greater nesting depth.
- **FR-021a**: The splitter's current behaviour MUST first be characterised by measurement — a reproduction demonstrating a mis-split, or evidence that none is reachable. Whichever it is MUST be recorded, so the conclusion is not carried forward as an assumption the way it was in fixpp#208.
- **FR-021b**: Typed-read instance boundaries and validation instance boundaries MUST agree for every affected context, and that agreement MUST be pinned. A shape that validates as N instances MUST read back as N instances.

**Performance**

- **FR-022**: Delimiter resolution sits on the inbound validation path and is queried per received field, so this feature changes hot-path behaviour. A benchmark covering that path MUST ship **in the same change** as the fix, together with any intentional baseline update and its rationale. A narrative assertion that the budget was met is not acceptable in place of the benchmark.

### Key Entities

- **Group context**: the coordinate identifying one occurrence of a repeating group — message type, ancestor group path (outermost first, excluding the group itself), and the group's own count tag. The unit at which delimiters and member sets must be exact.
- **Delimiter**: the tag that opens every instance of a group in a given context; defined as the first member in declaration order. Currently one value per count tag across the whole dictionary; must become one value per context.
- **Member set**: the tags a context's declaration declares as direct members of that group. Used to determine where a group instance ends.
- **Group declaration record**: the loader's per-group record carrying the delimiter. Currently deduplicated by count tag with first-seen-wins, which is the structural cause of the context divergence.
- **Independent document walk**: a reader of the raw dictionary documents, sharing no code with the loaders, used to derive expected delimiters and member sets for the regression pin.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Across all ten shipped dictionaries, the number of group contexts whose resolved delimiter differs from the declaration-order first member falls to **0**. The pre-fix figure is **335 measured**, over a population that excludes the 30 contexts which resolve no delimiter today; once those register (SC-003) the post-fix population is **365**, and the measured baseline for those 30 MUST be established before the fix rather than asserted after it.
- **SC-002**: Across all ten shipped dictionaries, the number of group contexts whose member set differs from its declaration falls from **52 to 0**.
- **SC-003**: The number of group contexts present in the dictionary documents but absent from the runtime falls from **30 to 0**.
- **SC-004**: Groups whose delimiter is a nested group's count tag parse at multiple instances. Coverage is split deliberately, because a per-context wire witness is not constructible at this scale: the **delimiter pin (FR-012) covers all 232 measured contexts plus the 30 newly-registering ones by construction**, and **wire acceptance is witnessed on a named subset** — one message per affected count tag (1677, 1772, 40204, 41599, 42060, 1499, 1669, 1919) plus the synthetic two-instance reproduction. No previously-accepted single-instance message may become rejected. Enumerating 232 wire witnesses is explicitly *not* the deliverable; a sibling feature already found that some contexts are unconstructable at all because every member is schema-optional, and that constraint applies here too.
- **SC-005**: The registered-group count for FIX 5.0 SP2 agrees with the code generator's emitted count, and the difference from today is fully accounted for by three named groups.
- **SC-006**: A deliberately reintroduced global-first-seen delimiter resolution causes the regression pin to fail, demonstrated and recorded.
- **SC-007**: No schema-valid message accepted before this change is rejected after it, except for the disclosed C-ABI construction-order change, which is enumerated by group and documented.
- **SC-008**: Dictionaries with no affected contexts today (FIX40, FIX41, FIXT11) show no behavioural change.
- **SC-009**: Message-processing throughput stays within the project's standing regression budget against the recorded baseline, since delimiter resolution sits on the inbound path.
- **SC-010**: For every affected context, the instance count and boundaries reported by validation and by typed read agree; the pre-existing behaviour of the read path is characterised by evidence rather than left unverified.
- **SC-011**: A dictionary whose group declaration has no resolvable member is rejected by default with a diagnostic naming that group, and is loaded with a warning under the explicit tolerant mode. All ten shipped dictionaries continue to load under the default.
- **SC-012**: A group built through the C ABI is accepted at construction if and only if the engine's own inbound validation would accept the same delimiter placement, with no exported ABI signature changed.

---

## Assumptions

- **Declaration order is the authority.** The FIX specification defines a group's delimiter as its first field in declaration order; where the engine's current behaviour disagrees with declaration order, the engine is wrong. This is corroborated internally by three independent authorities that already compute the declaration-order value — the Orchestra loader for the tags it covers, the code generator's group-order data, and the typed builders — and it is the premise of the entire measurement above. It is *not* corroborated against an external reference engine, and the reference engines are absent from the working copy, so that corroboration cannot be obtained during this feature. Per the 2026-07-30 clarification this does **not** hold the rule hostage: declaration order is authoritative unconditionally (FR-020), and any divergence a reference engine later shows is recorded as a documented behaviour rather than treated as a defect in this resolution.
- **Delimiter correctness subsumes member-set correctness.** Because a declaration-order first member is necessarily a declared member of its own context, and because the measurement shows no member is ever missing and the only extra tag is ever the delimiter itself, the member-set defect is a consequence rather than a peer. The feature therefore does not separately remove the delimiter's injection into the member set — it makes that injection redundant, and pins the result.
- **The claim that wrong delimiters cause mis-parsing is, at specification time, a reading of the receiver's logic rather than a wire-level measurement.** No wire reproduction was constructed during triage. The first test written for this feature closes that gap (Story 1's independent test) before any fix is made; if the reading turns out to be wrong, the feature's premise changes and this spec must be revisited.
- **The baseline measurement's root-cause split is sound but not proven.** A wrongly-scanned delimiter that coincidentally equalled another context's declared first member would be attributed to the wrong cause. The split reproduces fixpp#208's independently-derived tag list exactly, which is strong corroboration but not proof.
- **The ten dictionaries in the repository are the full population.** No dictionary outside those shipped is considered.
- **The C ABI's exported signatures do not change.** This feature changes behaviour reachable through a frozen ABI, not the ABI surface itself; it is nonetheless treated as ABI-affecting for gate purposes, since the observable contract changes for existing callers.

## Dependencies

- Requires the independent document walk used by the existing dictionary census, extended to record declaration order (it currently records members as an unordered set, which cannot express a delimiter). **The extension must be purely additive** — a new record alongside the existing member sets, not a reshaping of them. The existing sets are consumed by census pins on a parked sibling branch that cannot currently be built or re-run, so changing their shape would break pins with no way to observe the breakage.
- Requires the per-release external-engine interop gate for **observation only** (FR-020) — it records divergence, it does not gate the resolution rule. The reference engines are currently absent from the working copy; because the gate is observational, their absence does not block this feature, but it does mean any divergence is discovered later rather than here.
- Supersedes and closes fixpp#208; its two defects are FR-003/FR-006 and FR-007 here.
- On close-out, unblocks two pins in `082-structural-group-detection` that are currently written to tolerate the injected delimiter, and corrects that feature's recorded note that a related test fails "5 of its 14 contexts" — it now fails 2.
