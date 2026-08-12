# Implementation working notes — 082-structural-group-detection

Durable record of the evidence that stops existing once **T023** (the predicate replacement)
lands, plus the observed-RED transcripts for every pin whose RED state is only reachable before
its implementation task. Referenced by T002/T003/T004, T046/T048, and the T055 completeness audit.

**Branch**: `082-structural-group-detection` · **Base**: `3f577f10` (tasks + analyze + checklist + audit)

---

## Phase 1 — pre-change evidence (T001–T004)

### T001 — clean codegen rebuild

The `build/linux-clang-debug` tree was found stale at the 082 branch point (an incremental
`cmake --build` scheduled **3507/3507** targets, i.e. every object including the `fixpp-codegen`
emitter and the whole `_codegen` tree, was rebuilt from scratch). This discharges T001's
requirement directly: no stale `Reify.hpp` emitter object can survive a full-tree rebuild
(`project_codegen_emitter_staleness`). The pre-change generated tree is recorded at
`<scratchpad>/pre-change/codegen-tree/` for the T026/T027 regeneration diffs.

_(status: see § Evidence index below for the recorded artefact paths)_

### T002 — pre-change `table_view_footprint_bench` figures

`bench/baselines/dictionary/` contains `reify_bench.json` and `xml_loader.json` **only** — there is
no `table_view_footprint_bench.json` today (075 recorded its figures in-file). Confirmed by
directory listing at the branch point. The "before" numbers below are therefore the only ones
obtainable, and are the comparison basis for T046's ±5% budget.

Benches are **not built by any preset** (`FIXPP_BUILD_BENCH:BOOL=OFF` everywhere), so this needed a
separate `cmake -B build/linux-clang-release -DFIXPP_BUILD_BENCH=ON`. Note `cmake --preset` cannot be
used here — the Conan `CMakeUserPresets.json` preset-name collision (`Duplicate preset "conan-debug"`)
makes CMake refuse to read any preset; configuring the existing build dir by `-B` sidesteps it.

**MEASURED pre-change** (release, `--benchmark_min_time=0.5s`, 10×3593 MHz, load avg 2.30);
transcript `<scratchpad>/pre-change/T002-table_view_footprint-BEFORE.txt`:

| benchmark | time | CPU | iters | counter |
|---|---|---|---|---|
| `BM_TableView_Sizeof/iterations:1` | 471139 ns | 13495 ns | 1 | `sizeof_B=608` |
| `BM_TableView_BuildFix50SP2` | 357594 us | 357604 us | 2 | — |
| `BM_TableView_BuildFix44` | 4578 us | 4576 us | 141 | — |

There is **no FIX 4.2 row today** — T046 adds it, so that row has no "before" by construction.
`sizeof_B=608` is the pre-`group_bits_`-growth figure T046 must re-report against.

### T003 — pre-change `v42` compile-time TU figure

`bench/codegen/compile_time_bench/` harness, run via `ctest -L bench`, against its load-bearing
≤3 s single-version ceiling.

**MEASURED pre-change**, 4 runs (`ctest -R '^compile_time_bench$'`, release + `FIXPP_BUILD_BENCH=ON`);
transcripts `<scratchpad>/pre-change/T003-compile_time_bench-BEFORE*.txt`:

| version | run 1 (loaded) | run 2 | run 3 | run 4 | steady state | ceiling verdict |
|---|---|---|---|---|---|---|
| `v42` | 3.57 s | 2.73 s | 2.76 s | 2.72 s | **≈ 2.73 s** | PASS, but only **~0.27 s (≈10%) headroom** |
| `v44` | 4.79 s | 4.44 s | 4.64 s | 4.46 s | ≈ 4.5 s | **FAIL — pre-existing, not caused by 082** |
| `v50sp2` | 10.64 s | 10.13 s | 10.05 s | 10.53 s | ≈ 10.3 s | KNOWN_OVERAGE (exempt) |
| `vt11` | 2.16 s | 2.05 s | 2.04 s | 2.04 s | ≈ 2.05 s | PASS |
| all-versions | 15.34 s | 14.46 s | 14.42 s | 15.09 s | ≈ 14.8 s | straddles the 15 s **soft** ceiling |

Two findings that change what T048 can claim:

1. **The harness already returns `NFR-003-2 result: FAIL` before this feature touches anything**,
   because **`v44` exceeds the load-bearing 3 s single-version ceiling** (≈4.5 s) and only `v50sp2`
   is exempt. T048 therefore cannot be a pass/fail gate — it is a **record-and-compare-to-T003**
   obligation, and the pre-existing `v44` overage must be stated as such so it is not mis-attributed
   to 082. (No CI job runs this harness — `tier1.yml`'s `bench` job is soft and runs only
   `placeholder_bench` — which is why the overage has stayed invisible.)

2. **`v42` has only ~10% headroom** (2.73 s vs the 3 s ceiling), and 082 adds 18 `class G_` to both
   `v42/Messages.hpp` and `v42/Reify.hpp`. A post-change breach is a live risk, not a formality.
   Run 1's 3.57 s shows the measurement is load-sensitive, so T048 must compare **quiet-machine
   steady state to quiet-machine steady state**, not single runs.

### T004 — pre-change registered-group sets (all ten dictionaries)

Baseline side of K1 / K2 / K3. **FIX43's before-value is 33, not 34** — it is the one dictionary
where before ≠ after (contract C2: registered-before 33, registered-after 34, delta `+1 tag (576)`).

| Dictionary | registered **before** | registered **after** (C2) | delta |
|---|---|---|---|
| FIX40 | 0 | 4 | +4 |
| FIX41 | 0 | 7 | +7 |
| FIX42 | 0 | 18 | +18 |
| FIX43 | **33** | **34** | **+1 tag (576)** |
| FIX44 | 59 | 59 | 0 |
| FIX50 | 67 | 67 | 0 |
| FIX50SP1 | 97 | 97 | 0 |
| FIX50SP2 | 505 | 505 | 0 |
| FIXT11 | 1 | 1 | 0 |
| Orchestra FIX Latest | 524 | 524 | 0 |

**MEASURED** at the branch point via a scratch tool sweeping `table_view::group_first_field(t) != 0`
over all tags for each dictionary loaded through the real `fixpp::dict::load_any` (transcript:
`<scratchpad>/pre-change/T004-registered-sets.txt`). Nine of ten rows match C2 exactly:

`FIX40 0` ✓ · `FIX41 0` ✓ · `FIX42 0` ✓ · `FIX43 33` ✓ · `FIX44 59` ✓ · `FIX50 67` ✓ ·
`FIX50SP1 97` ✓ · `FIXT11 1` ✓ · `Orchestra FIX Latest 524` ✓

**`FIX50SP2` measures 502, NOT the 505 that C2 / T018 / the S0 oracle all state.** See
§ "BLOCKER B-1" below — this is a real pre-existing loader defect, not a measurement artifact.

---

## BLOCKER B-1 — three FIX50SP2 groups are silently dropped by the loader (pre-existing)

Found while taking T004's measurement; it invalidates two things the feature depends on.

### The measurement

The shipped `as_table_view()` registers **502** groups for FIX50SP2. The S0 oracle's model of the
same "before" predicate says **505**. The measured set is a strict **subset** — no extras. The three
missing tags:

| tag | name | message-reachable from |
|---|---|---|
| 1499 | `NoAsgnReqs` | `StreamAssignmentReport(CD)`, `StreamAssignmentRequest(CC)` |
| 1669 | `NoRiskLimits` | `PartyRiskLimitsDefinitionRequest(CS)`, `…RequestAck(CT)`, `PartyRiskLimitsReport(CM)`, `…ReportAck(DE)`, `PartyRiskLimitsUpdateReport(CR)` |
| 1919 | `NoPriceMovements` | `SecurityList(y)` |

Eight shipped FIX50SP2 messages therefore carry a declared, message-reachable repeating group that
the runtime cannot resolve at all.

### Root cause

`xml_loader.cpp`'s first-member scan (`:610-641`) resolves a `<component>` child **one level only** —
it iterates the component's *direct* `<field>` children and never recurses. All three groups have
**only `<component>` children**, and every one of those components contains only a nested `<group>`:

```
<group name="NoAsgnReqs">        -> <component Parties>                 -> <group NoPartyIDs>
                                    <component StrmAsgnReqInstrmtGrp>   -> <group NoRelatedSym>
<group name="NoRiskLimits">      -> <component RiskLimitTypesGrp>       -> <group NoRiskLimitTypes>
                                    <component RiskInstrumentScopeGrp>  -> <group NoRiskInstrumentScopes>
<group name="NoPriceMovements">  -> <component PriceMovementValueGrp>   -> <group NoPriceMovementValues>
                                    <component ClearingAccountTypeGrp>  -> <group NoClearingAccountTypes>
```

No direct `<field>` is reachable in one hop, so `first_field_tag` stays **0**, `group_first_field`
returns 0, and the group is never registered. This is a **silent** drop — no diagnostic today.

### What this does NOT affect — T023 and the feature's core mechanism are clear

For 1499 / 1669 / 1919 `group_first_field` returns 0 **both before and after** the predicate swap, so
FIX50SP2 stays a C2 **EQUAL** row — EQUAL at 502 rather than EQUAL at 505. The T023 predicate
replacement does exactly what the spec says. Only two things are actually wrong: C2's registered
*value* for this one row, and FR-023's precondition.

### What it does block

1. **The FIX50SP2 count in C2 / T018 / T006.** T018 asserts registered-after `= 505` by exact-set
   equality both directions; T006 requires the oracle to equal C2's registered-after column. Against
   the shipped loader both are **502**.

2. **FR-023's implementation (T011 / T012 / T013).** S0/K11's "no zero-member `<group>` warnings"
   measures `len(list(g)) == 0` — *literally childless*. The loader's actual failure mode is "no
   **resolvable** member", and under that (implementable) definition FIX50SP2 has **3**. Measured
   under the loader's own rule, groups with no resolvable first member:

   `FIX40 0 · FIX41 0 · FIX42 0 · FIX43 0 · FIX44 0 · FIX50 0 · FIX50SP1 0 · FIX50SP2 3 · FIXT11 0`

   Implementing T012/T013 as written makes **FIX50SP2 throw on load**, breaking a shipped dictionary,
   leaving T011 permanently RED, and falsifying FR-023 / P1-NON / K11's "zero vendored dictionaries
   are affected" plus the T051 release-note text.

Everything else proceeds: T002/T003, T009/T010 (synthetic fixtures), and the whole FIX42/FIX43 RED
batch (T015–T017, T019–T022, T040, T042) are untouched by this.

### DECISIVE MEASUREMENT — codegen does **not** share the defect

`v50sp2/Messages.hpp` in the T001 pre-change snapshot emits **505** `class G_`, and `G_1499`,
`G_1669`, `G_1919` are all present. (Controls: `v44` 59 — matching its 59 registered; `v42` 0.)
`ir.cpp`'s `walk_level` keys on the *element name* and pushes unconditionally, so it never consults
the loader's first-member scan.

So the runtime registers **502** while codegen emits **505**: a live **runtime-vs-codegen structural
divergence** — exactly the failure class T022's K6b pin exists to catch, found independently.

Consequence: repairing the loader's component scan to recurse **moves no golden** (codegen already
emits all 505), makes C2's 505 correct *as written*, makes FR-023's "zero vendored dictionaries
affected" *true*, and lets T006 / T011 / T018 pass **unedited**.

### Latent twin in the Orchestra loader (symmetry check)

`OrchestraLoaderState::first_member_tag`'s `fixr:componentRef` branch
(`orchestra_loader.cpp:495-513`) has the **same** one-level shape — it scans
`components_[…].node.children("fixr:fieldRef")` only, never recursing into a nested `fixr:groupRef`
or `fixr:componentRef`. OrchestraFIXLatest merely does not happen to contain a group whose members
all sit behind such a component (its 524 matched). Any fix must land on **both** loaders or it is a
half-restructure (`feedback_half_restructure_symmetric_api`).

### Status — RESOLVED, user decision 2026-07-30

**User chose: fix the one-level component scan in BOTH loaders** (`xml_loader.cpp` and
`orchestra_loader.cpp`), i.e. make the first-member scan recurse into nested `<component>` / `<group>`
(resp. `fixr:componentRef` / `fixr:groupRef`) instead of stopping at direct `<field>` children.

Consequences, all of which keep the *specified* design intact:

- Runtime FIX50SP2 registered goes **502 → 505**, converging on what codegen already emits.
- **No golden moves** — codegen is already at 505, so T027 (`v50sp2` read byte-identity) and T039
  (builder-set byte-identity) still hold.
- **C2's 505 is correct as written**; T006 / T011 / T018 need **no edit**.
- FR-023's "zero vendored dictionaries are affected" becomes **true**, so T012/T013's rejection is
  safe to implement and T051's release-note text stands.
- It is nonetheless a real behavior change — 3 groups across **8** shipped FIX50SP2 messages
  (including `SecurityList(y)`) become resolvable at runtime — so it needs its **own B&L row +
  release note** (fold into T050/T051) and may move `bench/baselines/dictionary/xml_loader.json`
  (T049 already flags that entry as the one whose no-move ground is narrowed).

### Added tasks (not in the original tasks.md)

- **T012a** RED: pin that FIX50SP2 registers **1499 / 1669 / 1919** with their correct member sets,
  and that the ten-dictionary registered counts are `0/0/0/33/59/67/97/**505**/1/524` pre-T023.
  Must be observed RED before T012b.
- **T012b** Implement the recursive first-member scan in `src/dictionary/xml_loader.cpp`.
- **T013b** Implement the symmetric fix in `src/dictionary/orchestra_loader.cpp`
  (`fixr:componentRef` branch, `:495-513`) — same change unit, per
  `feedback_half_restructure_symmetric_api`.

