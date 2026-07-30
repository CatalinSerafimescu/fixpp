# Quickstart: Validating Structural Repeating-Group Detection

**Feature**: `082-structural-group-detection` | **Date**: 2026-07-29

Runnable scenarios that prove the feature end-to-end. Each names the requirement it discharges
and the observable **before → after**. Contract IDs refer to
[`contracts/group-detection.md`](./contracts/group-detection.md); decision IDs to
[`research.md`](./research.md).

## Prerequisites

```bash
cd research/G19-fix-fpml-iso20022/library      # all commands run from the submodule root
```

A configured build directory (any existing preset). Two traps apply to this feature specifically:

- **Force a clean codegen rebuild before trusting any golden diff.** Non-debug directories have
  silently compiled a stale `Reify.hpp`; a stale emitter object makes a golden comparison
  meaningless. Rebuild the codegen tool from scratch, not incrementally.
- **`tools/codegen/**` is touched ⇒ `ctest -L codegen` is mandatory.** A label-filtered run that
  omits it has previously missed a subsystem's COUNT pin.
- **Never pass repeated `-L` flags.** `ctest -L A -L B` is **conjunctive** — `ctest --help`
  (3.30.0): "With multiple `-L`, run tests where **each** regular expression matches at least one
  label". No test in this repo carries both `wire` and `session`, or both `codegen` and
  `dictionary`, so `ctest -L wire -L session` selects **0 tests and exits 0** — the canonical
  false-green shape. Use regex **alternation**, `-L "wire|session"`, as `.github/workflows/tier1.yml`
  does. Every scenario below states its **expected non-zero test count**, measured against a
  configured tree at the 082 branch point; a selection that returns 0, or materially fewer than
  stated, means the command or the labels are wrong, not that the feature passed.

---

## S0 — Baseline: reproduce the census oracle (no build required)

Establishes ground truth independently of the library. Run this **first**; it is the source K1–K3
assert against, and it must give identical output before and after the change (it reads XML, not
code).

```bash
python3 specs/082-structural-group-detection/contracts/predicate_census.py
```

**Expected** (run at Gate A round 1 and reproduced exactly, so this is a measured expectation, not a
hoped-for one): the C2 table — FIX40 `0/4`, FIX41 `0/7`, FIX42 `0/18` DIFFER; FIX43 `34/34` DIFFER
with `82` type-only and `576` struct-only, registered `33 -> 34`; FIX44/50/50SP1/50SP2/T11/FIX-Latest
EQUAL with registered `59 / 67 / 97 / 505 / 1 / 524`; the "declared `<group>` not message-reachable"
note listing `384`/`627` on FIX50/SP1/SP2; and **no zero-member `<group>` warnings** on any of the
ten — the **no-regression evidence for FR-023** (the fail-closed load rejection affects **0** shipped
dictionaries), and the standing measurement behind contract **P1-NON**.

*Discharges*: the evidence base for FR-005, FR-013, FR-014, FR-023's no-regression leg, and P1-NON ·
*Contract*: C2, K7, K11.

### S0b — Derive the v42 builder-plan counts (no build required)

The companion derivation for the **builder** tier, whose counts are not the tag counts S0 produces.
It self-validates against the three shipped tiers before printing the `v42` prediction, and exits
non-zero if any of them diverges — so a stale rule cannot silently produce a plausible number.

```bash
python3 specs/082-structural-group-detection/contracts/builder_plan_census.py
```

