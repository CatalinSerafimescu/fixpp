# Implementation Plan: Structural Repeating-Group Detection for Legacy FIX Dictionaries

**Branch**: `082-structural-group-detection` | **Date**: 2026-07-29 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/082-structural-group-detection/spec.md`

## Summary

Repeating-group detection is gated on the XML-declared field **datatype**
(`FieldRef::type == field_data_type::NumInGroup`) at **11 detection line-sites** across the runtime
dictionary (3) and the codegen emitters (8), plus **1 driver site** — 12 changed lines in all.
*(Counting unit: one **line**-site, used consistently in this document and in research D-7. An
earlier revision wrote "nine sites", which counted D-7's disposition **rows** while writing the test
figure in lines.)* Notably the C-ABI outbound **write** path is already structural
(`src/capi/message_write.cpp:157/719/812/923`) — the library is asymmetric, not uniformly
group-blind, and that is what makes `group_first_field` the right predicate rather than a new one.
FIX 4.0/4.1/4.2 type every `<group>` count field `INT`, so those
dictionaries register zero groups — `fixpp::v42` has no typed group accessors, legacy inbound
group reads are absent or positionally-wrong, and the 077/078 typed builder tier had to exclude
`v42` outright (issue #196).

The technical approach, established in [research.md](./research.md), is a **re-point onto
structural truth that already exists on both sides** — not new dictionary plumbing:

- **Runtime** — swap the datatype test for **`group_first_field(fr.tag) != 0`** (D-1). That is an
  existing *public* accessor which binary-searches the `groups_` table built solely from the
  `<group>` element, **and it is already the predicate the C-ABI write path uses in production** —
  so this converges the runtime tier onto one predicate rather than adding a second (C1.3 P4).
  **No new API, public or internal.**
- **Codegen** — `MessageIR::group_order` is *already correctly populated for FIX42 today*
  (`ir.cpp:80` `walk_level` keys on the element name). Add one codegen-tool-local
  `VersionIR::group_tags` set derived from it, and have all **8 emitter line-sites** (6 disposition
  rows) consult that one value (D-3).
- **Driver** — delete `if (ir.ns != "v42")` (`main.cpp:132`) with **no** replacement version
  predicate; `vt11` continues to self-skip via its genuinely empty application registry (D-8).

`FieldRef::type` is not modified (D-4), which is what makes the byte-identity predictions
falsifiable rather than tautological.

## Technical Context

**Language/Version**: C++23 (library + host codegen tool); Python 3 for the checked-in census oracle

**Primary Dependencies**: pugixml (loader + codegen-tool-local re-parse), GoogleTest. No new dependency.

**Storage**: N/A — in-memory dictionary metadata plus checked-in golden files

**Testing**: GoogleTest via ctest labels — **four separate invocations**, or regex alternation
(`-L "codegen|dictionary|wire|session"`). **Never repeated `-L` flags**: `ctest -L A -L B` is
conjunctive (`ctest --help`, 3.30.0: "With multiple `-L`, run tests where **each** regular
expression matches at least one label"), and no test in this repo carries both `wire` and `session`
or both `codegen` and `dictionary`, so such a command selects **0 tests and exits 0**. Expected
non-zero selections at the branch point: `-L codegen` 32, `-L dictionary` 16, `-L wire` 4,
`-L session` 9, `-L "wire|session"` 13, `-L "codegen|dictionary|wire|session"` 60. pytest for the
SWIG bindings (untouched)

**Target Platform**: Linux (Clang + GCC) Tier 1; Windows/MSVC Tier 2; libc++ Tier 3

**Project Type**: C++ library with a build-time host codegen tool

**Performance Goals**: The **diff** is setup-time — `as_table_view()`'s only two production call sites are `src/session/session.cpp:992` and `:1234`, **both inside `open()`**, and the added `group_first_field` binary search runs in the setup loop's own iteration over `all_fields`, not per message. But the **behaviour** is not: registration flips FIX40/41/42 from a `table_view::group_bits_` all-clear short-circuit to real group-context resolution on the parse hot path, per message (research **D-12**). And a second profile moves that is **not** downstream at all: `bench/dictionary/table_view_footprint_bench.cpp` times `as_table_view()` **itself** (`:113-116`, `:129-132`), so its build time moves on **every** dictionary — the per-field test goes from one enum compare to an O(log G) `groups_` binary search — including on the C2 EQUAL rows whose set-equality bounds nothing here. And a third: `bench/codegen/compile_time_bench/` — a script-registered profile with **no `.cpp` and no `add_executable`** — syntax-only-compiles `v42/Messages.hpp` + `Reify.hpp` against a **load-bearing ≤3 s ceiling** that only `v50sp2` is exempt from (`compile_time_bench.sh:139-143` really does `exit 1`), and 082 adds 18 `class G_` to those headers — but **no CI job runs it** (`tier1.yml`'s `bench` job is soft and runs only `placeholder_bench`; nothing invokes `ctest -L bench`), so an overage would be invisible in CI. This is therefore treated as an Article VIII **§2 intentional perf change** with **three** benchmark legs in the same PR (**FR-022**), not a §3 N/A: (a) re-measure `BM_TableView_BuildFix{44,50SP2}` + a new FIX 4.2 row and check in `bench/baselines/dictionary/table_view_footprint_bench.json`; (b) the FIX 4.2 group-bearing parse bench + fresh baseline; (c) run the existing `compile_time_bench` and record the `v42` figure (a ceiling check, not a baseline). §2/§3 are **not** scoped to hot-path cost (`.specify/constitution.md:185-186`; §5 is the only hot-path clause). The ±5% budget applies to `bench/baselines/`, and every currently-**baselined** profile is provably unmoved — that leg stands: the wire parse/offset benches run against a **test-double** `table_view` (`support/mock_dict_table.hpp`) and load no dictionary; `validator_bench` builds its real FIX44 `table_view` outside the measured window (`:250` vs `:302-310`) on a C2 EQUAL row; and the one FIX42 bench, `BM_XmlLoader_LoadFix42` (`bench/dictionary/xml_loader_bench.cpp:47-57`), measures `XmlLoader::load` only — it never calls `as_table_view()` and never parses a message. **One qualification, added when OD-1 resolved to the fail-closed loader rejection:** FR-023 adds a per-`<group>`-occurrence member check *inside* `XmlLoader::load`, i.e. inside that last profile's timed region, so its no-move ground is narrowed — expected negligible, and **already covered** by the existing `bench/baselines/dictionary/xml_loader.json`, which is in SC-012's 8-file ±5% re-check set. No fourth FR-022 leg (research D-12 §1). What that leg omits, and (a)/(c) supply, is that **neither mover has a `bench/baselines/` entry** (075 recorded (a) in-file only; (c) is a ceiling check by design), so §2's budget had nothing to compare against. Bench census over a **closed** enumeration of all **34** profiles registered under `bench/` — 32 `add_executable` binary targets ∪ 2 `add_test` script harnesses that have no `.cpp` (a `*.cpp` glob alone would have missed them): research **D-12 §1**, which also records the one stated gap — nothing measures the `v42` **builder** tier's compile cost (D-11's risk row).

**Constraints**: C-ABI frozen at `1.5.0`, no symbol/signature change (FR-017). Read goldens for `v44`/`v50sp2`/`vt11`/`vlatest` and builder golden sets for `v44`/`v50sp2`/`vlatest` byte-identical (FR-015). Alloc discipline in `as_table_view()` unchanged — tables built once at setup, never per message.

**Scale/Scope**: 9 XML dictionaries + 1 Orchestra; effective registration delta on 4 of them (FIX40 +4, FIX41 +7, FIX42 +18, FIX43 +1 tag). Codegen/golden impact confined to `v42`: **46** messages (**39** `app`), **18** group **tags** on the read tier ⇒ 18 `class G_`; **17** tags reaching the builder tier ⇒ **28** distinct `(no_tag, signature)` plan headers and **226** emitted files under `--families all` (**19** / **147** under `official`) — research **D-9a**. *Do not use "18 group tags" as a file count.* **11 production detection line-sites + 1 driver line-site; 7 test line-sites** across 6 files (`reused_tag_census.hpp` contributes `:74` and `:80`) — all three figures in **line**-sites. **FR-023 is outside that census and does not change it**: it adds **one validation site per loader** (`xml_loader.cpp`, `orchestra_loader.cpp`) — not detection sites, so D-7's inventory is unaffected — and those two are the only production edits in the diff that are not detection or driver sites.

## Constitution Check

*GATE: evaluated before Phase 0, re-evaluated after Phase 1 design (below).*

| Article | Requirement | Status | Evidence / how satisfied |
|---|---|---|---|
| **VI** — 100% FIX rule | Every OFFICIAL row traceable; Normative References present | **PASS** | No new OFFICIAL rows — this is correctness of existing FIX 4.0–4.3 coverage. Normative References cite `coverage-index.md:189/239/307` plus the four L-rows being closed. FR-008 is the Article-VI heart: a `required='Y'` group must be *representable* in `Args`, so its omission is detectable rather than silent. |
| **VII** — Testing / TDD | RED→GREEN first; no code without a test; grouped isolation-safe tests selected by label | **PASS** | Every FR carries a pin (data-model.md § Pin map). Two existing descope tests invert (FR-016b) and are the natural RED starts. New tests join existing labelled buckets (`dictionary`, `codegen`, `wire`); no `gtest_discover_tests`. |
| **I §1** — Version scope | v1.0 codegen scope per `[2c §1.3]` | **PASS** | Article I §1 (`.specify/constitution.md:94`) already lists "FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1". 082 delivers a `v42` builder tier *inside* that scope — the change is **permissive**, moving the library toward Article I §1, not widening it. FIX40/41 (and FIX43/50/50SP1) stay runtime-only (spec § Assumptions). **No amendment to this article.** |
| **VIII** — Perf budgets | §1 perf-sensitive module has a bench; §2 ±5% vs `bench/baselines/` per profile, intentional perf changes re-baseline **in the same PR**; §3 no perf change without a bench in the same PR (`.specify/constitution.md:184-186` — **none of the three is scoped to hot-path cost**; §5 is the only hot-path clause) | **PASS (planned, §2 — three legs)** | **Not N/A, and all three legs are named.** The diff is setup-time (`as_table_view()`'s two call sites are both in `Session::open()`), but three profiles move — out of a closed census of the **34** profiles registered under `bench/` (D-12 §1). **(a) The changed function itself is benched.** `bench/dictionary/table_view_footprint_bench.cpp` times `as_table_view()` (075 T011 created it for exactly this — `bench/dictionary/CMakeLists.txt:23-26`, so §1 already classifies this module perf-sensitive), and its build time moves on **every** dictionary, C2 EQUAL rows included, because the per-field test becomes an O(log G) `groups_` binary search. It has **no** `bench/baselines/` entry (075 recorded it in-file at `:16-62`), so §2's ±5% budget has nothing to compare against. FR-022 (a) requires re-measuring `BM_TableView_BuildFix{44,50SP2}` + a new FIX 4.2 row and checking in `bench/baselines/dictionary/table_view_footprint_bench.json`. **(b) The downstream parse path moves**: registration flips FIX40/41/42 from a `group_bits_` clear-bit short-circuit to real group-context resolution *per message* (research D-12) — FR-022 (b) requires a FIX 4.2 group-bearing parse bench + fresh baseline in the same PR. **(c) The `v42` read-tier compile ceiling moves**: `bench/codegen/compile_time_bench/` (a script harness with no `.cpp`, so a `*.cpp` census cannot see it) syntax-only-compiles `v42/Messages.hpp` + `Reify.hpp`, which gain 18 `class G_`, against a **load-bearing ≤3 s** ceiling only `v50sp2` is exempt from (`compile_time_bench.sh:139-143` does `exit 1`; the all-versions ceiling is WARN-only). **No CI job runs it** — `tier1.yml`'s `bench` job is soft and runs only `placeholder_bench`, and nothing invokes `ctest -L bench` — so FR-022 (c) requires it run **manually** and the `v42` figure recorded; no new bench, no baseline (it is a ceiling check). Existing *baselined* profiles provably unmoved (that leg stands) — see Performance Goals; the 8-file re-check set and its selection rule are in SC-012. One stated gap: nothing measures the `v42` **builder** tier's compile cost — D-11's risk row, deliberately not an FR-022 obligation. |
| **IX** — Coverage / sanitizers / static analysis | ≥95% line, ≥85% branch on touched modules; ASan/UBSan/TSan; clang-tidy/format/cppcheck/IWYU | **PASS (planned)** | Touched modules: `src/dictionary/` (incl. **both loaders**, FR-023), `tools/codegen/`. The detection predicate **replaces** an existing branch rather than adding one, and each replaced branch is directly exercised by its pin. **FR-023 does add two error paths** — one `throw` per loader — so the earlier "no error path is added" ground no longer covers the whole diff; **no uncovered-error-path waiver is anticipated anyway**, on new grounds: each throw has its own dedicated rejection fixture (K11, one per loader, on a non-first-seen occurrence), so both new branches are directly covered. |
| **X** — ABI policy | No silent C-ABI break | **PASS** | FR-017: no `capi/` edit, no symbol-golden or abidiff regeneration; `FIXPP_C_ABI_VERSION` stays `1.5.0`. C-ABI *behavior* for FIX40/41/42 group reads changes — that is the fix — but no surface does. FR-023 likewise adds **no** surface: it reuses the existing `xml_parse_error` / `orchestra_parse_error` classes and appends **no** `fixpp::core::error` variant (`include/fixpp/dict/error.hpp:18-27`), so `error_message()`'s exhaustive switch and `tests/core/test_020_error_completeness.cpp`'s slot pin are untouched. |
| **XI** — Concurrency | — | **PASS (N/A)** | No threading, executor, or cancellation change. |
| **XII** — Security / TLS | — | **PASS (N/A)** | Untouched. |
| **XV** — Banned patterns | No per-message allocation, no hidden global state | **PASS** | `as_table_view()` still builds its tables once at setup; the added predicate is a lookup and allocates nothing. |
| **XVI §3** — `/clarify` mandatory (codegen + wire trigger) | Before `/plan` | **PASS** | Completed 2026-07-29 — 2 questions answered, 2 resolved from source, zero open items. |
| **XVI §4** — `/analyze` mandatory | Same trigger set | **PENDING** | Pipeline step 6, after `/speckit-tasks`. |
| **XVII §1** — Gate A required | Triggers include wire format / parser / **codegen layout** | **CONVERGED + USER-SIGNED-OFF 2026-07-30** | Triggered by the codegen-layout change. `/gate-a` ran after this plan and **before** `/speckit-tasks`. Rounds 1–2 applied; **round 3 converged** (Codex P1=0 P2=0 P3=2; Opus post-judging P1=0 P2=0 P3=5) and the user signed off 2026-07-30 — see § Gate A. |
| **XVIII §7** — Application-message codegen scope for v1.0 | Article text must not assert a false fact about the shipped library | **AMENDMENT REQUIRED (FR-020) — RATIFIED BY THE USER 2026-07-30** | `.specify/constitution.md:386` states, in bold, "**`fixpp::v42` builders remain DEFERRED** … blocked on the L-063-1 structural-group-detection fix … tracked as issue #196" — **exactly what this feature delivers**. Article XVIII §4 (`:383`) makes roadmap changes constitution amendments. FR-020 replaces that sentence with a delivered-by-082 record and adds a **v0.11** Status-banner line (`:85`); the v0.9 amendment-log entry at `:18` stays intact as historical record. **Annotation-only**, folded into this feature's branch per the v0.5/v0.6/v0.7/v0.8/v0.9/v0.10 precedent. The user **ratified** this amendment on **2026-07-30** (spec § Open decisions OD-2 — RESOLVED); the edit itself remains an **implementation-time** task, and `.specify/constitution.md` is not touched by this design bundle. |
| **XX §1-2** — Amendments | Not silently violatable; Gate A review on the amendment **+ user sign-off** | **SATISFIED 2026-07-30** | Article XX §1 (`:402`): "The constitution is amendable but **not silently violatable**. Any conflict between this document and a feature spec must be resolved by amending the article first (with rationale committed in the same PR), then proceeding." §2 (`:403-407`) requires Codex Gate A review on the amendment and user sign-off. **Both are now discharged**: Gate A reviewed the amendment as part of this bundle across three rounds and **converged** at round 3, and the user **ratified** the annotation-only Article XVIII §7 + Status-banner v0.11 amendment on **2026-07-30** (spec § Open decisions OD-2 — RESOLVED). Per the six-feature precedent this rides the 082 branch, **not** a standalone `Constitution: amend …` PR. |
| **Appendix A** — mandatory-trigger reference (authoritative on conflicts, per its own preamble, `.specify/constitution.md:414-424`) | Listed trigger categories require **all four** controls | **3 of 4 satisfied / 1 PENDING (`/analyze`, pipeline step 6)** | 082 hits at least two Appendix-A rows: **Codegen layout** — "dictionary loader, multi-version coexistence" (`:424`; the driver change + the new `v42` split layout) and **Wire format / parser** — "offset-table semantics, framing rules, **validator changes**" (`:423`; FIX40/41/42 group-membership and 079 per-group enforcement become reachable, FIX43 tag 576 registers). The four controls: (1) `/clarify` — **DONE** (2026-07-29, plus a Gate A round-1 session 2026-07-30); (2) `/analyze` — **PENDING** (pipeline step 6, after `/speckit-tasks`); (3) Codex Gate A — **CONVERGED at round 3 and user-signed-off 2026-07-30**; (4) **user `/plan` sign-off — GIVEN 2026-07-30.** The fourth was untracked in the reviewed revision, so nothing in the pipeline would have surfaced it; tracking it is what made it a decision the user could give. |

**Result: no article is violated once FR-020's annotation-only amendment lands. Nothing is pending the user — the Article XVIII §7 / Status-banner ratification and the Appendix-A user `/plan` sign-off were both given 2026-07-30 (spec § Open decisions OD-2, RESOLVED), and OD-1 resolved the same day to the fail-closed loader rejection (FR-023). The one remaining control is `/analyze`, at pipeline step 6. Complexity Tracking table omitted — nothing to justify.**

The design actively *reduces* complexity: one predicate replaces nine scattered datatype tests,
and it adds no public API (D-1). The alternative "additive enumeration accessor" was rejected
precisely because it would enlarge the frozen surface for no gain.

### Post-Phase-1 re-evaluation

Re-checked after `data-model.md` / `contracts/` / `quickstart.md` were written. **No gate moved.**
Phase 1 introduced exactly one new data element (`VersionIR::group_tags`, codegen-tool-local,
Article X unaffected) and no new runtime type, interface, allocation, or thread interaction. The
contract in `contracts/group-detection.md` is a *behavioral* contract over existing accessors, not
a new interface. Article VII's obligation strengthened rather than weakened: the pin map now names
a witness for all **28** FRs (FR-001..023 plus 006a/006b/006c/016a/016b). Provenance of the four
added after the original bundle: **round 1** added FR-020 (governing-document closure), FR-021 (the
by-construction `class G_` reconciliation), and FR-022 — at that point the Article VIII obligation
was the **FIX 4.2 group-parse bench alone**; **round 2** widened FR-022 to the `as_table_view()`
build-time re-baseline and the `v42` read-tier compile-ceiling run (NEW2-P2-1's re-based bench
census), giving it its final three legs; and **FR-023** (fail-closed loader rejection of a
member-less `<group>`) was added **post-convergence**, on the user's OD-1 decision of 2026-07-30.
FR-023 is the only one of the four that touches the loaders; it adds two error paths, both covered
(Article IX row above).

## Project Structure

### Documentation (this feature)

```text
specs/082-structural-group-detection/
├── plan.md                        # This file
├── spec.md                        # /speckit-specify + /speckit-clarify output
├── research.md                    # Phase 0 — decisions D-1..D-11
├── data-model.md                  # Phase 1 — entities, invariants, FR→pin map
├── quickstart.md                  # Phase 1 — runnable validation guide
├── checklists/
│   └── requirements.md            # spec-quality checklist (16/16)
├── contracts/
│   ├── predicate_census.py        # non-circular raw-XML oracle (checked in) — C2 / FR-018
│   ├── builder_plan_census.py     # v42 builder-plan derivation, self-validating — D-9a
│   └── group-detection.md         # the predicate contract
└── tasks.md                       # /speckit-tasks output — NOT created here
```

### Source Code (repository root)

```text
src/dictionary/
├── dictionary.cpp                 # as_table_view(): 3 detection sites (:398, :441, :446) — D-1
├── xml_loader.cpp                 # + reject a member-less <group> (throw xml_parse_error);
│                                  #   member scan :610-641, recorded :644, pushed :649;
│                                  #   OUTSIDE the first-seen dedup guard at :609 — FR-023
└── orchestra_loader.cpp           # + the Orchestra sibling (throw orchestra_parse_error);
                                   #   first_member_tag() == 0 at :629 (helper :467),
                                   #   record block :626-635, dedup guard :626 — FR-023