**Ordering constraint**: T012a/T012b/T013b must land **before** T012/T013 (FR-023's rejection),
otherwise FIX50SP2 throws on load. They must also land before T018's 505 pin can go GREEN.

### B-1a — the naive recursion is NOT surgical: it changes **8** groups, not 3

A by-construction simulation of one-level vs. fully-recursive document-order resolution over every
first-seen group in all nine `<fix>` dictionaries (FIX40–FIXT11) gives:

`FIX40 0 · FIX41 0 · FIX42 0 · FIX43 0 · FIX44 0 · FIX50 0 · FIX50SP1 0 · FIX50SP2 **8** · FIXT11 0`

Three are the intended `0 → tag` repairs. **Five already had a working delimiter and would get a
DIFFERENT one:**

| tag | group | delimiter now | after full recursion | why |
|---|---|---|---|---|
| 1677 | `NoPartyRiskLimits` | 1670 `RiskLimitID` | **1671** `NoPartyDetails` | 1st child is `<component PartyDetailGrp>`, which starts with `<group NoPartyDetails>`; the scalar 1670 is the **3rd** child |
| 1772 | `NoPartyEntitlements` | 1883 `EntitlementStatus` | **1671** `NoPartyDetails` | same shape |
| 40204 | `NoPhysicalSettlTerms` | 40205 `PhysicalSettlCurrency` | **40209** `NoPhysicalSettlDeliverableObligations` | same shape |
| 41599 | `NoLegPhysicalSettlTerms` | 41601 | **41604** | same shape |
| 42060 | `NoUnderlyingPhysicalSettlTerms` | 42061 | **42065** | same shape |

**Blast radius of those five: 103 distinct FIX50SP2 messages**, including `ExecutionReport(8)`,
`NewOrderSingle(D)`, `MarketDataSnapshotFullRefresh(W)`, `Quote(S)`, `TradeCaptureReport(AE)`,
`SecurityList(y)`, `MassQuote(i)`. (`NoPhysicalSettlTerms` alone reaches 93; `NoLegPhysicalSettlTerms`
61; `NoUnderlyingPhysicalSettlTerms` 76.)

The delimiter decides where each repeating-group **entry** starts on the wire, so changing it for
103 messages is a parse-behavior change of a completely different order from what 082 scopes.

**Which value is correct?** On fixpp's own stated rule, **the recursive one**. `orchestra_loader.cpp`
says it explicitly: *"A leading nested group's OWN count-field tag is the delimiter surrogate
(mirrors xml_loader.cpp treating a leading `<group name=X>` exactly like a `<field name=X>`
reference)."* For all five, the leading element in document order **is** a nested group, reached
through a `<component>` wrapper the one-level scan cannot see. So today's delimiters are wrong —
but they are wrong *pre-existing*, not wrong-because-of-082, and no 082 pin covers them.

(The QuickFIX reference engine could not be consulted — `reference-engines/` is not present in this
working copy, so this rests on fixpp's own documented rule, not on cross-engine confirmation.)

### Two variants of the approved fix

- **(A) Full document-order recursion** — corrects all 8. Most correct; but a 103-message parse
  change with no Gate A coverage, no interop re-run against QuickFIX/Fix8, and unmeasured bench
  impact.
- **(B) Fallback-only recursion** — recurse **only when the existing one-level scan yields 0**.
  Corrects exactly the 3 unregistered groups (8 messages); leaves all 5 existing delimiters
  untouched. Delivers everything 082 needs: 502 → 505, C2 correct as written, FR-023's precondition
  true, T006/T011/T018 unedited, no golden movement.

### B-1b — VERIFIED AT RUNTIME: the recursive values are already shipping, in the other loader

My 9-dictionary simulation covered only the `<fix>`-schema files and **missed
`OrchestraFIXLatest.xml`**. A runtime probe (`<scratchpad>/delim_probe.cpp`, loading both
dictionaries through the real `load_any` and reading `table_view::group_first_field`) shows
**8 of 8**:

| tag | group | FIX50SP2 today | Orchestra today |
|---|---|---|---|
| 1499 | `NoAsgnReqs` | **0** (unregistered) | **453** |
| 1669 | `NoRiskLimits` | **0** | **1529** |
| 1919 | `NoPriceMovements` | **0** | **1920** |
| 1677 | `NoPartyRiskLimits` | 1670 | **1671** |
| 1772 | `NoPartyEntitlements` | 1883 | **1671** |
| 40204 | `NoPhysicalSettlTerms` | 40205 | **40209** |
| 41599 | `NoLegPhysicalSettlTerms` | 41601 | **41604** |
| 42060 | `NoUnderlyingPhysicalSettlTerms` | 42061 | **42065** |

Orchestra's `first_member_tag` has a **direct `fixr:groupRef` branch**
(`orchestra_loader.cpp:478-494`) and FIX Latest declares these groups with a leading `groupRef`, so
that path already yields the recursive value. **Cross-loader parity for the same semantic groups is
broken today, and FIX50SP2 is the outlier.** This is independent corroboration that the recursive
values are the correct ones — fixpp already ships them.

Also a framing correction to B-1: the 3 "never registered" groups are **not inert** today. The bare
store skips them (`dictionary.cpp:403`) but the **context** store registers them with a
`members.front()` fallback delimiter (`dictionary.cpp:510-511`; members are tag-sorted, so 1499 gets
**146**). Strict validation of those 8 messages already runs against a bogus delimiter.

---

## BLOCKER B-2 — `consume_group` cannot parse a delimiter that is a nested group's count tag

**REPRODUCED AT RUNTIME** (`<scratchpad>/delim_repro.cpp`, built against `libfixpp_wire`):

```
REPRO   (2 instances): REJECTED  error=38 wire_required_field_missing  ref_tag=200
CONTROL (1 instance) : ACCEPTED
```

### Mechanism (`include/fixpp/wire/validator.hpp:357-406`)

`:357` opens each instance at `delim_tag`; `:362` consumes it with a bare `++i` and **no descent**.
The nested-group descent at `:376` applies only to members scanned *after* the delimiter. When the
delimiter **is** the nested group's count tag, the nested group's body fields follow — and they are
not members of the outer group (`dictionary.cpp:457-462` builds membership from the *immediate*
enclosing group only), so `is_member` fails at `:365` and the scan breaks. The outer `while` then
sees a non-delimiter tag, exits with `actual_count == 1`, and `:402` rejects on
`actual_count != declared_count`.

**It is silent on single-instance groups** — which is why it has never been noticed.

### Why this cannot be scoped away

Every one of the 8 delimiters is itself a group count tag — **under either variant**:

- narrow/fallback-only fix: 1499 → **453** `NoPartyIDs`, 1669 → **1529** `NoRiskLimitTypes`,
  1919 → **1920** `NoPriceMovementValues` — all three are `<group>` count tags;
- full recursion adds: 1677/1772 → **1671** `NoPartyDetails`, 40204 → **40209**,
  41599 → **41604**, 42060 → **42065** — likewise.

So **there is no version of the loader fix that avoids B-2.** The `consume_group`
descend-at-delimiter fix is a hard **prerequisite**, not an optional extra.

### It is already live, today, on unmodified `main`

Orchestra already carries all 8 of these delimiters (B-1b). Any Orchestra/FIX-Latest session
strict-validating a **multi-instance** `NoPartyRiskLimits`, `NoPhysicalSettlTerms`,
`NoLegPhysicalSettlTerms`, `NoUnderlyingPhysicalSettlTerms`, `NoPartyEntitlements`, `NoAsgnReqs`,
`NoRiskLimits` or `NoPriceMovements` **false-rejects a schema-legal message right now**. That is a
pre-existing, user-visible defect owned by no feature.

### Additional consequences (advisor-reported, not independently verified by me)

- **Codegen already assumes the recursive delimiter.** `ir.cpp:60-98` `walk_level` recurses through
  components and sets `delimiter_tag = members.front().tag`, so emitted builder plans already order
  these groups 1671-first — meaning a fixpp-typed-builder-built message is today rejected by fixpp's
  own strict validation for these groups.
- **C-ABI flip.** `src/capi/message_write.cpp:718-722` enforces delimiter-first at commit, so
  clients building the five groups 1670-first (accepted today) would get `TYPE_MISMATCH`. GA-frozen
  ABI ⇒ needs a B&L row and Gate B visibility.
- **Nothing pins any of the 8 delimiters.** No existing test covers them, and 082's T005 oracle
  extension censuses *member sets*, not delimiter values — so all of this would land pin-free
  without an added delimiter leg.
- **Interop trade-off that cannot be settled locally:** all members of these groups are
  schema-optional, so *any* delimiter choice rejects some schema-legal shape. A counterparty opening
  instances with 1670 is accepted today and rejected after the fix. Belongs in the per-release
  QuickFIX/Fix8 interop gate.

**Escalated to the user (third time).** The first approval rested on "3 groups / 8 messages"; the
second on full-vs-narrow recursion. Neither accounted for B-2, which makes a wire-validator change a
prerequisite for *any* loader fix here.

---

## S0 / S0b — design-time oracles reproduced at the branch point

Both scripts read XML only, so they are invariant across T023 and serve as the standing ground
truth the C++ pins assert against.

### S0 `contracts/predicate_census.py` — reproduced exactly

- FIX40 `0/4` DIFFER · FIX41 `0/7` DIFFER · FIX42 `0/18` DIFFER
- FIX43 `34/34` DIFFER, `82` type-only, `576` struct-only, registered `33 -> 34`
- FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT11 / FIX-Latest **EQUAL**, registered
  `59 / 67 / 97 / 505 / 1 / 524`
- "declared `<group>` not message-reachable" note lists `384` / `627` on FIX50 / SP1 / SP2
- **no zero-member `<group>` warnings on any of the ten** — the FR-023 no-regression evidence
  (the fail-closed load rejection affects **0** shipped dictionaries) and the standing
  measurement behind contract **P1-NON** / K11.

FIX42's 18 registering tags: `33, 73, 78, 124, 136, 146, 199, 215, 267, 268, 295, 296, 382, 384,
386, 398, 420, 428`.
FIX40's 4: `73, 78, 124, 136`. FIX41's 7: `33, 73, 78, 124, 136, 146, 199`.

Full transcript: `<scratchpad>/pre-change/S0-predicate-census.txt`.

### S0b `contracts/builder_plan_census.py` — T031's derivation, taken BEFORE the first run

Self-validation legs all `OK` (v44 all 83/88 name-for-name vs golden; v50sp2 all 156/558 likewise;
v44 official 33/54 vs `determinism_test.cpp`'s constant), then the `v42` **prediction**:

| mode | messages | distinct plans | tags with a plan | emitted files | registry |
|---|---|---|---|---|---|
| `--families all` | 39 | **28** | **17** | **226** = 39×5 + 28 + 3 | 39 |
| `--families official` | 25 | **19** | **11** | **147** = 25×5 + 19 + 3 | 25 |

Ordinaled tags (`all`): `{73: 3, 78: 2, 146: 4, 268: 2, 295: 3, 296: 2, 420: 2}`.
Ordinaled tags (`official`): `{73: 2, 78: 2, 146: 3, 268: 2, 295: 3, 296: 2}`.

`384 NoMsgTypes` is one of the 18 read-tier tags but **not** one of the 17 builder-tier tags —
its only FIX42 host is the admin message `Logon`, excluded by `emit_builders`' `is_application`
gate. This is derived from the interning rule, **never transcribed from a first run** (T031/T033).

Full transcript: `<scratchpad>/pre-change/S0b-builder-plan-census.txt`.

---

## Observed-RED transcripts

A pin never seen RED proves nothing (`feedback_sanitizer_canary_must_be_proven_red`). Each batch
below was authored, built, and **run against unchanged production code**, and its failure output
recorded, before the corresponding implementation task landed.

### Phase 2 — T005–T008 (oracle extension), landed

The oracle (`tests/dictionary/required_scope_oracle.hpp`, `+98`) gained `DictOracle::group_tags`,
populated inside the **existing** `qfix_walk` `"group"` branch and `orch_walk` `"fixr:groupRef"`
branch — no third walker, and reachability-restricted by construction (the walk only visits
header / trailer / message subtrees). Measured output, all ten exact against C2's registered-after
column: `4 / 7 / 18 / 34 / 59 / 67 / 97 / 505 / 1 / 524`. T007's zero-member count is **0** on all ten.

T008 re-pointed `reused_tag_census.hpp:74,80` onto `oracle.group_tags`.

**Why T008 does NOT use `Dictionary::group_first_field()`** (a finding worth keeping): the handle's
global `groups_` table is populated once per **declared `<component>`** regardless of message
reachability (`xml_loader.cpp:954-970`'s unconditional per-component `expand_field_list`), so a
sweep over it yields the unrestricted **struct** set (69 / 99 / 507 on FIX50 / SP1 / SP2), not the
registered set. This is the declared-vs-registered distinction C2 already documents. Note this is a
*different object* from `table_view::group_first_field`, which is what T004 measured and what C2's
"registered" column means — the two are not interchangeable.

#### EXPECTED-RED, pending T023 — do not mistake for a regression

Re-pointing the census activates FIX40/41/42 group discovery inside `collision_membership_guards_test.cpp`
(063/T017's file), which derives `TEST_P` cases from the census but asserts through
`as_table_view()`'s still-datatype-gated `group_member_tags(...)`. **10 failures, all newly-discoverable
FIX40/41/42 and nothing else:**

- `CollisionMembershipGuards.CoversEveryCensusedCollisionExactly` — hardcoded `expected_total=69`
  now sees 78, plus its three "FIX40/41/42 contribute zero" assertions.
- `PerCensusedCollision/…/ContextResolvesToTheCorrectVariant/` × 9 — `FIX40xml_tag73`,
  `FIX41xml_tag73`, `FIX42xml_tag{73,78,146,268,295,296,420}`.

Full suite `dictionary_pure_tests` **276/286**, exactly these 10 red, nothing else moved.
`RequiredScopeCensus.*` 4/4 and `RequiredScopeParity.*` 2/2 still green. The file was deliberately
**left untouched** — it is T023's to resolve, and silencing it now would destroy the RED evidence.

#### T006's landed constant pin

The oracle's ten-dictionary output is now pinned against **literal constants** in
`reused_tag_census_test.cpp` (nine in the `kRuntimeDicts` loop + a separate Orchestra-FIX-Latest
test, since `kRuntimeDicts` is QuickFIX-only), together with the zero-member count `== 0` per
dictionary, and exact **set** equality both directions for FIX40 / FIX41 / FIX42.

This is deliberately **not** an oracle-vs-loader comparison. An oracle-vs-actual pin passes whenever
both sides drift together — e.g. if the oracle were later "corrected" to match a defective loader.
Pinning the oracle to constants is what makes that failure mode visible. A comment at the pin says
so, to stop a future reader "simplifying" it into an actual-side check.

**Orchestra note:** `dictionary_pure_tests` had no `FIXPP_ORCHESTRA_DATA_DIR` macro, so one
`target_compile_definitions` line was added to `tests/dictionary/CMakeLists.txt`.

**Independently verified by the orchestrator** (not taken from the agent's report):
`dictionary_pure_tests` → **287 tests, 277 pass, 10 fail**, the 10 being exactly the expected
collision-guard set. The FIX50SP2 oracle constant is **505** here — correct, because this pin is the
raw-XML structural truth; the *loader-side* pin (T018) uses **502**. Both are pinned on purpose.

### Phase 3/5 — pre-T023 RED batch (T015–T018, T040, T042), OBSERVED RED

`dictionary_required_scope_census_test` → **10 tests, 5 pass, 5 fail**. Orchestrator-verified by
running the binary, not taken from the agent's report. Transcripts:

| pin | state | observed |
|---|---|---|
| **T015** FIX42 18 tags | **RED** | oracle `{33,73,…,428}` (18) vs actual `{}` |
| **T016** FIX40 / FIX41 | **RED** | `{73,78,124,136}` and `{33,73,78,124,136,146,199}` vs `{}` |
| **T017** tag-146 | **RED** | all 6 contexts + the bare leg, expected sets vs `{}` |
| **T040** FIX43 576 | **RED** | `group_first_field(576)` = 0; members `{}` vs `{577}` |
| **T042** FIX43 delta | **RED** | actual 33 tags vs `baseline+1` = 34, `missing{576}` |
| **T018** six unchanged | **green** | non-regression witness — green by design, see below |

**T018 is green now and stays green — that is correct, not a false green.** All six are C2 **EQUAL**
rows (type set == struct set), so replacing the datatype gate with the structural one cannot move
them. It is FR-014/SC-002's *non-regression* leg: its job is to go RED if T023 accidentally moves one
of the six. Its FIX50SP2 row is pinned at **502**, derived as `oracle.group_tags` minus
`{1499,1669,1919}` (not hand-transcribed) with a `#208` citation and a note that it flips to a plain
`oracle.group_tags` comparison once #208 lands.

#### T017 needed a second leg — added by the orchestrator

As first written, T017 pinned only the **context** store. The task text requires both, and the gap
was load-bearing: a T023 implementation that populates the context store correctly while leaving the
**bare** store wrong (or vice versa) would have passed. That is precisely the half-restructure FR-004
exists to prevent, and T015 does not close it — T015 pins the bare store's registered *tag set*, not
tag 146's *member set*.

The added leg pins `tv.group_member_tags(146)` (bare overload) to the loader's **first-seen** variant.
The expected value is **derived, not transcribed**: `News(B)` is the first `<message>` in FIX42.xml
document order that reaches a `<group name="NoRelatedSym">`, so its member set is what `finalize()`'s
`group_fields_` expansion records for the bare `no_tag`. Verified independently to be the 19-member
`{News, Email}` variant `{22,46,48,65,106,107,167,200,201,202,205,206,207,223,231,348,349,350,351}`.

Observed RED: `FIX42 tag 146 (BARE store, first-seen wins): missing-from-actual{22,46,…,351}`.

Both legs now fail for the right reason (`group_bit(146)` is clear pre-T023), and they assert
genuinely different things — per-context sets vs the single first-seen set.

### Phase 3 — cross-cutting RED batch (T019–T022, T021b), OBSERVED RED

Each binary re-run independently by the orchestrator, not taken from the agent's report.

| pin | binary | state | observed |
|---|---|---|---|
| **T019** | `codegen_082_v42_group_classes_test` | **RED** | `class G_` count **0**, expected **18** (the 46 message classes already match and correctly do *not* fail) |
| **T020** | same | **RED** | `group_ids.contains(296)` false — no `G_296` (`NoQuoteSets`) flyweight |
| **T021** | `test_082_ungated_group_parse` | **RED** | `NoOrders(73)=2` yields **0** group instances, expected 2 — identically for FIX40/41/42 |
| **T021b** | `test_082_group_required_member_validation` | **RED** | all 14 oracle-derived pairs: omitting a required group member is **accepted**, must be rejected |
| **T022** | `capi_082_group_detection_cross_path_test` | **RED** | write-path set (18 tags) vs bare registered set `{}` |

**T022's RED is the informative one.** It fails not because the write path is broken but because the
write path is **already structural** — `fixpp_msg_group_begin` succeeds for all 18 FIX42 tags today
(`src/capi/message_write.cpp:812` uses `Dictionary::group_first_field`) — while the read-side bare
store is still datatype-gated and empty. That is exactly the **cross-path divergence K6b exists to
catch**, and it confirms C4.4 empirically rather than by assertion. After T023 both sides should be
the same 18 tags.

**T021's SC-008a first leg already holds today** (`from_app_calls == 1`, session stays `Active` with
the strict flag off) and is correctly *not* among the failures — it is a non-discriminating leg whose
job is to survive T023 unchanged.

#### Follow-up to fold into T023's phase — `kDelimTags`

`test_082_group_required_member_validation_test.cpp` carries a hardcoded `kDelimTags` table mapping
each `(msg_type, path, no_tag)` to its delimiter, used **only** to construct valid wire bytes; no
delimiter value is ever asserted, and a missing entry throws loudly at construction. That is
acceptable at RED time — the delimiter cannot be derived from the code under test without
circularity, and nothing else in the repo pins delimiters (which is itself part of #208).

**Once T023 lands the delimiters become derivable, so add a self-check**: assert each `kDelimTags`
entry equals `tv.group_first_field(msg_type, path, no_tag)`. That converts a bare hardcode into a
*checked* hardcode at no cost. Tracked here so it is not lost.

### Phase 2 loader — FR-023 rejection (T009–T014), RED → GREEN

RED captured **before** the loader edits: both `MemberLessGroupAtNonFirstSeenOccurrenceThrows*` pins
failed with `threw == false` — i.e. no throw occurred, not a setup error. T011 (ten dictionaries load
clean) was **green-by-construction from the start**, which is itself the evidence that the narrowed
rejection never touches a real dictionary.

Implementation placed **outside** the first-seen dedup guard in both loaders
(`xml_loader.cpp` before `:609`'s `group_index_by_no_tag_.contains`, `orchestra_loader.cpp` before
`:626`), so the rule is occurrence-independent rather than order-dependent. Diagnostic names both
`name` and `no_tag`. No new exception subclass and no `fixpp::core::error` variant, so FR-017/SC-009
and `test_020_error_completeness.cpp`'s slot pin are untouched.

After: `dictionary_required_scope_census_test` **13 tests, 8 pass, 5 fail** (the 3 new pins GREEN; the
5 pre-T023 pins unmoved). `dictionary_pure_tests` back to **287 / 277 / 10**. Both
orchestrator-verified. All ten dictionaries load clean, message counts
27 / 28 / 46 / 68 / 93 / 93 / 105 / 156 / 8 / 181.

#### Collateral finding — FR-023 is NOT theoretical for third-party dialects

`tests/dictionary/lookup_test.cpp`'s `load_small_dictionary()` fixture declared two **literally
self-closing** `<group name='NoPartyIDs'/>` / `<group name='NoLegs'/>` tags, deliberately, to
exercise the empty-group accessor paths (`group(453)->field_count == 0`, `group_fields(453).empty()`).
FR-023 rejected it, surfacing 2 failures beyond the expected 10.

Fixed by giving each group one zero-field `<component name='Empty'/>` child: the resolved shape is
unchanged (`field_count` still 0, `group_fields()` still empty), so the accessor paths under test are
preserved, while the fixture moves out of FR-023's literal scope. A repo-wide grep for other
self-closing `<group>` / `<fixr:group>` fixtures found none besides fuzz-corpus seeds, which are not
required to load cleanly.

**Two things to carry forward from this:**

1. **T051's release note should say this shape occurs in practice.** "Zero vendored dictionaries are
   affected" is true, but a checked-in fixture *in this repo* used the rejected shape — so the
   rejection is a real behavior change for third-party dialects, not a theoretical one. That is a
   stronger and more honest operator-facing rationale than the bare zero-impact claim.
2. **#208 interaction.** The replacement fixture is deliberately the *residual* shape (children
   present, resolving to nothing) — the same category as FIX50SP2's 1499/1669/1919. If #208 is later
   scoped to *also* extend FR-023's rejection to "no resolvable member", this fixture will start
   throwing and must be revisited. #208's suggested scope (recursive scan, no new rejection) does not
   break it, but the coupling is worth naming.

---

---

## ⏸ PARKED 2026-07-30 — resume after issue #210 lands

**User decision:** park 082 here, fix [#210](https://github.com/CatalinSerafimescu/fixpp/issues/210)
on its own branch, then resume. #210's fix makes three of 082's pins *stronger*, so finishing them
first would mean writing concessions we would immediately delete.

**Branch #210 off `main`, NOT off 082.** #210 reproduces entirely on shipped dictionaries (42
polluted contexts on FIX44/50/50SP1/50SP2, all C2 **EQUAL** rows, so T023 provably changes nothing
for them — T018 passing before *and* after is the witness). A branch off 082 would carry 082's
unfinished commits into #210's ancestry; rebasing before merge would then mean the artifact tested is
not the artifact merged, and #210's pins developed over T023 would cover FIX42 contexts that do not
exist on `main`.

### State at park

| item | state |
|---|---|
| Phases 1, 2 (T001–T014) | **complete**, committed |
| Phase 3 RED batch (T015–T022, T021b, T040, T042) | **complete**, RED observed and recorded |
| T023–T025 (predicate + codegen re-point) | **complete**, committed |
| T030 (carve-out refresh) | **complete** — 5 files, see below |
| `dictionary_pure_tests` | **287 / 287 GREEN** |
| `dictionary_required_scope_census_test` | **13 / 13 GREEN** |
| T026–T029 (regenerate, golden diffs, consistency gate) | **NOT STARTED** |
| T021b | **blocked on #210** — deliberately left unrestricted |
| Phases 4–7 (T031–T055) | **NOT STARTED** |

### T030 as delivered (5 files, not the 4 tasks.md names)

Two carve-outs were **inverted, not deleted** — each carried an explicit instruction to flip when
#196 landed, and 082 *is* #196:

- `required_scope_test.cpp` — `Fix42GroupCountFieldIsIntTypedContextStoreBlindL0661` →
  `Fix42IntTypedGroupCountFieldNowResolvesInContextStore`. Keeps the `FieldRef::type == Int`
  assertion deliberately: that is what makes it a behavioural FR-001 witness rather than a tautology,
  and it is the test that would catch a reintroduced datatype gate.
- `required_scope_census_test.cpp` — `PerGroupContextStoreIsEmptyForL0661GroupBlindDicts` →
  `PerGroupContextStoreIsPopulatedForFormerlyGroupBlindDicts`. Asserts `group_required_members`,
  which is **not** affected by #210 (the pollution goes through `add_group_member_ctx`, not the
  required-member store), so plain equality is correct there. Output: FIX40 **6** contexts, FIX41
  **10**, FIX42 **38** — previously zero.