**Expected**: three `OK` lines (`v44 all` 83/88 with a name-for-name golden match, `v50sp2 all`
156/558 likewise, `v44 official` 33/54 matching `determinism_test.cpp`'s constant), then
`v42 --families all` → 39 msgs / **28** plans / 17 tags / **226** files, and
`v42 --families official` → 25 msgs / **19** plans / 11 tags / **147** files.

*Discharges*: the evidence base for FR-009, FR-016b, SC-006 · *Contract*: K8 · *Research*: D-9a.

---

## S1 — FIX42 groups become visible at the runtime tier

*Discharges*: FR-005, SC-001 · *Contract*: C1.1, K1, K4 · *US1*

```bash
ctest --test-dir <build> -L dictionary --output-on-failure
```

**Expected selection: ≥ 16 tests** (16 at the branch point; this feature adds more). A selection of
0 means the label filter is wrong.

**Before**: `as_table_view()` on `FIX42.xml` registers **0** groups.
**After**: exactly **18**, each with its declared member set — matching S0's **registered-after**
column (reachability-restricted, *not* the raw struct column) by exact-set equality in **both**
directions. FIX40 → 4, FIX41 → 7.

Also asserts K4, and asserts it on a subject that **can** fail: for `NoRelatedSym(146)` — 4 distinct
direct-member lists across its 6 FIX42 occurrences — the **063 context store holds the distinct
member set per `(msg_type, parent path, no_tag)`**, equal to the oracle's per-context set, while the
bare store holds the loader's first-seen set. A tag-set projection across the two stores is not
sufficient (it passes while every per-context member set is wrong), and `LinesOfText(33)` is not a
valid subject (its two occurrences carry identical members `{58, 354, 355}`, so a collapse on it is
unobservable). A pass on one store alone would be a half-restructure.

### S1b — A member-less `<group>` is a LOAD ERROR, in both loaders

*Discharges*: FR-023, SC-013 · *Contract*: C1.1, P1-NON, K11 · *OD-1, resolved by the user 2026-07-30*

Same command as S1 (`-L dictionary`, ≥ 16 tests).

**Before**: a `<group>` element with no resolvable `field`/`group`/`component` child loads
**silently** — `first_field_tag` stays 0 (`xml_loader.cpp:610`, recorded `:644`), and
`group_first_field` then cannot tell it from "not a group" (`dictionary.cpp:92-99`). It was the only
structurally-broken group form still passing: `xml_loader.cpp:584` already rejects a `<group>` whose
name resolves to no `<field>`, and `:1017`'s delimiter-collision guard already excludes the
zero-delimiter case explicitly via `g.first_field_tag != 0`.

**After**: the load **throws** — `fixpp::dict::xml_parse_error` from the `<fix>` loader,
`fixpp::dict::orchestra_parse_error` from the Orchestra loader (the latter derives from the former,
so one `catch (fixpp::dict::xml_parse_error&)` covers both) — with a diagnostic naming the offending
group's `name` and its `no_tag`, per `error.hpp:73`'s "facts an operator needs" convention. No new
exception subclass and no `fixpp::core::error` variant, so the C-ABI surface is untouched (FR-017).

Two things the pin must get right, or it cannot fail:

- **Assert it per loader**, not once — they are separate code paths and both are in scope.
- **Put the member-less `<group>` at a NON-first-seen occurrence of its `no_tag`.** Both `GroupDef`
  records sit inside a first-seen-wins dedup guard (`xml_loader.cpp:609`,
  `orchestra_loader.cpp:626`); a check placed *inside* that guard would make the rejection depend on
  declaration order — not fail-closed — and a fixture whose member-less group is its tag's only
  occurrence would pass such an implementation anyway.

**No-regression leg**: all **ten** vendored dictionaries still load clean. S0 is the standing
measurement that none declares a member-less `<group>`, so **0** shipped dictionaries are affected —
but this *is* a loader behavior change for third-party XML, which is why FR-023 also requires a B&L
behavior row + operator-facing release note (S8).

**Why this is the payoff, not a bolt-on**: with the state unreachable at load, contract C1.3
**P1-NON** stops being a tolerated limitation and `group_first_field(t) != 0` is exactly the C1
predicate over every dictionary the loaders admit. The sentinel is still ambiguous *read in
isolation* — that has not changed and should not be claimed otherwise; its **input** is what became
unreachable.

---

## S2 — FIX43: one tag moves, one tag must not

*Discharges*: FR-011, FR-012, FR-013, SC-003 · *Contract*: C2, K3 · *US3*

Same command as S1 (`-L dictionary`, ≥ 16 tests).

**After**:
- tag **576** `NoClearingInstructions` **is** registered, member `ClearingInstruction`, and a
  populated group reads membership-bounded (before: absent / `TYPE_MISMATCH`);
- tag **82** `NoRpts` is **not** registered, and `ListStatus` still enforces it as a plain
  **required** field — unchanged from today (no-regression pin, D-2);
- the FIX43 set differs from baseline by exactly `+1 tag (576)`.

---

## S3 — Non-regression on the six unaffected dictionaries

*Discharges*: FR-014, SC-002 · *Contract*: C3, K2

Same command as S1 (`-L dictionary`, ≥ 16 tests). FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT11 /
Orchestra FIX Latest must register **exactly** their pre-feature sets — **59 / 67 / 97 / 505 / 1 /
524**, 0 additions, 0 removals. Exact-set, not containment: a subset check passes while silently
dropping a group.

**Compare against the *registered* column, never the declared one.** FIX50 / SP1 / SP2 **declare**
69 / 99 / 507 group tags but **register** 67 / 97 / 505 — `NoHops(627)` is unreachable behind their
empty `<header/>` (FIXT owns the header; 081 / L-041-2) and `NoMsgTypes(384)` belongs to FIXT11's
`Logon`. An oracle wired to declared sets fails these three rows spuriously, and the usual next step
is that someone weakens the assertion until it stops pinning anything (FR-018).

---

## S4 — The parse correction is ungated

*Discharges*: FR-006a, SC-008, SC-008a · *Contract*: C4.1 · *US1*

```bash
ctest --test-dir <build> -L "wire|session" --output-on-failure
```

**Expected selection: ≥ 13 tests** (13 at the branch point — 4 `wire` + 9 `session`). Note the
quotes and the `|`: the previous revision of this scenario used `-L wire -L session`, which is
conjunctive and selects **0 tests** while exiting 0 — a vacuously green run of the feature's
compat-critical scenario.

Feed a FIX 4.2 `MarketDataSnapshotFullRefresh` with a populated `NoMDEntries(268)` group to a
session with **`validate_inbound_messages` OFF**.

**Before**: a typed / C-ABI group query returns `TYPE_MISMATCH` or absent.
**After**: a membership-bounded read — *with the strict flag still off*, which is the point. The
flag must be off in this scenario so the parse axis and the validation axis are never conflated.

Companion assertion (SC-008a): with the flag off, **no** FIX40/41/42 message is newly *rejected* —
read shape changes, acceptance does not.

---

## S5 — Regenerate and diff every version (the discriminating check)

*Discharges*: FR-015, FR-016, FR-016a, **FR-021**, SC-004, SC-005 · *Contract*: K5 · **Highest-value scenario**

FR-021 belongs here because its two inputs are exactly this scenario's: the **class** side is parsed
from the text of the `v42/Messages.hpp` this step regenerates, and the **structural** side is S0's
oracle. It is sequenced as `plan.md` § Implementation Sequencing step **5b**.

```bash
cmake --build <build> --target fixpp-codegen   # after a CLEAN rebuild — see Prerequisites
ctest --test-dir <build> -L codegen --output-on-failure
```

**Expected selection: ≥ 32 tests** (32 at the branch point).

**Must be byte-identical**: `v44`, `v50sp2`, `vt11`, `vlatest` read goldens; `v44`, `v50sp2`,
`vlatest` builder golden sets; **and** `v42`'s own `Fields.hpp` + `Validator.hpp` (D-4/D-10 —
`FieldRef::type` is not modified and `emit_validator` has no group axis, so these are real
predictions, not tautologies).

