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

### Concessions to #210 that must be REVERTED when it lands

All three are commented in-place as such:

1. **`required_scope_census_test.cpp` T017** — the per-context member-set leg allows exactly one
   extra tag, the global first-seen delimiter, and nothing else. Collapses to plain
   `EXPECT_EQ(members, actual)`.
2. **`collision_membership_guards_test.cpp`** — `first_tag_only_in` takes an `exclude` parameter so
   the injected delimiter is never chosen as a discriminator. Becomes a no-op; the parameter can go.
3. **`test_082_group_required_member_validation_test.cpp` T021b** — left **unrestricted and RED** on
   purpose. 5 of its 14 contexts fail because the runtime delimiter is global, not per-context. Do
   not write a restriction; #210 fixes it outright.

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

## Evidence index

| Artefact | Path |
|---|---|
| S0 census transcript | `<scratchpad>/pre-change/S0-predicate-census.txt` |
| S0b builder-plan derivation | `<scratchpad>/pre-change/S0b-builder-plan-census.txt` |