tools/codegen/fixpp-codegen/
├── ir.hpp                         # + VersionIR::group_tags — D-3
├── ir.cpp                         # populate group_tags from MessageIR::group_order
├── emit_messages.cpp              # :166, :234, :337, :347, :425 — D-3
├── emit_reify.cpp                 # :217, :227 — D-3
├── emit_builders.cpp              # :606 top_level_synthetic_members — D-3
└── main.cpp                       # :132 delete the v42 exclusion — D-8

tests/dictionary/
├── required_scope_oracle.hpp      # EXTEND with the group-tag census — D-6 / FR-018
│                                  #   (079's shared single oracle; do NOT fork a third walker)
├── reused_tag_census.hpp          # re-point to read that oracle — D-6 / FR-018
├── reused_tag_census_test.cpp     # carve-out text + FIX40/41/42/43 assertions
├── required_scope_test.cpp        # :107 carve-out comment now false — rewrite
├── required_scope_census_test.cpp # :341 carve-out + COUNT pins move
│                                  # + per-context member-set pin (tag 146) — FR-004/I-4a
└──                                # + the member-less-<group> load-rejection pins, one per
                                   #   loader, on a NON-first-seen occurrence, plus the
                                   #   ten-dictionaries-still-load-clean leg — FR-023 / K11