**Must change**: `v42/Messages.hpp` keeps its **46** message classes and gains exactly **18**
`class G_` (from 0) plus group accessors on the 22 group-bearing messages; `v42/Reify.hpp` gains the
matching owning-class accessors.

**Scope of "every `v42` artifact": five, not six.** The generated `v42` directory holds
`Fields.hpp`, `Messages.hpp`, `Validator.hpp`, `Reify.hpp`, `NormativeReferences.md` — **no
`Manifest.txt`**. `MessageIR::occurrences` is populated only on the Orchestra path, so
`emit_manifest` returns empty for `<fix>`-schema versions and no file is written; `vlatest` is the
only version with a manifest. Do not look for a `v42` manifest diff.

**Only one of the five has a checked-in golden** (`Messages.hpp`, in the 003 corpus). So a moved
count in the other four cannot be caught by a golden diff at all: the by-construction
reconciliation for the group axis is FR-021's class-side ⟷ raw-XML gate, and the remaining axes are
pinned by recording this diff in the `/speckit-verify` record. Every moved count needs a
by-construction reconciliation to FIX42's declared structure. A golden that merely "regenerated" is
not evidence — an unexpected delta is a finding.

---

## S6 — The v42 typed builder tier exists and cannot silently omit a group

*Discharges*: FR-007, FR-008, FR-009, FR-010, FR-016b, SC-006 · *Contract*: C4.3 · *US2 — the #196 deliverable*