- `collision_membership_guards_test.cpp` — added to T030's scope (tasks.md named only 4 files).
  Banner rewritten, `expected_total` 69 → **78** with per-dict tallies FIX40 1 / FIX41 1 / FIX42 7,
  and a latent weakness fixed: the zero-check used `std::map::count`, which returns only 0 or 1, so
  it could report presence but never magnitude — it would have passed whether a dictionary
  contributed 1 case or 70. Now asserts the tally via `operator[]`.
- `reused_tag_census_test.cpp` + the census helper — done earlier under T008.
- **`delimiter_census_test.cpp` — a FIFTH file, and it was MISSED until CI found it.** See below.

### The pin T030 missed — `delimiter_census_test.cpp` (found by CI, not by this branch)

083 planted `DelimiterCensus.IntTypedOutOfCheckedSetIsExactlyFiftyFive` as a deliberate tripwire, with
an in-source instruction to update it *"only if the cause is confirmed (e.g. **#196 landing**)"*. 082
is #196, so it fired — and **nothing in this feature's task list enumerated it**. It failed every leg
of tier1, tier2 and tier3 on the first CI run of the branch (PR #261), 358/359 passing.

**The population is identical to the one T030 already recorded.** That section states FIX40 **6** /
FIX41 **10** / FIX42 **38** contexts for `required_scope_census_test.cpp`'s inversion; the delimiter
census pinned **6 / 10 / 38 / 1** for the same contexts under a different name. The matching numbers
were sitting in this document while the pin went unnoticed — the miss was in the *enumeration*, not
the evidence. Same class as the #208 carve-out miss: a list built from one file set cannot see a
sibling asserting the same thing elsewhere. Enumerate by **the instruction to flip on #196**, which is
greppable, not by the files a task happens to name.

**Fixed by INVERTING it, not by zeroing it** (`IntTypedCountTagContextsAreExactlyFiftyFiveAndNowRegistered`).
Structural detection registers the whole population, so `int_typed_out_of_checked_set` is now 0 on all
ten dictionaries — but *0 alone is a vacuous pin*, satisfied equally by a census that stopped
measuring, which is the exact failure this tripwire existed to prevent. So the 55 **moves** rather than
dissolving: a new `int_typed_registered` bucket, classified by the same `FieldRef::type` test (082 does
not change `FieldRef::type` — D-4), carries the same 6/10/38/1 breakdown. Measured:

| | contexts | int_typed_oos | int_typed_reg |
|---|---:|---:|---:|
| FIX40 | 6 | 0 | **6** |
| FIX41 | 10 | 0 | **10** |
| FIX42 | 38 | 0 | **38** |
| FIX43 | 235 | 0 | **1** |
| other six | 55 992 | 0 | 0 |
| **TOTAL** | 56 276 | **0** | **55** |

**Mutation-proven (M1):** forcing `checked_set_status` never to return `kNotNumInGroup` — the
classifier a reintroduced datatype gate would defeat — drops `int_typed_registered` to 0 and takes all
five assertions RED. ⚠️ Note **the four zero-assertions still PASSED under M1**; only the 55-pin
failed. That is the whole argument for inverting instead of zeroing, demonstrated rather than claimed.
The complementary library-side mutant (re-introducing the datatype gate in `dictionary.cpp`) was **not**
run — it triggers the full codegen cascade — but that direction is already covered by
`RequiredScope.Fix42IntTypedGroupCountFieldNowResolvesInContextStore`.

### Carve-out residue the same enumeration found — OPEN, all three GREEN in CI

Having been bitten by enumerating from a task list, the fix pass re-derived the population properly:
`grep -rln '#196\|L-066-1\|L-063-1\|L-061-1\|L-077-1' tests/ contracts/` → **16 files**. Five are the
T030 set (incl. the delimiter census above), the rest are 082-authored — except **three that assert a
premise 082 falsified**. None fails: they are stale *documentation and coverage scope*, invisible to a
red/green signal, which is exactly why the grep matters. **Not fixed here** — this commit is scoped to
the CI-red test. Phase 7 / T050 should absorb them:

1. **`tests/wire/required_scope_two_tier_test.cpp:33-35`** — the strongest of the three, because it is a
   *coverage* claim, not a comment: *"FIX42 excluded entirely: no generated typed `validate_<Msg>`
   (`tools/codegen/fixpp-codegen/main.cpp:132` `if (ir.ns != "v42")` — L-077-1/#196)"*. **T035 deleted
   that condition**, so the cite names a line that no longer exists and the stated reason for excluding
   FIX42 from this suite is gone. FIX42 now *has* generated typed validators — this file should either
   cover them or say why not.
2. **`tests/wire/validator_type_check_test.cpp:974-982`** — describes L-066-1 as live and *"explicitly
   deferred to issue #196 … NOT something T012 authorizes fixing here"*. 082 closes it, so whatever
   group-path assertion was withheld on that basis is now writable.
3. **`tests/support/fix44_dictionary.hpp:18`** — carries the *"FIX43/FIX44/FIX50/FIX50SP1/FIX50SP2/
   FIXT.1.1 group-registering scope (L-063-1)"* six-dictionary enumeration. Comment-only, and it is the
   exact scope note FR-019/T050 already requires widening — listed so the T050 pass has the site.

### Concessions to #210 that must be REVERTED when it lands

> ## ✅ ALL THREE DISCHARGED 2026-08-12 — and there was a FOURTH, to #208
>
> | # | Revert | Acceptance used | Result |
> |---|---|---|---|
> | 1 | `required_scope_census_test.cpp` — bounded allowance → plain `EXPECT_EQ(members, actual)` | **Strengthening** (one permitted extra tag → none), so a green *is* proof | **13/13** |
> | 2 | `collision_membership_guards_test.cpp` — `first_tag_only_in`'s `exclude` removed | ⚠️ a **widening**, so green is NOT proof. Used the comment's own prediction — *"the exclusion becomes a no-op"* — checked against the **derived case set**, observable as the parameterised test-name list | **80/80**, and the case list is **byte-identical**: 78 cases, per dict 1/1/7/9/12/13/14/21 |
> | 3 | `test_082_group_required_member_validation_test.cpp` T021b | already discharged by measurement (0 failures) | green |
>
> **#210's fix is on the CALLER side** — 083 T031/T032 made the delimiter source each context's own
> declaration, so `table_view.hpp:645`'s unconditional `add_group_member_ctx(..., first)` is
> **unchanged and meant to stay** (D-5 / C-3.3 — the injection is now a no-op). Reading `table_view.hpp`
> alone concludes #210 is unfixed. Verified in this tree before reverting anything.
>
> **⚠️ A FOURTH carve-out existed and is not on this list** — conceded to **#208**, which 083 also
> closed: `required_scope_census_test.cpp`'s `expected.erase(1499/1669/1919)`. This list was built by
> grepping `#210`, so a carve-out citing a different issue was invisible to it. Retired separately; see
> the § *A FOURTH carve-out* below and
> [[feedback_a_carveout_list_built_by_grepping_one_issue_number_misses_the_others]].
>
> For #2, the acceptance instrument is worth keeping: **the parameterised test-name list IS the derived
> case set**, so a set-identity check on it is free and strictly stronger than a count. It is recorded in
> `first_tag_only_in`'s banner so a future change cannot silently re-baseline it.

All three are commented in-place as such:

1. **`required_scope_census_test.cpp` T017** — the per-context member-set leg allows exactly one
   extra tag, the global first-seen delimiter, and nothing else. Collapses to plain
   `EXPECT_EQ(members, actual)`.
2. **`collision_membership_guards_test.cpp`** — `first_tag_only_in` takes an `exclude` parameter so
   the injected delimiter is never chosen as a discriminator. Becomes a no-op; the parameter can go.
3. **`test_082_group_required_member_validation_test.cpp` T021b** — left **unrestricted and RED** on
   purpose. 5 of its 14 contexts fail because the runtime delimiter is global, not per-context. Do
   not write a restriction; #210 fixes it outright.
   **→ DISCHARGED 2026-08-11 by measurement (#210 landed with 083).** The binary is now fully
   GREEN: **0 baseline-construction failures and 0 omission-rejection failures**, both counts
   measured, not projected — `build/linux-clang-debug/bin/test_082_group_required_member_validation`
   at `87fcf5a8`, 2 tests / 32 ms, including `KDelimTagsAgreesWithRuntimeGroupFirstField` (the pin
   that originally *proved* the delimiter global). The green is not vacuous: the #210 carve-out at
   `:492-503` `continue`s **without** incrementing `cases_checked`, and `:526` asserts
   `cases_checked == 13`, so a still-excluded case fails the test rather than vanishing from it.
   ⚠️ **083's handoff projected that 2 of the 14 would still fail** (`spec/behaviors-and-limitations.md`
   § *Handoff to 082*). That projection is **refuted by measurement — the number is 0.** Read the two
   counts separately: the "5" above is *baseline-construction* failures; the pin body's "RED today"
   is the distinct *all-14 omission-rejections* count. Both are zero.

### The #210 severity escalation found here

The `kDelimTags` self-check (added because T023 made delimiters derivable) proved the per-context
delimiter is **global**, so strict validation rejects schema-valid messages with
`wire_unexpected_tag` (error 42):

```
msg_type=R no_tag=146: table says 55 (Symbol), runtime returns 46 (global first-seen)
baseline FAIL msg_type=R no_tag=146 ref_tag=46 error=42
```

5 of FIX42's 14 required-group contexts affected (`R/146`, `V/146`, `X/268`, `i/295`, `l/420`).
Posted to #210; it invalidates that issue's cheaper fix option (b), since removing the member
injection alone leaves the delimiter global and the false rejection intact.

**Coupling to #208:** per-context delimiter resolution picks each context's true first member in
document order. Where that member is a nested group's count tag, #208's B-2 (`consume_group` cannot
open an instance on such a delimiter) bites immediately. FIX42's five are safe (`55 Symbol` etc. are
plain fields); FIX50SP2's may not be. #210's RED pin must check this explicitly.

---

## RESUMED 2026-08-11 — post-merge baseline (measured)

Branch caught up to `main` by **merge** (not rebase — the branch is published and its Gate A record
cites branch SHAs): merge commit `87fcf5a8`, **0 behind / 18 ahead**. Conflicts were 2 files, both
additive-append (`.specify/feature.json`; `tests/session/CMakeLists.txt` — kept both 082's T021/T021b
blocks and main's 088 blocks). All four stale preset build trees were deleted and rebuilt cold.

**The parked record's central premise was wrong.** The resume brief held that 082 forked **pre-083**
and must absorb 083's loader rewrite. Measured: `git merge-base --is-ancestor 1b9356bd 75965e3b` →
**YES** — `1b9356bd` is 083's squash merge, so **the fork base already contains 083**. Corroborated
by main's drift over `src/dictionary/` since the base being one file (`CMakeLists.txt`, +2/−1). There
was never a merge collision to absorb, and 083's *"082 is parked on an unbuildable branch"* is retired:
the merged tree **builds clean, 2771/2771 edges, zero errors**.

### FR-023 × 083 — the collision is REAL, but it is not the predicted one

The brief predicted *"083's FR-023 throws and every shipped dictionary fails to load — immediate and
total."* **Refuted:** `LoaderDisposition.AllTenShippedDictionariesLoadUnderFailClosedDefault` **passes**.
What is actually broken is 4 cases in `tests/dictionary/loader_disposition_test.cpp` — **main's own
file, untouched by this branch** (`git diff --name-only 75965e3b..HEAD` on it is empty), so 082's
loader edits are the cause. 083 chose a **member-less** group as its representative *unresolvable*
fixture, so both features act on the same input class with opposite dispositions, in two legs:

| Leg | Test | 083 requires | 082 FR-023 does |
|---|---|---|---|
| **1 — tolerant opt-in** | `UnresolvableGroupSkippedUnderTolerantOptIn`, `ContextWithoutDelimiterRecordTolerantModeSkipsGroup`, `OrchestraRejectionIsOrchestraParseError` (its tolerant half, `:370-374`) | FR-006a / FR-023a / C-6.4 — skip the group, load the rest, identically in **both** loaders | throws unconditionally |
| **2 — DEFAULT policy** | `DeclaredGroupWithZeroContextsDoesNotFailClosed` | FR-006d / C-3.6 — a group reachable from **no** message expansion is *informational, never a load rejection* | throws |

Leg 2 has no policy escape: the fixture is `<component name='UnreferencedComp'><group name='NoBad'
required='N'></group></component>` — member-less **and** context-less — and both requirements speak at
the default policy. `UnresolvableGroupRejectedUnderFailClosedDefault` **passes**, so the two features
already agree on the default *for a referenced* group. **OD-1 (user, 2026-07-30) predates 083's
`unresolved_group_policy` and did not contemplate the opt-in; the layering is an open decision.**

### Outcome of the FR-023 amendment (measured 2026-08-11, after the removal)

All **11** `LoaderDisposition` cases GREEN, including both required PASSes that would have refuted the
removal — `AllTenShippedDictionariesLoadUnderFailClosedDefault` (1512 ms) and
`AllShippedContextsHaveADelimiterRecord` (2863 ms). So 083's *"emitted no first member"* predicate and
082's removed *"no literal child"* scan are equivalent on all ten shipped dictionaries.

**One real gap surfaced, and it was in 083's diagnostic, not in the layering.** Re-pointing FR-023's
own pins onto 083's disposition failed T010: the Orchestra message read `<fixr:group> with
<fixr:numInGroup id="100">` — it names the `no_tag` but **not the group's `name`**, which FR-023
requires and which the `<fix>` twin already prints. Fixed at `orchestra_loader.cpp`'s fail-closed
throw by adding `name="…"`, degrading to the id-only form when the optional attribute is absent.
The rejection itself fired correctly, with the derived `orchestra_parse_error`, **at a non-first-seen
occurrence** — so 083's check being outside the dedup guard is confirmed by test, not just by reading.

⚠️ **Baseline coverage hole, recorded so it is not repeated.** The `ctest -R 'dict|codegen'` sweep used
for the baseline does **not** match FR-023's own pin: its ctest name is **`required_scope_census`**,
not `dictionary_required_scope_census_test`. T009–T011 were therefore outside *both* the before and
after runs, and T010's failure was found only by running the binary directly. Any FR-023 re-check must
name that test explicitly.

`fixpp::dict::codegen-build-graph-check` failing post-edit is **not** a regression: its assertion is
`git status --porcelain` being empty, and it was listing the two modified loader files. It is a
codegen-staleness gate that requires a committed tree; it also reconfigures the build tree while it
runs, so keep `ctest` at `-j2` (see project memory `feedback_build_resource_cap_oom`).

### Other post-merge RED, attributed

- `test_067_emit_builders_unit.cpp:742,760` (`Group077DedupSoundness`, 3 cases) — synthetic tags
  `9002`/`9003` are `NUMINGROUP`-typed but declare no group structure, so T024/T025's re-point onto
  `VersionIR::group_tags` stops emitting their `G_*Args` structs. A 067 fixture resting on the old
  datatype gate; **not** covered by T032/T033, which only invert 077's expectations.
- `DeterminismTest.{GeneratedMatchesGolden,AdditiveOffOnByteDiff}` — **expected at this parking
  point**: T026/T028 (regenerate + classify the emitted artifacts, regenerate the v42 golden) are
  still unchecked.
- `fixpp::dict::read-tier-byte-diff` — **2 artifacts diverged from the pre-077 baseline**, reported by
  the gate itself as *"a real read-tier regression, not test noise"* (FR-009/SC-005). Present in the
  baseline, i.e. **not** caused by the FR-023 amendment. This is exactly what **T027** exists to
  settle — it requires the `v44`/`v50sp2`/`vt11`/`vlatest` read goldens to be **byte-identical**, the
  discriminating check that the new predicate is set-equal wherever contract C2 says EQUAL. **Two are
  not.** Treat it as the next substantive item, and identify the two artifacts before any golden is
  regenerated: T028 regenerates the `v42` golden *by construction*, and reaching for that on a `v44`+
  divergence would enshrine a real defect.

### Also to reconcile

`tasks.md` shows **T023–T025 unchecked**, but commit `027eef20` and the branch diff show the
predicate swap and the emitter re-point already applied. Reconcile **by reading code**, not by
trusting the boxes — a task closable only by inspection is the false-green shape 083's handoff
named. Tally at resume: 26 done / 31 open.
**→ RECONCILED 2026-08-12 by code reading; see the section below.**

---

## RESUMED 2026-08-12 — T023–T025 reconciled, and the byte-diff "regression" dispositioned

### T023 / T024 / T025 are APPLIED (read from code, not from the boxes)

| Task | Site | Read state |
|---|---|---|
| T023 | `dictionary.cpp:489-492` bare loop | `legacy_first = group_first_field(legacy_no_tag); if (legacy_first == 0) continue;` — datatype gate gone, and the once-tautological guard is **folded into the predicate** exactly as T023 prescribes |
| T023 | `dictionary.cpp:528` `immediate_parent` | `if (group_first_field(fr.tag) != 0)` |
| T023 | `dictionary.cpp:533` context loop | `if (group_first_field(fr.tag) == 0) continue;` |
| T023 | `dictionary.cpp:550` `members.empty()` | **unchanged**, as T023 requires (post-detection registration guard, outside the predicate) |
| T024 | `ir.hpp:151` + `ir.cpp:731-739` | `VersionIR::group_tags` exists, populated on **both** schema paths (`collect_group_tags` / `collect_orchestra_group_tags`), header/trailer unioned, sorted+unique |
| T025 | all **8** sites | `emit_messages.cpp:169,237,340,350,428` (5) + `emit_reify.cpp:218,228` (2) + `emit_builders.cpp:607` (1) all call `is_group_tag(ir, …)`, whose body is `std::binary_search(ir.group_tags…)` — structural, not datatype |

The only residual `NumInGroup` token in `dictionary.cpp` is inside a comment (`:512`). `emit_manifest.cpp:73`
is correctly left alone (dispositioned NO CHANGE). Line numbers have drifted from `tasks.md`'s by +1…+91;
the **cites in `tasks.md` are pre-change and should be read as such**, not re-pointed
(cf. [[feedback_stale_anchor_repoint_to_a_plausible_twin_is_worse_than_stale]]).

### `read-tier-byte-diff`'s 2 diverged artifacts are NOT a regression — they are T026/T027's evidence

The two are **`v42/Messages.hpp`** and **`v42/Reify.hpp`**. Nothing else moved. That four-way split is
exactly what 082 predicts, and a regression would be very unlikely to reproduce it:

| Measured | 082's own requirement |
|---|---|
| `v42/Fields.hpp` **OK** | T026 / **FR-016a**: must be byte-identical |
| `v42/Validator.hpp` **OK** | T026 / **FR-016a**: must be byte-identical |
| `v42/Messages.hpp` **DIFF** | **T025** re-points `emit_messages.cpp` (5 sites); **T028** exists to regenerate this very golden |
| `v42/Reify.hpp` **DIFF** | **T025** re-points `emit_reify.cpp` (2 sites) |
| all 12 `v44`/`v50sp2`/`vt11` **OK** | **T027** / FR-015 / SC-005: set-equal wherever C2 says EQUAL |

FR-016a's byte-identity prediction is **falsifiable rather than tautological** precisely because T023
declined to touch `FieldRef::type` (D-4) — so the two OK v42 artifacts are a real pass, not a no-op.
`emit_builders.cpp:607` is T025's 8th site and does not appear in the read tier, which is consistent.

**The gate it fails is 077's T022, not one of 082's.** `tests/codegen/read_tier_byte_diff_test.cmake`
hardcodes a **pre-077 baseline** (2026-07-16, HEAD `455737c3`) on the premise, stated in its own banner,
that *"077 touches ONLY `emit_builders.cpp` … so all 16 must be byte-identical."* **That premise is
082's to invalidate for v42, by design.** So the disposition is: update **only** the two v42 hashes,
leave the other 14 literals byte-for-byte untouched (that is what keeps the gate discriminating), and
annotate the two lines rather than rewriting the T001 provenance banner — a silent hash swap would leave
the next reader believing 077's premise still holds.

⚠️ **The cmake hash and the checked-in golden are ONE change unit.** The banner claims the four
`Messages.hpp` hashes are corroborated against
`specs/003-dictionary-codegen/contracts/golden/<ns>_Messages.golden.hpp` (gated by
`DeterminismTest.GeneratedMatchesGolden`). T028 regenerates the v42 one. Update the cmake hash without
the golden and `GeneratedMatchesGolden` stays RED **and the banner's corroboration claim becomes false.**

⚠️ **The hashes in `build/resume082-ctest-dict-codegen.log` are STALE — do not transcribe them.** They
predate the FR-023 deletion and the Orchestra `gname` fix. `fixpp-codegen` links `fixpp_dictionary`, so
codegen output is in scope for change even where none is expected. Re-run the gate post-build and
**compare against the old hashes first**: if v42 Messages/Reify still hash `827a9bd0…` / `4c546c83…`
and the other 14 still match baseline, that is a bonus result — the loader amendment is
**codegen-neutral**. If any of the 14 moved, that IS a regression and it is 082's.

**Supersedes** the "identify the two artifacts before any golden is regenerated / reaching for T028 on a
`v44`+ divergence would enshrine a real defect" caution recorded above. That was written before the
artifacts were known and was guarding the **opposite** case; the divergence is v42-only, so T028 is the
correct instrument, not the dangerous one.

**T028's bar is *by construction*, not "golden regenerated".** The instruments already exist: **T019**
pins exactly **18** `class G_` and **46** message classes in the regenerated `v42/Messages.hpp`, and
**T015** pins the 18 tags `{33, 73, 78, 124, 136, 146, 199, 215, 267, 268, 295, 296, 382, 384, 386, 398,
420, 428}`. Diff old-golden vs new-golden and check the added `G_` classes are **exactly** that set. A
`diff | wc -l` sanity number does not discharge it.

### #210's fix is VERIFIED PRESENT in this tree — so concessions 1 and 2 are revertible

Checked because "the issue is closed" is not evidence about *this* working tree. The mechanism the
concessions cite — `set_group_first_ctx`'s unconditional
`add_group_member_ctx(msg_type, parent_path, no_tag, first)` at `include/fixpp/dict/table_view.hpp:645`
— **is still there, unchanged, and is meant to be.** #210 was fixed on the **caller** side: 083 T031/T032
changed the delimiter *source* to this context's **own** declaration (Entity 2 / `delim_cap`), so the
injected tag is already a declared member and the injection *"becomes a no-op (D-5 / C-3.3) and the
pollution disappears by construction rather than by a second fix"* (`dictionary.cpp:592-610`).
**Reading `table_view.hpp` alone would have concluded #210 was unfixed.**

- **Concession 1** (`required_scope_census_test.cpp:626-670`, T017) — collapses to plain set equality.
  This is a **strengthening** (it removes an allowance), so a green after the edit *is* proof. Today it
  asserts `missing.empty()` plus `extra ⊆ {tv.group_first_field(no_tag)}`; post-#210 `extra` should be
  empty outright.
- **Concession 2** (`collision_membership_guards_test.cpp:88-100`, `first_tag_only_in`'s `exclude`) —
  ⚠️ **not symmetric**: dropping `exclude` **widens** the discriminator candidate set, so green is not
  proof. But the comment at `:86-88` states a sharp, falsifiable prediction — *"when #210 lands the
  exclusion becomes a no-op"* — so the real acceptance is that removing it be **behaviour-neutral**:
  the `expected_per_dict` case counts must not move. Counts can only move via the
  `continue` at `:158-166` (variants differing **only** by the injected delimiter). If a count moves,
  the exclusion was **not** a no-op — investigate rather than re-baseline the pin.

## MEASURED 2026-08-12 (post-rebuild) — the gname fix is verified, and a FOURTH carve-out surfaced

Rebuild completed clean (exit 0, 453 CXX objects). Verified the loaders actually recompiled before
trusting anything: `orchestra_loader.cpp.o` went `4412224 → 4414200` bytes at 09:29:59, and the byte-diff
gate consumes `$<TARGET_FILE:fixpp-codegen>` = `bin/fixpp-codegen` (09:30:17), **not** the stale
`_codegen_bootstrap` copy from the previous day.

### ✅ The committed-but-uncompiled Orchestra `gname` fix WORKS

- `LoaderDisposition.*` — **11/11 PASS**, including both required passes.
- `RequiredScopeCensus` T009–T011 — **T010 now PASSES**
  (`MemberLessOrchestraGroupAtNonFirstSeenOccurrenceThrowsOrchestraParseError`). This was the pin the
  fix was written for and it had never been compiled.

### ✅ The loader amendment is CODEGEN-NEUTRAL — proven by hash identity, not inferred

Re-ran `read-tier-byte-diff` on the fresh tool. **All 14 unchanged artifacts are still OK**, and the two
v42 hashes came back **bit-identical to the pre-amendment run** — `827a9bd0…` and `4c546c83…`. So the
FR-023 deletion and the `gname` fix changed **no** generated byte anywhere in the read tier. The hashes
are now measured post-amendment and are safe to transcribe into the gate.

### ⚠️ A FOURTH carve-out — to #208, not #210 — and the parked notes never listed it

`RequiredScopeCensus.SixUnchangedDictionariesBareStoreExactSet` (**T018**) is **RED**:

```
FIX50SP2.xml bare-store exact-set: extra-in-actual{1499,1669,1919,}
```

Cause is at `required_scope_census_test.cpp:812-817`, which erases those three from the expected set:

```cpp
// #208: never registers -- one-level <component> scan defect, not this feature's predicate.
expected.erase(1499); expected.erase(1669); expected.erase(1919);
```

**#208 is CLOSED — by 083** (`CLAUDE.md`: *"per-context group-delimiter resolution via 083 (closed #210 +
#208 + #212)"*), and #208's *"one-level `<component>` scan defect"* is **precisely** what kept these three
from resolving a delimiter. So the carve-out is stale, and the direction is the reassuring one: the
**oracle always said 505**; the implementation used to register only 502 of them and has now caught up.

**The test's own banner already prescribed this exact fix** — *"Pinned at 502 = oracle.group_tags minus
those 3 tags; this row flips to a plain oracle.group_tags comparison (505) once #208 lands."* #208 has
landed, so the flip is the prescribed action, not an invention.

**This is the same class as the three #210 concessions but a different issue, and it was NOT on the list
of three.** The list was built by grepping for `#210`; a carve-out to a *different* closed issue is
invisible to that. Cf. [[feedback_enumerate_the_layers_selector_source_quantifier_instead_of_patching_the_one_that_bit]]
— enumerate by *what the carve-out concedes to*, not by the one issue number you happened to search.

Fix (a **strengthening** — it restores 3 tags to the expected set, so a green after it is proof): delete
the erase block and re-pin FIX50SP2's `expected_count` **502 → 505**. ⚠️ **Re-derived, not silently
updated** — the test's own message says *"re-derive, don't silently update this pin"*. The re-derivation:
`build_quickfix_oracle` yields **505** for FIX50SP2 and always did; **502** was the *implementation's*
pre-#208 number, frozen into the pin by the carve-out. Applied 2026-08-12.

⚠️ **T018's task text in `tasks.md` says "FIX50SP2 505" and was CORRECT ALL ALONG** — it names the
post-#208 target. The shipped pin was 502. Anyone reconciling task text against code here will see a
mismatch that is the *carve-out's* doing, not a stale task.

⚠️ **082's own attributable delta for FIX50SP2 is still ZERO.** The +3 is **083's**, inherited by catching
up to `main`. T023's bare-loop edit cannot account for it: FIX50SP2's type set and struct set are both
**507**, so removing the datatype gate changes nothing there, and the delimiter resolution that moved is
loader-side. FIX50SP2 remains a C2-EQUAL row; only its *baseline* moved.

⚠️ This was **pre-existing at the parking point**, invisible because `ctest -R 'dict|codegen'` never
matched `required_scope_census`. The predicted coverage hole biting exactly as predicted.

✅ **The two oracles AGREE — an earlier note in this file claiming they diverged (505 vs 508) was my own
mis-derivation and is retracted.** `predicate_census.py` gives FIX50SP2 `struct=507`, `reachable=505`
(excluding only `384`/`627` as not-message-reachable, **including** 1499/1669/1919 — measured directly),
and `build_quickfix_oracle.group_tags` is likewise **505**. The `508` figure came from assuming the
shipped pin was 505 and back-solving `505 + 3`; the pin was **502**. Two independent oracles agreeing on
505 is a real corroboration of the new pin, not a coincidence to be distrusted.

## ✅ US4 / Phase 6 COMPLETE 2026-08-12 — ALL FOUR USER STORIES DELIVERED, L-061-1 closed

### T044 — the golden is a genuinely INDEPENDENT oracle

`tests/session/golden/v42_mass_quote.fix`, the **first FIX 4.2 golden** in that directory. Every prior
entry is FIX 4.4 because before 082 no `v42` grouped write was expressible at all (L-061-1).

Authored by a **real QuickFIX-cpp v1.16.0** (`reference-engines/quickfix-cpp`, `libquickfix.so.17`):
`FIX42::MassQuote` + nested `NoQuoteSets::NoQuoteEntries` → `Message::toString()`, tags `8`/`9`/`10`
stripped to the body-only form. Output verified byte-identical to the checked-in file.

```
> 35=i 117=QID-100 296=1 302=QS1 311=AAPL 304=1 295=1 299=QE1 132=10.5 133=10.75
```

Generator checked in at `tools/quickfix_v42_exemplar_golden/gen_v42_mass_quote.cpp`, **deliberately not
wired into CMake** — same OFF-by-default rationale as `tools/quickfix_{enum,required}_golden`, since
`reference-engines/` is outside the submodule and never present in CI, so a wired target could only fail
there. Checked in anyway so the golden is reproducible rather than folklore.

⚠️ **`311` is the entire delta vs the FIX 4.4 `mass_quote.fix`, and it is the DICTIONARIES' divergence,
not an authoring choice.** FIX 4.2 marks `UnderlyingSymbol(311)` **required** inside `NoQuoteSets`; FIX 4.4
does not. **Two independent sources agree**: QuickFIX's own FIX42 `message_order(302,311,312,…)` places it
second, and fixpp's separately derived `G_296_2Args` required set is `{302, 311, 304}`. That agreement is
the whole reason to use a reference engine. PROVENANCE.md warns against "aligning" the two goldens by
deleting `311` — a FIX 4.2 MassQuote without it is invalid.

### T045 — the exemplar: two legs, and neither substitutes for the other

`tests/session/test_082_v42_nested_exemplar_roundtrip.cpp`, ctest
**`test_082_v42_nested_exemplar_roundtrip`** (labels `082;us4;roundtrip;golden;dict;i`), linking
`fixpp::builders::v42` + `fixpp::validators::v42`. **PASSES.**

- **Leg 1 (write)** — `build_MassQuote` output byte-diffs clean against T044's QuickFIX golden.
- **Leg 2 (read)** — the same bytes parsed back through the regenerated v42 read tier, with **both**
  group levels enumerated (`G_296` → `G_295`) and every seeded field compared by value.

⚠️ **Why both legs.** Leg 1 alone passes if the reader is broken. Leg 2 alone passes if writer and reader
share a *compensating* bug — and they share the same dictionary and the same group tables, so a symmetric
error is the plausible failure, not a far-fetched one. The QuickFIX golden is what breaks the symmetry.

⚠️ **Two anti-vacuity guards, both necessary.** `ASSERT_NE(group_first_field(296/295), 0)` before the walk
— an unregistered group yields an **empty** `group_view`, so every in-loop `EXPECT` would simply not run
and read as "all fields matched". And `sets_seen == 1` / `entries_seen == 1` after it, for the same reason
one level down. The nested count is the one that actually pins L-061-1's capability.

**Mutation matrix — all three kill it:**

| Mutant | Result |
|---|---|
| corrupt the **golden's** `311=AAPL` → `MSFT` (no rebuild — golden read at runtime) | RED |
| change the seed's `OfferPx` `10.75` → `11.75` | RED |
| drop `311` from the seed entirely | RED at `validate` with `error=38` (`wire_required_field_missing`) |

## ✅ US3 / Phase 5 COMPLETE 2026-08-12 (T041 + T043) — and T043's task text names a wrong example

`dictionary_required_scope_census_test` **15/15**. Both new pins mutation-proven (below).

### T041 — FIX43 tag 82 stays a plain REQUIRED field

Three legs, and the third is the one a weaker pin would drop: `group_first_field(82) == 0` (not a
group) · `field_valid_for("N", 82)` (still a **known** field of ListStatus) · `82 ∈ required_fields("N")`
(still **enforced**, because present-but-optional is a silent weakening a validity check alone misses).

82 is the tag that discriminates **union** from **replacement**: measured from raw XML it is typed
**NUMINGROUP** yet declared as a `<group>` **nowhere**. The old datatype gate would register it; a
datatype-OR-structural **union** would *also* register it; only a pure structural predicate rejects it.
With T040's 576 (INT-typed but a real `<group>`, so it must register) the two pin the predicate from both
sides **inside one dictionary**.

### ⚠️ T043's named example is WRONG, and unsatisfiable as written

T043 says *"tags 82 and 576 are a group in one dictionary and a plain field in another across
FIX43/FIX44"*. Measured from raw XML, **neither is**:

| tag | FIX43 type | FIX43 `<group>`? | FIX44 type | FIX44 `<group>`? |
|---|---|---|---|---|
| 82 `NoRpts` | **NUMINGROUP** | no | INT | no |
| 576 `NoClearingInstructions` | **INT** | **yes** | **NUMINGROUP** | **yes** |

82 is a group in **neither**; 576 is a group in **both**. Stronger still: across FIX43/FIX44 **no tag at
all** satisfies the claim — all **25** FIX44-only groups are not even declared in FIX43's `<fields>`, and
there are **zero** FIX43-only groups. The claim cannot be witnessed on that pair by any tag.

Two accurate witnesses used instead:
- **(a) 576's datatype is dictionary-dependent while its structure is not** — INT in FIX43, NUMINGROUP in
  FIX44, a reachable group in both. It is the **only** tag in the pair whose datatype differs while being
  a group in both, so it is a unique witness: a datatype gate registers it in FIX44 only; the structural
  predicate registers it in both.
- **(b) The literal group-here/plain-field-there claim IS witnessable, on a different pair.** Exactly
  **two** tags in the whole shipped set qualify: **33 `LinesOfText`** (group in FIX41, plain field in
  **FIX40**) and **85 `NoDlvyInst`** (group in FIX44, plain field in **FIX40**).

### ⚠️ The negative legs needed a NON-VACUITY guard, or the claim is unfalsifiable

`group_first_field(t) == 0` is exactly what an **empty or failed-to-populate** table returns for *every*
tag. So the FIX40 half of (b) would pass for the wrong reason and the per-dictionary claim would be
unfalsifiable. Both FIX40 zero-assertions now sit behind
`ASSERT_EQ(bare_registered_group_tags(tv), {73, 78, 124, 136})` — FIX40's own set, derived by
`predicate_census.py` — so the two zeroes mean *"structurally absent"* rather than *"nothing is here"*.
Cf. [[feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree]].

**Mutation matrix — all three kill their target:**

| Mutant | Result |
|---|---|
| T041's required-field leg searches tag `9999` instead of `82` | RED |
| T043's FIX40 `group_first_field(33) == 0` flipped to `!= 0` | RED |
| **the non-vacuity guard itself** — drop `136` from FIX40's expected set | RED |

## US2 STARTED 2026-08-12 — the `fixpp::v42` builder tier is emitted (#196's actual deliverable)

### ✅ T031 — plan set derived BEFORE the first run, and the run matched it

`python3 contracts/builder_plan_census.py` (no args ⇒ self-validates against the v44/v50sp2 goldens
first, then reports v42):

| mode | messages in scope | distinct plans | tags with a plan | emitted files |
|---|---|---|---|---|
| `--families all` | **39** (= registry) | **28** | **17** | **226** = 39×5 + 28 + 3 |
| `--families official` | **25** (= registry) | **19** | **11** | **147** = 25×5 + 19 + 3 |

**17 builder tags, not 18** — `384 NoMsgTypes` is a read-tier tag but not a builder-tier one: its only
FIX42 host is the admin message `Logon`, excluded by `emit_builders`' `is_application` gate. T031
predicted exactly this and the census confirms it.

**Post-T035 the emitted tree matches the derivation exactly**: 226 builder-tier files (195 under
`messages/` = 39×5, **28** `groups/<Plan>.hpp`, 3 others), 28 plan headers. Derived first, then measured —
not transcribed.

### ✅ T032 / T033 — the two descope pins INVERTED (FR-016b), each proven RED before T035

- **T032** `test_077_builder_no_emit.cpp`: `V42EmitsNoBuilders` → **`V42EmitsBuilders`**. RED pre-T035.
  ⚠️ `Vt11EmitsNoBuilders` is deliberately **unchanged and still green** — vt11 must keep emitting
  nothing, but for a *different and genuine* reason (zero application messages ⇒ `is_application`
  self-skip, **not** a version check; FR-010/T038). Inverting both together would have destroyed that
  distinction.
- **T033** `test_077_v42_vt11_completeness_and_c4.cpp`: `V42HasAppMessagesButIsNonBuilderBearingByPolicy`
  → **`V42RegistryExactSetEqualsRawXmlWalk`**, mirroring the v44 sibling. The expected set is the
  **raw-XML-derived** one (the same `legacy_expected_msgtypes` call the old test already made **and then
  discarded**), independently cross-checked by `builder_plan_census.py`'s 39 — *not* a transcribed list,
  per FR-016b's own warning that a set copied from the first emitter run is a corpus built from the read
  it checks. Added an `ASSERT_TRUE(exists(all.hpp))` guard so the exact-set comparison cannot "fail for
  the wrong reason" against an empty parse. RED pre-T035, and it failed on exactly that guard.

### ✅ T035 — the driver exclusion deleted, with NO replacement predicate

`main.cpp`'s `if (ir.ns != "v42")` is gone (D-8 / FR-007 / FR-010). Its premise was the **datatype**
gate — *"v42 types NumInGroup as legacy INT … so emit_builders materializes ZERO typed groups"* — which
is the very thing 082 replaced. No version test remains on that path at all; the only legitimate skip is
a genuinely empty application registry, which is vt11's, structurally.

⚠️ **T035's text is INCOMPLETE as written, and the gap is a silent no-delivery.** Deleting the exclusion
makes the codegen *emit* 226 v42 files, but `cmake/Codegen.cmake`'s
`foreach(_ver IN ITEMS v44 v50sp2 vlatest)` meant **nothing compiled them** — files generated and
consumed by no target, which fails no build and passes no test. Three CMake changes were required and
are part of T035:

1. `foreach(_ver IN ITEMS **v42** v44 v50sp2 vlatest)` — creates `fixpp_builders_v42` /
   `fixpp_validators_v42`.
2. A new `_v42_builders_marker` (`v42/all.hpp`), added to the missing-output regen-guard loop, so an
   absent v42 builder tier triggers regeneration instead of being the expected state.
3. The post-generation existence assertion, mirroring v44/v50sp2.

⚠️ **vt11 is deliberately absent from all three**, and this is structural rather than policy: it emits
no builder files, so `file(GLOB)` finds nothing and `add_library` would fail on an empty source list —
and a vt11 builders *marker* would demand a file that must never exist, wedging the regen guard forever.
Commented in place so nobody "restores symmetry". Two now-false "for every ns != v42" comments were
corrected at the same time.

### ✅ T036 / T037 / T038 / T039 DISCHARGED 2026-08-12 — v42's builder tier is emitted, compiled and gated

Build clean (exit 0, 3228 edges): **`libfixpp_builders_v42.a` 12.5 MB, `libfixpp_validators_v42.a` 6.5 MB**.
Both inverted pins flipped **RED → GREEN** (T032 3/3, T033 3/3), `codegen_determinism_test` **22/22**.

- **T036** — 226 v42 builder-tier files checked in under
  `specs/078-precompiled-builder-libs/contracts/golden/v42/` (28 `groups/<Plan>.hpp`), the copy
  re-hashed byte-exact against the emitted tree. ⚠️ **And GATED**: a new
  `DeterminismTest.V42AllModeBuildersMatchesGolden` cell plus v42 added to the setup's golden-set
  existence loop. Without that cell the 226 files would be a **dead golden** — in the repo, compared by
  nothing, reading as coverage while gating nothing.
- **T037** — `DeterminismTest.V42OfficialModeBuildersStructuralShape`: **147 files** (25×5 + 19 + 3) and
  **registry 25**, both derived from `builder_plan_census.py --families official`. **Deliberately not a
  golden** — 078 retired the `--families official` pinned-golden convention, so a `v42-official/`
  directory would reintroduce a dropped convention. ⚠️ Pins **counts only**: plan names are
  mode-dependent (FR-016b), so this must never become a name-set comparison against the all-mode golden.
- **T038** — vt11 still self-skips, and the assertion is **stronger than it could previously be**:
  `grep 'ir.ns [!=]='` over `main.cpp` now matches **only a comment**, so there is no version predicate
  anywhere in the emit path — yet vt11's emitted dir still contains exactly its 5 read-tier artifacts and
  no `all.hpp`/`groups/`/`messages/`. Before T035 that emptiness was confounded with the v42 ns
  predicate; now it can only be the `is_application` empty-registry skip. Both vt11 pins green; its read
  golden unmoved (all 4 vt11 artifacts OK in `read-tier-byte-diff`).
- **T039** — measured, both halves:
  - *Regression half:* the three existing builder golden sets are **BYTE-IDENTICAL** after v42 was
    added — v44 **506** files, v50sp2 **1341**, vlatest **1445**, same names and same sha256 throughout.
  - *Structural-key half:* **17** tags carry **28** plans, and the ordinal map
    `{73:3, 78:2, 146:4, 268:2, 295:3, 296:2, 420:2}` sums to exactly `28 − 17 = 11` extra plans. That
    arithmetic **is** B-077-1's guarantee: a second structural variant of a tag becomes a new **ordinaled**
    plan rather than silently sharing the first. The emitted names match the census's, ordinals included.
  - ⚠️ **T039's own wording says "v42's 18 newly-visible groups entered the tier" — it is 17, not 18.**
    `384 NoMsgTypes` is read-tier only; its sole FIX42 host is the admin message `Logon`, excluded by
    `emit_builders`' `is_application` gate. T031 predicted this; the emitted plan set confirms it.

⚠️ **A THIRD v42 builder-descope assertion existed, in a file FR-016b does not name.**
`determinism_test.cpp:879-881` asserted v42 emits no `all.hpp` on the `FIX_LATEST=OFF` path. FR-016b lists
exactly two descope pins and this is in neither; it surfaced **only by going RED after T035**. Inverted to
sit with its v44/v50sp2 siblings — `FIXPP_CODEGEN_FIX_LATEST` gates **vlatest only** (FR-012/T014/G4a).
Same shape as the #208 carve-out earlier in this feature: see
[[feedback_a_carveout_list_built_by_grepping_one_issue_number_misses_the_others]].

### ✅ T034 DELIVERED 2026-08-12 — US2 COMPLETE, all 14 required-group omissions rejected

`tests/codegen/test_082_v42_required_group_omission_test.cpp`, ctest
**`codegen_082_v42_required_group_omission_test`** (labels `codegen;082;us2;required_group_omission`),
linking `fixpp::builders::v42` + `fixpp::validators::v42`. **15/15** — the 14 pairs plus a completeness
guard. This is the hole issue #196 exists to close: a FIX 4.2 `required='Y'` group is now representable in
`Args`, so omitting it is **detectable** rather than silent (Article VI).

**Every case asserts a PAIR, and the positive control is load-bearing.** `validate_required` returns
`wire_required_field_missing` for **both** a missing required scalar (`builder_validate.hpp:77`) **and** an
empty required group (`:86`) — the same enum from two causes. So "it rejected" proves nothing alone: a case
that forgot a required scalar would reject for the wrong reason and read as a pass. Each case asserts the
full `Args` validates **clean** first, then that clearing only that span rejects.

**Plan names are per-message, not per-tag** — seven of the tags are ordinaled, and the divergence is real
and exercised: `73` → NewOrderList `G_73_1Args` vs ListStatus `G_73_3Args`; `146` → MarketDataRequest
`G_146_3Args` vs QuoteRequest `G_146_2Args`; `268` → MDSnapshotFullRefresh `G_268_1Args` vs
MDIncrementalRefresh `G_268_2Args`; `295` → QuoteCancel `G_295_1Args` vs MassQuote's nested `G_295_3Args`.

**The 14th (nested) case is built as T034 prescribes**: a `G_296_2Args` entry complete in its own required
scalars (302/311/304) but carrying an **empty** `quote_entries` span, so the rejection arrives through the
296 row's `gc.validate_entry` rather than a top-level `group_checks` row.

#### US2's gates, each proven RED by mutation (2026-08-12)

Every US2 gate was mutation-tested against **the compiled binary**, with the artifact restored and
re-hashed afterwards. Nothing here rests on "it passed the first time".

| Gate | Mutant | Result |
|---|---|---|
| T034 case | `{true→false}` on NewOrderList's group check in the **compiled** `.validator.cpp` | RED, its own message |
| T034 completeness | delete the `News_LinesOfText_33` TEST_F | RED — registered **14** vs expected **15** |
| T034 completeness | duplicate a `kCovered` row | RED — distinct-row check |
| **T036 golden** | flip **one byte** in the checked-in `golden/v42/groups/G_73_1Args.hpp` | RED, naming `groups/G_73_1Args.hpp not byte-identical` |
| **T037 count** | `kExpectedV42OfficialMsgCount` 25 → 26 | RED on both legs (152 vs 147 files; registry ≠ 26) |

⚠️ **T036's golden needed this most, and had it least.** It was copied from the same emitter run it now
gates, so its first green was *"a corpus built from the read it checks"* —
[[feedback_verification_corpus_built_from_the_read_it_checks_is_blind]] — and proved nothing until a
perturbation was shown to fail it. Verified incidentally: the cell reads the golden **from disk at
runtime**, so no rebuild is needed to mutate it (checked, not assumed — unlike the `.inl`/`.cpp` twin below).

⚠️ **T034's completeness cell was a COMPILE-TIME TAUTOLOGY as first written.** It declared
`std::array<..., kCaseCount> kCovered` and then asserted `kCovered.size() == kCaseCount` — extent and
expectation were the same token, so it could never go red while reading as completeness coverage.
Exactly the class this feature spent the day retiring. Fixed by deducing the extent (`std::to_array`),
adding a distinct-row check, and — the part that actually binds table to tests — asserting gtest's own
`current_test_suite()->total_test_count()`, so **deleting a whole case fails HERE** instead of silently
shrinking coverage.

#### ⚠️ The mutation that "passed" — and why it was the mutant that was broken, not the test

Flipping `{true → false}` on NewOrderList's group check in
**`messages/NewOrderList.validator.inl`** left the test **GREEN**. The test was fine; the mutation was
unreachable. The test links the prebuilt `fixpp_validators_v42`, whose traits come from
**`NewOrderList.validator.cpp`** — the `.inl` is compiled only under `FIXPP_VALIDATORS_HEADER_ONLY`.
Re-applied to the `.cpp`, the mutant **kills the test** with its intended message
(*"omitting a required='Y' repeating group MUST be rejected"*), and the tree was restored and rebuilt.

**The lesson is sharper than "guard your mutants".** The `assert applied == 1` guard **did fire correctly** —
the string genuinely existed in the `.inl` and was genuinely replaced. A no-op guard proves the edit landed
**in a file**; it does **not** prove that file is on the **test's build path**. Where a generated tier ships
the same traits twice — once inline, once compiled — the guard must also pin *which* copy the binary under
test actually consumes. See
[[feedback_a_mutation_guard_proves_the_edit_landed_not_that_the_file_is_on_the_build_path]].

### ▶ T034 PREP — the 14 pairs DERIVED (2026-08-12), and T034's premise confirmed

Derived from raw FIX42.xml (`required='Y'` on a `<group>` declaration, components expanded), so T034's
hand-written typed cases can be pinned against a **derived set** rather than a transcribed one:

**14 pairs across 12 messages — 13 top-level, 1 nested**, exactly as T034 states:

| message | msgtype | tag | location |
|---|---|---|---|
| BidResponse | `l` | 420 `NoBidComponents` | top-level |
| Email | `C` | 33 `LinesOfText` | top-level |
| ListStatus | `N` | 73 `NoOrders` | top-level |
| ListStrikePrice | `m` | 428 `NoStrikes` | top-level |
| MarketDataIncrementalRefresh | `X` | 268 `NoMDEntries` | top-level |
| MarketDataRequest | `V` | 146 `NoRelatedSym` | top-level |
| MarketDataRequest | `V` | 267 `NoMDEntryTypes` | top-level |
| MarketDataSnapshotFullRefresh | `W` | 268 `NoMDEntries` | top-level |
| MassQuote | `i` | 296 `NoQuoteSets` | top-level |
| **MassQuote** | `i` | **295 `NoQuoteEntries`** | **nested in 296** ← the 14th |
| NewOrderList | `E` | 73 `NoOrders` | top-level |
| News | `B` | 33 `LinesOfText` | top-level |
| QuoteCancel | `Z` | 295 `NoQuoteEntries` | top-level |
| QuoteRequest | `R` | 146 `NoRelatedSym` | top-level |

14 pairs / 12 messages reconciles because **MarketDataRequest and MassQuote each contribute two**.
The single nested pair is `MassQuote 295-in-296` — so T034's "13 top-level + a 14th built as a 296 entry
carrying an empty 295 span, checked via `gc.validate_entry`" is confirmed, not assumed.

**Emitted v42 plan names (28, matching T031 including ordinals):** `G_124Args`, `G_136Args`,
`G_146_{1..4}Args`, `G_199Args`, `G_215Args`, `G_267Args`, `G_268_{1,2}Args`, `G_295_{1,2,3}Args`,
`G_296_{1,2}Args`, `G_33Args`, `G_382Args`, `G_386Args`, `G_398Args`, `G_420_{1,2}Args`, `G_428Args`,
`G_73_{1,2,3}Args`, `G_78_{1,2}Args`. Ordinaled map = `{73:3, 78:2, 146:4, 268:2, 295:3, 296:2, 420:2}`,
**identical** to `builder_plan_census.py`'s.

⚠️ **Seven of the 14 pairs' tags are ORDINALED**, so T034 cannot name a plan from the tag alone — it must
take the plan the *specific message* references (from the emitted `messages/<Msg>.hpp`). And per FR-016b
plan names are **mode-dependent**, so an `--families official` name must never be cross-compared with an
`--families all` one. Model to follow: `tests/session/test_067_builder_validate.cpp` (the v44 equivalent,
e.g. `G_73_2Args` for `NewOrderList`).

### ✅ T029 DELIVERED 2026-08-12 — FR-021's class-side ⟷ raw-XML gate, version-parameterised

`tests/codegen/test_082_class_xml_consistency_test.cpp`, ctest **`codegen_082_class_xml_consistency_test`**
(#26, labels `codegen;082;us1;class_xml_consistency`). **4 tests, all green, over all four
`<fix>`-schema versions** — v42 is FR-021's requirement, the other three were the "ideal" and cost one
`kCases` row each.

Two derivations sharing no code and no predicate: class side = **text** of the generated `Messages.hpp`;
structural side = `build_quickfix_oracle()`'s from-scratch pugixml walk.

| Leg | Assertion | v42 | v44 | v50sp2 | vt11 |
|---|---|---|---|---|---|
| pins | message classes / flyweights | 46 / **18** | 93 / **59** | 156 / **505** | 8 / **1** |
| **A** | flyweight tag set == `oracle.group_tags`, both directions | ✅ | ✅ | ✅ | ✅ |
| **B** | per-message top-level group refs == that msg_type's top-level groups | ✅ | ✅ | ✅ | ✅ |
| **C** | flyweight direct members (scalars + nested refs) == **union** over contexts | ✅ | ✅ | ✅ | ✅ |

**The count column is a fourth independent derivation of T018's registered-after numbers** — 59 / 505 / 1
read off the *generated class tier*. Note **v50sp2 = 505**, agreeing with the oracle and the #208-flipped
pin. Three independent routes now say 505.

**The emitter's flyweight-member rule was MEASURED, not read** (non-circularity forbids reading the
emitter). `G_<N>` is version-wide-shared but the oracle's member sets are per-context, and FIX42 tag 146
has 6 contexts in 4 variants. `G_146` carries **53** members; the **union** of the 6 per-context direct
sets is exactly **53**, equal both directions — first-seen (19) and largest (31) are both refuted. So
leg C is an **exact set equality**, with no subset weakening anywhere in the gate.

⚠️ **Two spellings of the same marker, and it is a live false-green trap.** Inside a flyweight the
reference is unqualified (`group_view<G_295>`); at message level it is **fully qualified**
(`group_view<::fixpp::v42::groups::G_296>`) — there is **no bare `group_view<G_296>` anywhere** in
`v42/Messages.hpp`. A scanner written for the unqualified form alone sees **zero** message-level group
references and every leg still passes. This bit the mutation harness first (M3 matched nothing), which is
the only reason it was noticed. Cf.
[[feedback_enumerate_the_layers_selector_source_quantifier_instead_of_patching_the_one_that_bit]].

**Mutation matrix — run against the compiled binary, not the prototype**, each mutant guarded by an
`applied == 1` assertion, and the generated header restored and re-hashed to `827a9bd0…` after each:

| Mutant | Effect | Kills |
|---|---|---|
| **M1** delete `class G_384` | flyweight count 18→17 | A's **count** pin, C, nesting |
| **M2** drop `G_296`'s scalar member 367 | shape only | **C only**, on `{296}` only |
| **M3** drop one message-level `group_view<::fixpp::v42::groups::G_296>` | shape only | **B only** |
| **M4** re-tag `class G_384` → `class G_9999` | count **stays 18** | A's **set-equality** |

⚠️ **M4 is not redundant with M1, and M1 alone would have left leg A unproven.** Under M1 the `ASSERT_EQ`
population pin fires first and **aborts the test**, so A's actual set-equality `EXPECT_EQ` never
executes — the count was doing all the work, and a flyweight emitted under the *wrong tag* keeps the
count at 18 and would have slipped through. M4 holds the count and moves only the set, reporting
`structural-only: 384  class-only: 9999`. **A mutant that kills a test does not prove the assertion you
care about killed it** — cf. [[feedback_witness_asserts_named_postcondition_not_proxy]].

⚠️ **This gate is NOT the same non-circularity class as its vlatest sibling.** `vlatest_manifest_class_consistency_test.cpp`
links **no** pugixml and no fixpp header — both its sides are generated text. This one links pugixml via
the oracle. The claim here is narrower and stated in the banner: the structural side never consults
`FieldRef::type`, `VersionIR::group_tags` or `group_first_field` — the three predicates 082 re-points —
so it can witness the re-point rather than move with it. Do not cite the sibling's rationale for this file.

Wiring note: `FIXPP_DICT_DATA_DIR` is a **`tests/dictionary`-scope** CMake variable and is **empty** in
`tests/codegen`; spelling it from `${CMAKE_SOURCE_DIR}/dictionaries` (as this directory's 067 sibling
does) is required. The empty value produced a bare `"FIX42.xml"` and a **loud** oracle load failure
rather than a silent empty oracle — the fail-closed behaviour worth keeping.

### ✅ T026 / T027 / T028 DISCHARGED 2026-08-12 — the v42 delta reconciled BY CONSTRUCTION

T028's bar is *"reconcile the emitted delta by construction — not 'golden regenerated'"*. Done, four ways,
before the golden was touched (old golden vs the gate's own regenerated `v42/Messages.hpp`):

| Check | Old golden | Regenerated | Verdict |
|---|---|---|---|
| `G_<tag>` group structs | **0** | **18** | T019's pin, exactly |
| the 18 tags themselves | — | `{33,73,78,124,136,146,199,215,267,268,295,296,382,384,386,398,420,428}` | **set-identical** to T015/FR-005/K1 |
| message-class **name set** | 46 | 46 | **sets equal** — none added, lost or renamed |
| scalar `decode_field<int32_t>(get<T>())` for the 18 | 17 | **0** | all traded for group accessors |

**The 17-vs-18 asymmetry is explained, not waved past:** tag **136** `NoMiscFees` has **no** `get<136>` in
*either* file, because it is a **nested** group (inside `NoAllocs(78)`), never reachable as a top-level
message accessor. It had no scalar accessor to lose and gains a `G_136` struct referenced from its
parent's class. So 17 traded + 1 nested-only = the 18.

⚠️ The delta is **not** purely additive — 5487 lines added, 4139 removed. The removals are exactly the
displaced scalar accessors plus block-position churn. A "purely additive" claim would have been false;
T026 asks for *byte-identical or changed-with-explanation*, and this is the explanation.

⚠️ Measured with **Python, not `diff`** — RTK filters `diff` output, and `diff | grep -c '^<'` returned
**0** on the very input whose `<` lines were visible one command later. Do not trust a piped `diff` count
here. Cf. [[feedback_rtk_git_status_truncates_modified_list_use_proxy_before_destructive_git]].

**Applied:** `v42_Messages.golden.hpp` regenerated (copy verified byte-exact afterwards — sha256
`827a9bd0…`, guarding [[feedback_crash_torn_write_can_corrupt_a_checked_in_golden_one_byte]]), and the
**two** v42 hashes in `read_tier_byte_diff_test.cmake` re-baselined with an in-place annotation explaining
why 077's premise no longer holds for v42. **The other 14 literals untouched.**

**Result: `read-tier-byte-diff` PASSES 16/16, `codegen_determinism_test` PASSES** (both
`GeneratedMatchesGolden` and `AdditiveOffOnByteDiff`, 150 s). Both were RED at the sweep baseline.

### C1.1's RESIDUAL EXCEPTION — CONFIRMED RETIRED, by an independent route

The prediction below was recorded before measuring. **It is confirmed, and not by the check it anticipated:**
T018's failure message *is* the measurement. `extra-in-actual{1499,1669,1919}` means the bare store now
registers all three, i.e. `group_first_field(1499/1669/1919) != 0` — the exact question item 8 asks.
So C1.1's **RESIDUAL EXCEPTION is RETIRED** and `contracts/group-detection.md` should say so.

### C1.1's RESIDUAL EXCEPTION — PREDICTION recorded 2026-08-12, before measuring

Open item 8 asks whether `group_first_field(1499 / 1669 / 1919)` on FIX50SP2 still returns **0**. Recorded
here **before** the runtime check so the measurement can refute it rather than be fitted to it.

Raw-XML structure — all three `<group>` elements have **only `<component>` children, no direct `<field>`
child**, which is exactly the shape the exception was written for (a literal first-field scan finds nothing):

| tag | name | children | transitive first member |
|---|---|---|---|
| 1499 | `NoAsgnReqs` | `Parties`, `StrmAsgnReq/RptInstrmtGrp` | **453** (`Parties` → group `NoPartyIDs`) |
| 1669 | `NoRiskLimits` | `RiskLimitTypesGrp`, `RiskInstrumentScopeGrp` | **1529** (→ group `NoRiskLimitTypes`) |
| 1919 | `NoPriceMovements` | `PriceMovementValueGrp`, `ClearingAccountTypeGrp` | **1920** (→ group `NoPriceMovementValues`) |

**PREDICTION: all three now return NON-ZERO (453 / 1529 / 1920), so the RESIDUAL EXCEPTION is RETIRED.**
083's capture resolves *through* nested components, and each of these resolves in one component hop. Note
each first member is itself a **nested group's count tag**, not a scalar — so a check that assumed the
delimiter must be a plain field would mis-read this.

Two independent corroborations already in hand, neither conclusive alone:
- The census above puts FIX50SP2 at `struct=507, registered=505`, and the **entire** gap of 2 is
  attributed to `384`/`627` being not-message-reachable. Were 1499/1669/1919 unresolvable the gap would
  be 5.
- `AllShippedContextsHaveADelimiterRecord` is **GREEN** (measured, 11/11 `LoaderDisposition`). ⚠️ Read
  that with care: it is a **context-store** assertion, whereas the exception is stated about the **bare**
  `group_first_field(no_tag)` store. Suggestive, not a substitute — which is why the direct measurement
  is still owed.

**First-seen-wins is unambiguous for all three, but for a DIFFERENT reason per tag** — counted, not assumed
(`<group>` occurrences in FIX50SP2.xml): `NoAsgnReqs` **2**, `NoRiskLimits` **1**, `NoPriceMovements` **1**.
So 1669/1919 are safe by *uniqueness*, and 1499 only because both of its occurrences open with `Parties`.
Do not carry either reason to another reused tag.

### The change unit for the hash update is TWO artifacts, not three — checked

`AdditiveOffOnByteDiff` (`determinism_test.cpp:721`) was a candidate third member. It is not, and the
reason matters: its leg **(a1)** compares each OFF-run legacy `Messages.hpp` against **the same 003
golden** `GeneratedMatchesGolden` uses — it holds **no independent hash of its own**. So regenerating
`v42_Messages.golden.hpp` satisfies both tests at once. Its leg **(b)** is a *relative* OFF-run-vs-ON-run
byte-identity walk, golden-independent and unaffected.

And `v42/Reify.hpp` has **no golden at all** — per `read_tier_byte_diff_test.cmake`'s own banner, only the
four `Messages.hpp` are goldened; the other 12 artifacts are covered *solely* by that script's hashes.

**Change unit = `read_tier_byte_diff_test.cmake` (the 2 v42 hashes) + `v42_Messages.golden.hpp`.** Nothing else.

### `ctest -R 'dict|codegen'` DOES reach the 067 baseline — checked, because the sibling trap already bit once

The 067 binary registers as **`codegen_067_emit_builders_unit_test`** (`tests/codegen/CMakeLists.txt:117-118`,
LABELS `codegen;067`). The name contains `codegen`, so the planned sweep **will** produce the
`Group077DedupSoundness` RED baseline the held patch is waiting on. Verified explicitly rather than assumed,
because on this same branch `required_scope_census` — which contains neither `dict` nor `codegen` — silently
sat outside both before/after sweeps. **If the sweep somehow does not run it, do not apply the patch on the
strength of the source reading — run the binary directly**, the way T010's failure was found.

### Registration deltas RE-DERIVED 2026-08-12 with the branch's own non-circular oracle

`python3 specs/082-structural-group-detection/contracts/predicate_census.py --dict-dir dictionaries`
(raw-XML only — loads neither `Dictionary`/`table_view` nor the codegen IR, so it can witness the very
predicate this feature changes):

| Dictionary | type set | struct set | registered before → after | delta |
|---|---|---|---|---|
| FIX40 | 0 | 4 | 0 → **4** | +4 |
| FIX41 | 0 | 7 | 0 → **7** | +7 |
| FIX42 | 0 | 18 | 0 → **18** | +18 |
| FIX43 | 34 | 34 | 33 → **34** | **+1** — tag **576** `NoClearingInstructions` |
| FIX44 | 59 | 59 | 59 → 59 | +0 EQUAL |
| FIX50 | 69 | 69 | 67 → 67 | +0 EQUAL |
| FIX50SP1 | 99 | 99 | 97 → 97 | +0 EQUAL |
| FIX50SP2 | 507 | 507 | 505 → 505 | +0 EQUAL |
| FIXT11 | 1 | 1 | 1 → 1 | +0 EQUAL |
| Orchestra FIX Latest | 524 | 524 | 524 → 524 | +0 EQUAL |

**This corroborates three shipped pins independently:** T015's FIX42 set is *exactly* the 18 tags
`{33, 73, 78, 124, 136, 146, 199, 215, 267, 268, 295, 296, 382, 384, 386, 398, 420, 428}`; T016's FIX40 **4**
/ FIX41 **7**; and T018's registered-after column **59 / 67 / 97 / 505 / 1 / 524**, all six matched.
The six `+0 EQUAL` rows are also the C2-EQUAL set T027 checks byte-identity for — consistent with the
byte-diff measurement above (only v42 moved).

⚠️ **Correction to the parked note, which said *"the brief's `+1` for FIX43 is stale — commit `8b9973a6`
already corrected it to `+576`"`*.** Those are not competing numbers and neither is stale. `8b9973a6`'s
message reads *"Effective FIX43 delta is **+576 ONLY**; tag 82 becomes a no-regression pin (FR-012)"* —
**`576` is the TAG, not a count.** The delta is **+1 tag, whose number is 576**. The parked note read a
tag identity as a cardinality and then declared the (correct) count stale. Measured above: `+1/-0`.
Cf. [[feedback_a_count_identity_is_not_proof_a_scanner_does_not_over_or_under_match]] — same family:
a count and an identity are different claims and must not be substituted for one another.

FIX43's asymmetry is also confirmed and is *not* a de-registration: tag **82** `NoRpts` is NUMINGROUP-typed
but is **not** a `<group>`, yet the delta is `-0` — because `group_first_field_impl` already returns 0 for
it, so it was never registered. That is exactly the FR-012 no-regression pin `8b9973a6` describes.
FIX50/SP1/SP2's `struct` (69/99/507) exceeding `registered` (67/97/505) by 2 is likewise benign and
already explained by the oracle's own note: `384` `NoMsgTypes` and `627` `NoHops` are declared `<group>`s
that are **not message-reachable**.

**Open item 7 is discharged.** Nothing left to re-derive here; `builder_plan_census.py` remains for the
US2 (T031) plan-set derivation, which is a different question.

### The 067 `Group077DedupSoundness` breakage — root cause found, and the recorded framing was wrong

The parked note said *"synthetic tags 9002/9003 are `NUMINGROUP`-typed but declare no group structure"*.
**That is not the cause.** `make_synth_group_message` (`test_067_emit_builders_unit.cpp:655-679`) **does**
populate `m.group_order` with a proper `GroupOrderEntry`. The real cause is one level up:
`build_dedup_soundness_ir()` (`:679-701`) hand-builds its `VersionIR` **bypassing `build_ir()`**, and
`ir.group_tags` is a **derived** field that only `build_ir()` populates (`ir.cpp:731-744`). So it stays
**empty**, `is_group_tag()` binary-searches an empty vector, and every synthetic no_tag — 9001 *and*
9002/9003/9004 — reads as a non-group. All **three** `Group077DedupSoundness` tests fail, not just the
two whose line numbers were recorded.

This is the *"fixture builds the object graph by hand and misses a newly-added derived field"* shape, not
a datatype-gate residue. **Censused before scoping** (cf.
[[feedback_census_all_handrolled_scanners_before_scoping_parse_fix]]): of the four test files touching
`VersionIR`, `build_dedup_soundness_ir()` is the **only** hand-built one — `required_scope_census_test.cpp:271`
and `test_067_emit_builders_unit.cpp:65` both call `build_ir()`, and the other two only mention it in
comments. So the fix is **single-site**; no shared helper is warranted.

Prepared patch — append to `build_dedup_soundness_ir()` before `return ir;`, mirroring `ir.cpp:737-744`
(the header/trailer union at `ir.cpp:206-211` has no analogue here — the fixture declares neither):

```cpp
    // 082 T024/T025: `group_tags` is DERIVED — only build_ir() populates it,
    // and this fixture bypasses build_ir() by construction. Mirror
    // ir.cpp:737-744 or every synthetic no_tag reads as a non-group and all
    // three Group077DedupSoundness cases fail.
    for (auto const& m : ir.messages) {
        for (auto const& entry : m.group_order) {
            ir.group_tags.push_back(entry.no_tag);
        }
    }
    std::sort(ir.group_tags.begin(), ir.group_tags.end());
    ir.group_tags.erase(std::unique(ir.group_tags.begin(), ir.group_tags.end()),
                        ir.group_tags.end());
```

⚠️ **Apply it only AFTER the 3 failures are observed on the unfixed tree.** The fix is deliberately held
until the in-flight rebuild produces that RED, per
[[feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree]] — a fix landed before its
baseline leaves no evidence it was load-bearing.

### Concession 3's `13`-vs-`14` is NOT a silent exclusion

Worth writing down because it reads like one. `test_082_group_required_member_validation_test.cpp:526`
asserts `cases_checked == 13` while the concession text says **14** contexts. The 14th — the nested
`MassQuote NoQuoteSets(296) → NoQuoteEntries(295)` descent — is an **explicit separate block** opening at
`:528`, outside the top-level loop the counter bounds. 13 + 1 = 14; nothing is dropped.

---

## Evidence index

| Artefact | Path |
|---|---|
| S0 census transcript | `<scratchpad>/pre-change/S0-predicate-census.txt` |
| S0b builder-plan derivation | `<scratchpad>/pre-change/S0b-builder-plan-census.txt` |

## Phase 7 — T048 (FR-022(c)): compile-time ceiling, measured

`ctest -R '^compile_time_bench$'`, release + `FIXPP_BUILD_BENCH=ON`, **7 runs**. Per T003 this is a
**record-and-compare obligation, not a pass/fail gate** — the harness already returned
`NFR-003-2 result: FAIL` before 082 touched anything, because `v44` exceeded the load-bearing 3 s
single-version ceiling and only `v50sp2` is exempt.

| version | pre-change (T003, steady) | post-082 observed range | verdict |
|---|---|---|---|
| `v42` | ≈ 2.73 s (~10 % headroom) | **1.70 – 2.24 s** | **PASS on 5/5 measured runs** |
| `v44` | ≈ 4.5 s | **2.76 – 3.53 s** | **FAIL 5/7, PASS 2/7 — pre-existing overage, now FLAKY at the boundary** |
| `v50sp2` | ≈ 10.3 s | 6.41 – 7.80 s | KNOWN_OVERAGE (exempt) |
| `vt11` | ≈ 2.05 s | 1.18 – 1.52 s | PASS |
| all-versions | ≈ 14.8 s | 9.40 – 11.38 s | PASS (15 s soft) |

**1. The risk T048 exists to catch did NOT materialize.** 082 adds 18 `class G_` to `v42/Messages.hpp`
and `Reify.hpp`, and T003 flagged that `v42` had only ≈0.27 s of headroom. Measured in-session, `v42`
sits at **1.70–2.24 s against a 3 s ceiling and passed every run**. That absolute figure — not a
cross-session delta — is the claim.

**2. NEW finding: `v44`'s overage is now FLAKY, which is worse than a stable failure.** It straddles
the ceiling (2.76 / 3.08 / 3.16 / 3.21 / 3.53 s plus two sub-3 s runs) and the harness verdict flips
`FAIL`↔`PASS` run to run. Pre-existing per T003 (≈4.5 s), **not caused by 082** — but a gate whose
verdict depends on machine load will read green on a lucky run and hide the overage entirely. ⚠️ This
also means **`ctest -L bench` is nondeterministic on this tree**, which T053's "full local gate" and
T055 must treat as a known pre-existing condition rather than a fresh regression. It is invisible in
CI because `tier1.yml`'s `bench` job is soft and runs only `placeholder_bench`.

**3. ⚠️ CROSS-SESSION MAGNITUDE COMPARISON IS INVALID HERE, AND `vt11` PROVES IT.** Every version
measured 22–42 % faster than its T003 figure, including ones 082 cannot have improved. **`vt11` is a
natural control**: it is admin-only, emits no builders, and 082 changes nothing for it — yet it moved
≈2.05 s → 1.18–1.52 s (**−35 %**). `v50sp2`'s read tier is likewise proven bit-identical and moved
−35 %. A uniform improvement across untouched versions is an **environment delta between the two
measurement sessions**, not a code effect. The same pattern appears in T046 (FIX44/FIX50SP2 build
34–44 % "faster"), which corroborates it. **Consequently: no before/after magnitude on either profile
is claimed as an 082 effect. Only in-session absolute figures against the ceiling are.** Recording the
control's movement is what makes the confound visible — see
`[[feedback_ccache_cold_miss_overhead_is_below_runner_variance]]` for the same lesson on CI build
times, and `[[feedback_a_topology_measurement_is_not_a_behavioural_claim]]`.