tests/codegen/
├── test_077_builder_no_emit.cpp              # V42EmitsNoBuilders INVERTS — FR-016b
├── test_077_v42_vt11_completeness_and_c4.cpp # expected(v42) ∅ → real set — FR-016b
│                                             #   (28 plans / 17 tags, DERIVED)
└──                                # + the v42 required-group omission pin (all 14 pairs,
                                   #   incl. the nested 296→295 per-entry construction)
                                   #   — FR-008 / SC-006 / K9
                                   # + the v42 class-side ⟷ raw-XML gate — FR-021
                                   # + the v42 --families official structural witness — FR-009 / K8

tests/wire/ · tests/session/       # ungated parse-correction pins, strict OFF — FR-006a

tests/capi/                        # cross-path P4 pin: fixpp_msg_group_begin's accepted
                                   #   tag set == the bare store's registered set — K6b

bench/wire/ · bench/baselines/wire/ # FIX 4.2 group-bearing parse bench + baseline — FR-022 (b)
bench/dictionary/table_view_footprint_bench.cpp  # + a FIX 4.2 row; re-measured — FR-022 (a)
bench/baselines/dictionary/table_view_footprint_bench.json  # NEW baseline (none exists) — FR-022 (a)

specs/003-dictionary-codegen/contracts/golden/v42_Messages.golden.hpp   # regenerates
specs/078-precompiled-builder-libs/contracts/golden/                    # gains v42 (--families all only)
spec/behaviors-and-limitations.md                                       # closes L-063-1/061-1/066-1/077-1 — FR-019
.specify/constitution.md                                                # Art. XVIII §7 + Status banner v0.11 — FR-020
```

**Structure Decision.** Existing layout; no new module, directory, or build target. The change is
confined to one runtime translation unit (`src/dictionary/dictionary.cpp`), the codegen tool, the
test tree, and the golden corpus. The only structural addition anywhere is a single
codegen-tool-local field on `VersionIR` (D-3) — **no runtime type gains a member**, which is what
keeps FR-017 and the `Fields.hpp` byte-identity prediction (FR-016a) intact.

## Implementation Sequencing (input to `/speckit-tasks`)

Ordered so that each step's evidence exists before the step that depends on it.

1. **Oracle first** — **extend `tests/dictionary/required_scope_oracle.hpp`** (079's shared,
   non-circular single oracle) with the group-tag / per-context-member census, and have
   `tests/dictionary/reused_tag_census.hpp` read *that* (FR-018/D-6). **Do not fork a third XML
   walker** — 079's own banner names the rule ("do NOT duplicate/fork the walker logic, that would
   break the single-oracle guarantee"). The extension MUST reproduce the reachability restriction
   (component expansion + `<header>`/`<trailer>` merge) and equal C2's **registered-after** column on
   all ten dictionaries, or SC-002 fails spuriously on FIX50/SP1/SP2 (declared 69/99/507 vs
   registered 67/97/505). This must land **before** the predicate change, or the census moves in
   lockstep with the code it checks and witnesses nothing.
2. **RED pins** — assert the target end-state while the code is unchanged: FIX40/41/42 register
   4/7/18 groups, FIX43 registers 576, tag 82 stays unregistered, `v42` emits builders. These go
   RED, including the two inverted 077 descope tests.
2b. **Loader fail-closed rejection** (FR-023 / K11) — **land this BEFORE the predicate change.**
   Add the member-less-`<group>` rejection to both loaders: `src/dictionary/xml_loader.cpp` throwing
   `xml_parse_error` and `src/dictionary/orchestra_loader.cpp` throwing `orchestra_parse_error`, each
   diagnostic naming the group's `name` and `no_tag` (`error.hpp:73`'s "facts an operator needs"
   convention). TDD: the two synthetic-fixture rejection tests go RED first, then the loaders.
   **Independent of step 1's oracle and of the detection re-point**, and placed here deliberately:
   once it lands, the zero-member state is **unreachable for everything downstream**, so steps 3–6
   inherit an exact `group_first_field` sentinel rather than a documented caveat (contract C1.1 /
   P1-NON). Two shape constraints: the check MUST sit **outside** the first-seen-wins dedup guards
   (`xml_loader.cpp:609`, `orchestra_loader.cpp:626`) so the rule is not order-dependent, and its
   fixture MUST put the member-less `<group>` at a non-first-seen occurrence so a wrongly-placed
   check fails the pin. Also assert the no-regression leg: all ten vendored dictionaries still load
   clean.

3. **Runtime predicate** (D-1) — all three `as_table_view()` sites in one change unit (FR-004).
4. **Codegen predicate** (D-3) — `VersionIR::group_tags` + all **8 emitter line-sites** (6 disposition
   rows in D-7: `emit_messages.cpp:166/234/337/347/425`, `emit_reify.cpp:217/227`,
   `emit_builders.cpp:606`).
5. **Regenerate + diff every version** (FR-015/FR-016) — the discriminating check. `v44`/`v50sp2`/
   `vt11`/`vlatest` must diff **clean**; `v42` read artifacts must match D-10's budget.
   Force a clean codegen rebuild first — the known emitter-staleness trap silently compiles a
   stale `Reify.hpp` in non-debug dirs.
5b. **The `v42` class-side ⟷ raw-XML consistency gate** (FR-021 / SC-004) — the by-construction
   reconciliation instrument behind the 0 → 18 `class G_` delta, placed here because it has exactly
   two hard predecessors and both are now satisfied: its **structural** side *is* step 1's FR-018
   oracle, and its **class** side is parsed from the text of the **regenerated** `v42/Messages.hpp`
   step 5 produces (extraction rule: `tests/codegen/vlatest_manifest_class_consistency_test.cpp:33-63`).
   Note the 076 V-1/V-1b *manifest*↔class pair cannot be instantiated for `v42` — V-1b keys on a
   `Manifest.txt` `v42` does not emit (FR-016) — so this is the class-side leg only. Without a step of
   its own an FR is how `/speckit-tasks` silently emits no task; SC-004 has nothing behind it if this
   is skipped.

6. **Driver exclusion + v42 builder pins** (D-8, FR-007/008/009/016b). Derive the expected plan set
   from `emit_builders`' interning rule **before** the first run (28 plans / 17 tags under
   `--families all`; 19 / 11 under `official` — D-9a), then check in the `--families all` golden set
   and instantiate the `--families official` **structural** witness (226 / 147 files). Build the
   14th required-group omission case as a 296 *entry* with an empty 295 span.
7. **US4 exemplar** — `MassQuote` `296 → 295` golden + read round-trip.
8. **Bench** (FR-022) — **three** legs, all Article VIII obligations for the same PR — **§2
   re-baselining for (a) and (b), §3 run-and-record for (c)** (leg (c) produces no baseline):
   **(a)** re-measure `table_view_footprint_bench`'s `as_table_view()` build-time profile
   (`BM_TableView_BuildFix{44,50SP2}` + a **new FIX 4.2 row**, pre-change figure captured first;
   `BM_TableView_Sizeof` re-reported with the `group_bits_` heap growth stated) and check in
   `bench/baselines/dictionary/table_view_footprint_bench.json` — this is the leg that measures the
   changed function, and no baseline for it exists today; **(b)** the FIX 4.2 group-bearing parse
   bench + fresh baseline; **(c)** run the existing `bench/codegen/compile_time_bench/` harness and
   record the `v42` TU figure under its load-bearing ≤3 s ceiling (`ctest -L bench`; no new bench,
   no baseline — it is a ceiling check, and only `v50sp2` is exempt from a FAIL). Then re-check
   SC-012's 8-file pre-existing set against ±5%.
9. **Docs and governing documents** — close L-063-1/L-061-1/L-066-1/L-077-1 and refresh L-066-1's
   stale `dictionary.cpp:335` cite to `:398`; add the FR-006c behavior change + release note **and
   FR-023's member-less-`<group>` load-rejection behavior row + release note** (stating explicitly
   that zero vendored dictionaries are affected); refresh the now-false carve-out comments in the
   four dictionary tests; **and amend `.specify/constitution.md`** — Article XVIII §7's "v42 builders
   remain DEFERRED" sentence plus a v0.11 Status-banner line (FR-020), annotation-only, in this
   branch. The user's ratification precondition (Article XX §2) is **already satisfied — given
   2026-07-30**, § Open decisions OD-2; the edit itself is this step's work. Closure that stops at
   B&L leaves the constitution asserting a false fact about the shipped library, which the next
   feature then inherits.

Because `tools/codegen/**` is touched, `ctest -L codegen` is mandatory locally — a label-filtered
run that omits it has previously missed a subsystem's COUNT pin.

## Gate A framing

`/gate-a` runs **after this plan and before `/speckit-tasks`** (pipeline step 4), not after tasks.
Reviewer attention is best spent on:

- **D-1** — the claim that **no new accessor is needed**: `group_first_field` is already public and
  already answers the structural question.
- **D-2** — the *corrected, weaker* case against a union predicate. A union is **not** wrong today;
  the argument is single-sourcing plus closing a latent trap. Review the argument as stated, not
  the stronger one the issue originally implied.
- **D-3** — `group_order` as the codegen source: keyed on the element name, not on a message's own
  field-run membership (I-6/P1). Note I-6a: `group_tags` is per **tag**, and the builder tier re-keys
  onto `(no_tag, signature)` **plans** — 18 read classes vs 28 plan headers over 17 tags.
- **D-5 — read the citation, not the comment.** The ungated-parse claim underpinning FR-006a, and
  the compat posture the user signed off on, rests on the comment at `session.cpp:992` being
  **stale**. The load-bearing evidence is the *consume* site:
  `Parser<access_mode::Index> pd_parser{*inbound_tv_}` at `session.cpp:328`, inside
  `parse_and_dispatch_`, with no `validate_inbound_messages` condition anywhere on that path. A
  reviewer who reads only the `:992` comment will wrongly challenge FR-006a.
- **C2 / D-10** — the reachability-restricted registration counts (**measured** by the oracle, not
  derived from set cardinality) and the per-artifact delta budget.
- **D-9a — the builder-tier plan arithmetic.** New in v2. The 28-plans-over-17-tags figure is
  derived from `emit_builders`' own `(no_tag, signature)` rule and validated by replaying that rule
  against the three shipped tiers (v44 all = 88 plans, exact golden name-set match; v50sp2 all = 558,
  exact match; v44 official = 54, matching `determinism_test.cpp`'s constant). Review the *validation*,
  not just the number.
- **D-1a / contract P1-NON** — the zero-member `<group>` disposition. The reviewed revision asserted
  a member-independence property that is false at the source; v2 records it as an explicit
  non-property with its representational reason and flags the fail-closed loader-rejection
  alternative to the user (spec § Open decisions OD-1). Review whether the non-property is stated
  honestly, not whether the property is restored. *(Written pre-review. **OD-1 has since been
  resolved 2026-07-30 in favour of the fail-closed loader rejection — FR-023**; the non-property is
  retired at the loader rather than documented around. See § Gate A round 3.)*

## Gate A

### Round 1 — disagreements

- **Codex P2-2 — "Two acceptance seams are not pinned to named test files" → NOT APPLIED
  (Disagree).** The repo convention is that `plan.md` § Project Structure names **directories** and
  `/speckit-tasks` names files. 082 is already **above** that bar, not below it:
  `specs/077-builder-args-dedup/plan.md:129,137` names `tests/session/` and `tests/codegen/` with no
  file names; `specs/079-required-presence-scope/plan.md:80` names `tests/` — one level coarser
  still; `specs/081-strict-validation-residuals/plan.md:91,97` names `tests/dictionary/` and
  `tests/wire/`. All three shipped through Gate A on that basis, while 082 already named six concrete
  existing test files. Holding 082 to a standard none of its three predecessors met is not a defect
  in 082, so Codex's proposed new file names (`test_082_legacy_group_parse_ungated.cpp`,
  `test_082_v42_required_group_validation.cpp`) are **not** adopted — naming them here would also
  pre-empt `/speckit-tasks`.
  **Its one genuine residual IS fixed**, carried forward as NEW-P3-2: FR-008/SC-006 had **no
  location at all** in § Project Structure — not a file, not a directory. § Project Structure now
  gives it `tests/codegen/`, and the FR → pin map in `data-model.md` carries a *Location* note for
  every previously unlocated pin (FR-004, FR-006/006a, FR-008, FR-009, FR-021, FR-022).

### Round 1 — round log

- Round 1 applied 2026-07-30: Codex P1=1 P2=2 P3=0; Opus post-judging P1=2 P2=9 P3=5; rewrite addresses root causes RC#1 (predicate contract text — predicate itself UNCHANGED), RC#2 (governing-document closure incl. Article XVIII §7 annotation), RC#3 (by-construction count discipline on the builder tier), RC#4 (verification instruments exercised). Reviews: research/reviews/codex_082-structural-group-detection_gate_a_review.md, research/reviews/opus_082-structural-group-detection_gate_a_adversarial_review.md.
- Two items are **flagged for the user, not decided** — spec § Open decisions **OD-1** (zero-member
  `<group>`: documented non-property vs fail-closed loader rejection) and **OD-2** (Article XVIII §7
  / Status-banner v0.11 annotation-only amendment ratification + user `/plan` sign-off).
- One finding was **corrected against the source during the rewrite**, in both reviewers' favour and
  against a premise both carried: NEW-P2-7 asserted that "`v42`'s `Manifest.txt` already carries
  structural group paths today" and that `v42`'s two shipped artifacts therefore contradict each
  other. **`v42` emits no `Manifest.txt` at all** — `MessageIR::occurrences` is populated only on the
  Orchestra path (`ir.cpp:476`), so `emit_manifest` returns empty for every `<fix>`-schema version
  (`emit_manifest.cpp:154-166`) and `main.cpp:29`'s empty-content skip writes no file; verified
  empirically against a generated tree (only `vlatest` has one). Consequences: FR-016's "all six
  emitted `v42` artifacts" is corrected to **five**, D-10's `Manifest.txt` row and SC-004's artifact
  list are corrected, and NEW-P2-7's fix shape is re-scoped — the 076 **V-1b manifest↔class** gate
  cannot be instantiated for `v42` (it keys on a manifest `v42` lacks), so FR-021 instead requires
  the **class-side ⟷ raw-XML** leg, which delivers the by-construction property FR-016/SC-004 need.
  Instantiating the full V-1/V-1b pair for `<fix>`-schema versions is blocked on `occurrences`
  population and is out of scope.
- One convention error inherited from the reviewed revision was also corrected: FR-009/D-10 asked for
  a `--families official` **pinned golden** for `v42`. 078 deliberately retired that gate
  (`determinism_test.cpp:898-909` — "no `v44-official/` golden set is checked in") in favour of the
  structural witness `OfficialModeBuildersStructuralShape` (`:920-948`). `v42`'s official-mode
  obligation is an instantiation of that witness, not a new golden directory.

### Round 2 — round log

- Round 2 applied 2026-07-30: Codex P1=0 P2=0 P3=0; Opus post-judging P1=0 P2=1 P3=4; rewrite closes NEW2-P2-1 (D-12's Article VIII bench census re-based on which benches TIME the changed function — adds bench/dictionary/table_view_footprint_bench.cpp, widens FR-022 to the as_table_view() build-time profile + checked-in bench/baselines/dictionary/ baseline, reconciles SC-012's budget list) plus NEW2-P3-1..P3-4 (P1 scoped to the detection predicate; D-7 second table +2 rows; group_bits_ line cites; "disjoint tag range" phrasing). Reviews: research/reviews/codex_082-structural-group-detection_gate_a_2_review.md, research/reviews/opus_082-structural-group-detection_gate_a_2_adversarial_review.md.
- The re-based census is enumerated over a **closed** basis, not by adding the one bench the review
  named — the finding's *class* is "a census missed an item", so the fix has to be a complete basis
  or it reproduces the finding one level up. A first attempt at "all 32 `bench/**/*.cpp` targets"
  was itself incomplete and was **discarded**: two bench profiles under `bench/` have **no `.cpp`
  and no `add_executable`**, because the measurement *is* a compiler invocation. The basis is now
  binary targets (`find` × `add_executable`) **∪** `add_test`-registered script harnesses = **34**
  profiles.
- **That correction found a second mover the review did not have, on a hard gate.**
  `bench/codegen/compile_time_bench/` syntax-only-compiles `<fixpp/v42/Messages.hpp>` +
  `Reify.hpp` (`compile_time_bench.sh:56`, `:69-83`) against `SINGLE_CEILING=3` s, which is
  **load-bearing** and exempts **only `v50sp2`** (`:93-99`); `:139-143` (`if ${PASS}; then … else …
  exit 1`) really does exit non-zero — verified at the source, since `set -e` would *not* catch a
  `false` variable. 082 adds 18 `class G_` to exactly those headers. But **no CI job runs it**:
  `tier1.yml`'s `bench` job (`:1099-1106`) is soft and executes only `placeholder_bench` under
  `continue-on-error: true`, and no workflow invokes `ctest -L bench` — so an overage exits non-zero
  locally and is invisible in CI. That is why FR-022 gains a third leg **(c)** as an explicit
  run-and-record obligation rather than a reliance on the gate; it is a ceiling check, so no baseline
  file. (The all-versions 15 s ceiling is WARN-only, `:132-135` — report, not gate.) `vlatest_builders_compile_bench` is `vlatest`-scoped and does not move
  (FR-015 byte-identity). One gap is now **stated rather than omitted**: nothing measures the `v42`
  *builder* tier's compile cost (226 new files) — recorded on D-11's existing "measure, don't assume"
  risk row, deliberately **not** promoted to an FR-022 obligation.
- Two further facts the review did not have sharpen leg (a): `bench/wire/{parser,offset_table}_bench.cpp`
  use a **test-double** `table_view` (`support/mock_dict_table.hpp`) and load no dictionary at all — a
  stronger no-move ground than set-equality; and the moving profile's 075 baseline lives as an
  **in-file comment block** (`table_view_footprint_bench.cpp:16-62`), not a `bench/baselines/` JSON,
  with `BM_TableView_BuildFix44` already recorded at **+5.06%** at 075 T032 — so §2 has no profile to
  compare against and the profile has a known-tight history.
- Two review-supplied line cites were **not** transcribed, because they do not resolve: the
  `dictionary.cpp:402` guard is at `:403-405` (`:402` is the `group_first_field` lookup), and
  `group_bit()` spans `:736-742`, not `:736-743` (`:743` starts `set_group_bit`). D-7 and D-12 carry
  the verified spans.
- **OD-1 and OD-2 remain open** — neither is resolved by this pass, and no finding required it.

### Round 3 — converged

- Round 3 reviewed 2026-07-30: Codex P1=0 P2=0 P3=2; Opus post-judging P1=0 P2=0 P3=5 — **CONVERGED** (bar: P1=0 ∧ P2=0). No third rewrite required; the rewrite budget (2/2) was not re-opened. Reviews: research/reviews/codex_082-structural-group-detection_gate_a_3_review.md, research/reviews/opus_082-structural-group-detection_gate_a_3_adversarial_review.md.
- Gate A **user-signed-off 2026-07-30**. Both open decisions resolved by the user the same day: **OD-1 → fail-closed loader rejection** (the alternative, not the bundle's default — see FR-023; deciding evidence was the loader's existing group-specific fail-closed dispositions at xml_loader.cpp:584 and :1017, censused after the loop closed) and **OD-2 → ratified** (annotation-only Article XVIII §7 + Status-banner v0.11, folded into this branch). Appendix-A user `/plan` sign-off given 2026-07-30.
- Post-convergence P3 sweep applied 2026-07-30 (round-3 findings, all non-blocking): Codex P3-1/P3-2 + the judge's five wording/placement items, incl. FR-021's missing sequencing placement.
- **Round-3 judge adjudication recorded: FR-022's leg (c) is LEGITIMATE, not unrequested scope.** Round 2's NEW2-P2-1 fix shape prescribed two legs and the rewrite shipped three; the judge ruled the third justified because a correctly re-based, *closed* bench census necessarily surfaces movers the reviewer did not name, and it verified every source fact behind leg (c) independently (`compile_time_bench.sh`'s `VERSIONS` incl. `v42`, `SINGLE_CEILING=3`, the `v50sp2`-only exemption, the hard `exit 1`, the WARN-only all-versions ceiling, and that no workflow invokes `ctest -L bench`). **Attribution correction carried into the pin rows:** the three obligations are **not** all Article VIII §2 — legs (a)/(b) discharge **§2** (re-baselining, `.specify/constitution.md:185`) because each produces a checked-in baseline, while leg (c) produces **no** baseline and therefore discharges **§3** (no perf change without a benchmark in the same PR, `:186`). Codex's proposed wording "Three Article VIII **§2** obligations" was **not** adopted for that reason; `data-model.md`'s FR-022 row and `group-detection.md` K10 now read "Three Article VIII benchmark obligations — §2 re-baselining for (a)/(b), §3 run-and-record for (c)". The judge also credited the round-2 rewrite for **declining** to promote the `v42` builder tier's compile cost into a fourth obligation, leaving it on D-11's risk row — the same discipline applied again here when FR-023 narrowed `xml_loader_bench`'s no-move ground (re-dispositioned in D-12 §1 and covered by the existing `bench/baselines/dictionary/xml_loader.json` in SC-012's 8-file set, **not** promoted to a fourth leg).
- **FR-023 is the one substantive post-convergence addition** (OD-1). It is loader-side, sequenced at step **2b** ahead of the predicate change, and its payoff is at the contract: with the member-less `<group>` rejected at load, C1.3 **P1-NON** goes from a tolerated non-property to a retired one, and `group_first_field(t) != 0` is exact over the dictionaries the loaders admit — the sentinel remains ambiguous *in isolation*, but its input is unreachable.