Same command as S5 (`-L codegen`, ≥ 32 tests).

**Before**: `V42EmitsNoBuilders` asserts `v42/all.hpp` and `v42/messages/` are absent.
**After**, that test is **inverted** (not deleted; its `vt11` companions stay untouched) and:

- the full 078 split layout is emitted for `v42` — `messages/<Msg>.hpp`, `groups/<PlanName>.hpp`
  + `groups.hpp`, `validators/traits.hpp`, `all.hpp` — over all **39** application messages, as
  **226** files: 39 × 5 per-message files + **28** shared `groups/<PlanName>.hpp` + 3 shared
  headers, `builder_registry` cardinality **39**;
- **the plan count is 28, not 18.** 077's dedup keys on `(no_tag, recursive signature)`, and 7 of
  FIX42's 18 group tags fork across contexts (`146` → 4, `73` → 3, `295` → 3, `78`/`268`/`296`/`420`
  → 2 each). Only **17** tags reach the builder tier — `384 NoMsgTypes`'s only host is `Logon`,
  which is `msgcat='admin'` and outside `emit_builders`' `is_application` scope. So the read tier's
  18 `class G_` (per tag, admin included) and the builder tier's 28 plan headers over 17 tags are
  **both correct**; they are different keyings, not a discrepancy (research D-9a). Never use "18
  group tags" as a file count;
- `validate_<Msg>` **rejects** an `Args` value omitting a group declared `required='Y'`, at all
  **14** message/group pairs (e.g. `NewOrderList`/`NoOrders`). This is the Article-VI heart of the
  feature: the group is representable, so its absence is *detectable* rather than silent — the
  exact failure mode that forced the 077 descope. **One of the 14 needs a different construction**:
  `MassQuote`/`NoQuoteEntries(295)` is nested inside the required `NoQuoteSets(296)` and is checked
  per-*entry* via `gc.validate_entry`, so its omission case is *a 296 entry carrying an empty 295
  span*, not a missing top-level group. Build it that way or the pin covers 13 plus a lookalike;
- `emit_builders` output matches the new `v42` golden **set** under the default `--families all`;
- **the `--families official` leg is a structural witness, not a golden.** 078 retired the
  official-mode byte gate (`determinism_test.cpp:898-909`) — no `<version>-official/` golden set is
  checked in for *any* version. `v42`'s instantiation of `OfficialModeBuildersStructuralShape`
  expects **147** files (25 messages × 5 + **19** plan headers + 3) and `builder_registry` **25**,
  against a fresh isolated `--families official` run. Plan *names* differ between modes (a tag bare
  under one can be ordinaled under the other), so never cross-compare the two modes by name;
- the expected sets above MUST be **derived from the interning rule**, not transcribed from the
  first run (FR-016b);
- no version-name predicate remains in the driver; `vt11` still self-skips via its empty registry.

---

## S7 — A grouped *and* nested FIX 4.2 write round-trips

*Discharges*: SC-007, closes L-061-1 · *US4*

Build a `MassQuote` with `NoQuoteSets(296)` containing `NoQuoteEntries(295)` — one of FIX42's five
nested group occurrences — via the `v42` builder tier.

**Expected**: emitted bytes match an independently-derived (QuickFIX) golden, and parsing them
back through the `v42` read tier round-trips every field and **both** group levels. Before this
feature no v42 grouped write was expressible at all; all five exemplars were forced to `v44`.

---

## S8 — **Governing-document** closure (not just B&L)

*Discharges*: FR-006c, FR-019, FR-020, **FR-023's B&L leg**, SC-010, SC-011, **SC-013's B&L leg**

**(a) `spec/behaviors-and-limitations.md`** —
- closes **L-063-1**, **L-061-1**, **L-066-1**, **L-077-1**;
- records the FIX43 `+1 tag (576)` correction with its evidence;
- carries the FR-006c named behavior change **and operator-facing release note**: FIX40/41/42
  inbound group reads change shape, and strict-validation deployments on those versions may see
  new rejects;
- carries **FR-023's** named behavior change + operator-facing release note: a dictionary declaring a
  `<group>` with no member no longer loads (both loaders), stating explicitly that **zero vendored
  dictionaries are affected** — the reachable population is third-party XML a consumer loads at
  runtime;
- refreshes L-066-1's own stale internal citation while rewriting that row —
  `behaviors-and-limitations.md:1749` cites "`dictionary.cpp:335`'s NumInGroup gate"; the gate is
  now at `dictionary.cpp:398`.

**(b) `.specify/constitution.md`** — the leg the previous revision of this scenario omitted, and the
feature's highest-visibility bookkeeping item, since #196's closure is the headline:
- **Article XVIII §7** (`:386`) currently states, in bold, "**`fixpp::v42` builders remain
  DEFERRED** … blocked on the L-063-1 structural-group-detection fix … tracked as issue #196" —
  exactly what 082 delivers. Replace that sentence with a delivered-by-082 record.
- The **Status banner** (`:85`) gains a **v0.11** line naming feature 082 (it currently records
  "v42 builders DEFERRED — L-063-1 zero-typed-groups, issue #196" at v0.9).
- **Article I §1** (`:94`) is **confirmed unchanged** — its codegen scope already reads "FIX 4.2,
  FIX 4.4, FIX 5.0 SP2, FIXT.1.1" per `[2c §1.3]`, so this is permissive, not a widening.
- The **v0.9 amendment-log entry** at `:18` carries the same sentence as *historical record* and
  MUST be left intact — only the live article text and the banner move.
- **Annotation-only, folded into the 082 branch** per v0.5/v0.9/v0.10 precedent — **not** a
  standalone `Constitution: amend …` PR. Article XX §2 requires Codex Gate A review on the
  amendment **and user sign-off**: both are discharged — Gate A converged at round 3 and the user
  **ratified** the amendment on **2026-07-30** (spec § Open decisions **OD-2**, RESOLVED). The edit
  itself is still this scenario's work.

Also confirm the four now-false carve-out comments are rewritten rather than left stale:
`required_scope_test.cpp:107`, `required_scope_census_test.cpp:341`,
`reused_tag_census_test.cpp:158`, `validator_type_check_test.cpp:966`.

---

## S9 — The FIX 4.2 group-parse benchmark (Article VIII §2)

*Discharges*: FR-022, SC-012 · *Research*: D-12

**Three** Article VIII obligations (FR-022 (a), (b), (c)) — **§2 re-baselining for (a) and (b), §3
run-and-record for (c)**, since (c) produces no `bench/baselines/` entry. Neither §2 nor §3 is scoped
to hot-path cost. Two **existing** profiles move — `as_table_view()` build time (a) and the `v42`
read-tier compile ceiling (c) — while (b)'s FIX 4.2 group-bearing parse path has **no existing
profile at all**, which is why it needs a new bench. The census behind that is closed over all **34**
profiles registered under `bench/`, not a `*.cpp` glob: two of them are script harnesses with no
`.cpp` at all (research D-12 §1).

**(a) `as_table_view()` build time — the leg that measures the diff directly.**
`bench/dictionary/table_view_footprint_bench.cpp` times `as_table_view()` **itself** (the
`Dictionary` is loaded once outside the loop), so it moves on **every** dictionary — the per-field
test goes from one enum compare to an O(log G) `groups_` binary search (`G = 507` on FIX50SP2). Set
equality does not bound it: FIX44/FIX50SP2 are C2 EQUAL and still move. It has **no**
`bench/baselines/` entry today — 075 recorded the numbers as an in-file comment block, so updating
that block does **not** discharge §2.

**(b) The FIX 4.2 group-bearing parse path — the downstream consequence.** `table_view::group_bits_`
is an exact pre-filter, and today **every** group bit is clear for FIX40/41/42, so every group probe
short-circuits on a bounds-check. After this feature the bits are set, so
`group_ctx_`/`group_members_` hash probes run, slices are built, and `group_member_fn` does real
work — per message. No existing baseline covers it: `bench/wire/{parser,offset_table}_bench.cpp` use
a **test-double** `table_view` (`support/mock_dict_table.hpp`) and load no dictionary at all;
`validator_bench` uses a real FIX44 `table_view` but builds it outside its measured window on a C2
EQUAL row; and `BM_XmlLoader_LoadFix42` measures `XmlLoader::load` only, never touching
`as_table_view()` or the parser.

**(c) The `v42` read-tier compile ceiling — an existing check that CI does not run.**
`bench/codegen/compile_time_bench/` has **no `.cpp`** (the measurement *is* a `clang++ -fsyntax-only`
run, registered via `add_test`, `LABELS bench`) and syntax-compiles one TU per version. 082 adds
**18** `class G_` classes to `v42/Messages.hpp` plus group accessors to `Reify.hpp`, so the `v42` TU
grows against a **load-bearing ≤3 s** ceiling that **only `v50sp2`** is exempt from — for `v42` an
overage sets `PASS=false` and `compile_time_bench.sh:139-143` does `exit 1` (the all-versions 15 s
ceiling, by contrast, is **WARN-only** and never exits). **CI will not catch it:** `tier1.yml`'s
`bench` job is soft and runs only `placeholder_bench` under `continue-on-error: true`, and no
workflow invokes `ctest -L bench`. Run it yourself and record the figure; no new bench and no
baseline file (it is a ceiling check, not a `bench/baselines/` comparison).

```bash
# (a) capture PRE-change first, then re-measure post-change (mirrors 075 T011/T032):
cmake --build <build> --target table_view_footprint_bench
./<build>/bench/dictionary/table_view_footprint_bench \
    --benchmark_repetitions=10 --benchmark_report_aggregates_only=true
#   → BM_TableView_BuildFix44, BM_TableView_BuildFix50SP2, the NEW FIX 4.2 row,
#     and BM_TableView_Sizeof (expect UNCHANGED — 082 adds no member; state the
#     group_bits_ heap growth separately, sizeof cannot see it).
#   → check in bench/baselines/dictionary/table_view_footprint_bench.json

# (b) the new FIX 4.2 group-bearing parse bench + its fresh baseline:
cmake --build <build> --target <the new fix42 group-parse bench>

# (c) the EXISTING read-tier compile-ceiling check (CI does not run it) — record v42:
ctest --test-dir <build> -R compile_time_bench --output-on-failure
#   → expect "v42 : <N> s → PASS"; a FAIL here (only v50sp2 is exempt) is a
#     real Article VIII finding, not a flake — and CI would not have shown it.

# then re-check the 8 pre-existing profiles that could be affected (SC-012's rule:
# benches executing dictionary / wire-parse / codegen-emitted code):
#   bench/baselines/wire/{framer_bench,offset_table_bench,parser_bench,validator_bench,writer_bench}.json
#   bench/baselines/codegen/typed_accessor_bench.json
#   bench/baselines/dictionary/{reify_bench,xml_loader}.json
```

**Expected**: both new baselines land in the same PR with their rationale in the PR body, (c)'s `v42`
figure is recorded and under ceiling, and the 8 pre-existing profiles stay within ±5%. **One of those
8 now carries FR-023's cost**: `bench/baselines/dictionary/xml_loader.json` times `XmlLoader::load`,
which is where FR-023's per-`<group>`-occurrence member check runs — expected negligible, measured by
this re-check rather than by a fourth FR-022 leg (research D-12 §1). Recording
"PASS (N/A) — no hot-path change" instead is true of the diff and false of the behaviour, of the one
bench that times the changed function, and of the compile gate over the grown headers. Watch (a)
closely: 075's own T032 re-measurement already put `BM_TableView_BuildFix44` at **+5.06%**.

---

## Full local gate

```bash
ctest --test-dir <build> -L "codegen|dictionary|wire|session" --output-on-failure
```

**Expected selection: ≥ 60 tests** (60 at the branch point). The previous revision wrote this as
`-L codegen -L dictionary -L wire -L session`, which is conjunctive and selects **0** — the single
most-likely-to-be-pasted command in this document, passing vacuously. Four separate invocations, as
in `plan.md` § Testing, are equally valid; a single repeated-`-L` invocation is not.

Then `/speckit-verify` for the sanitizer / coverage / static-analysis matrix. Note that
`/speckit-verify` is clang-only — the `gcc-release` and MSVC legs are CI-only jobs.
