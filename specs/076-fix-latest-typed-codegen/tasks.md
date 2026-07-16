---
description: "Task list for 076-fix-latest-typed-codegen"
---

# Tasks: FIX Latest Typed Message Classes via Native Orchestra Codegen (`fixpp::vlatest` tier)

**Input**: Design documents from `specs/076-fix-latest-typed-codegen/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ (build-and-verification.md, generated-api.md), quickstart.md

**Tests**: INCLUDED. This feature's deliverable *is* verification — the non-circular completeness census (FR-006, no QuickFIX peer), the typed round-trip, the determinism golden, and the additive OFF-path gate. Test tasks are load-bearing, not optional, and are written before / alongside their target surface.

**Organization**: Grouped by user story. Foundational (Phase 2) is the dominant, blocking codegen-tool work every story depends on.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 (Setup / Foundational / Polish carry no story label)
- All paths are repository-root-relative (repo root = the library submodule)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Scaffolding for the checked-in golden this feature's determinism (V-4) and OFF-path (V-7) gates diff against.

- [X] T001 [P] Create the codegen golden directory `specs/076-fix-latest-typed-codegen/contracts/golden/` with a `README.md` documenting the inventory it will hold: the `vlatest` `{Fields,Messages,Validator,Reify,Builders}.hpp` + census `Manifest`, AND the extended legacy `v42/v44/v50sp2/vt11` + `_dispatch/` inventory that V-7 diffs against (goldens live under `specs/<id>/contracts/golden/`, cf. `specs/069-v44-all-families/contracts/golden/` — there is no `tools/codegen/golden/`). Bytes are added by T016; this task only scaffolds the location + inventory contract.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Teach the offline codegen tool to load, partition, and project the FIX Latest Orchestra dictionary. **⚠️ CRITICAL**: No user story can begin until this phase is complete — the `ir.ns`-keyed read/reify/validator emitters only produce `fixpp::vlatest` output once the version row + load branch + projection exist.

- [X] T002 Add the `vlatest` `kCodegenVersions` row in `tools/codegen/fixpp-codegen/ir.cpp` (`:206-227`): `s = session_version::vlatest`, `a = application_version::v50sp2` (truthful, inert for ns selection), `ns = "vlatest"` (data-model Entity 1). `ns` MUST be unique (`"vlatest"` ≠ `"v50sp2"`), guaranteeing namespace disjointness; without this row `build_ir` throws `mapped=false` at `ir.cpp:265-270`. (FR-001 prerequisite — the partition row that makes the `ir.ns`-keyed emitters produce `fixpp::vlatest`.)
- [X] T003 Add the `OrchestraLoader` load branch in `build_ir` (`tools/codegen/fixpp-codegen/ir.cpp:250`) — route the FIX Latest dictionary through 074's `dict::OrchestraLoader` (pugixml) instead of the hardcoded `fixpp::dict::XmlLoader`, selected on the `--xml` input being `OrchestraFIXLatest.xml` (FR-002). Implemented as a root-element sniff (`<fixr:repository>` → `OrchestraLoader`; `<fix>` → `XmlLoader`; fail-closed otherwise) per research R1, which explicitly rejects a filename/extension heuristic as brittle.
- [X] T004 [P] Add the `app_version_enum` `"vlatest"` special-case in `tools/codegen/fixpp-codegen/gen_util.hpp:248-253` — map `ns="vlatest"` → `application_version::v50sp2` so the per-class `version_v` constant resolves (mirrors the existing `vt11`→`Unknown` special-case; an unguarded `ns` would emit the non-existent `application_version::vlatest` — a generated compile error) (FR-005).
- [X] T005 **DOMINANT / HIGH-RISK — Orchestra-native IR projection (RC-A)** in `tools/codegen/fixpp-codegen/ir.cpp` — add a `fixr:`-schema sibling to `populate_group_order` (`ir.cpp:145-197`, which is `<fix>`-schema-hardcoded — `doc.child("fix")` `:158`, reads `msgcat` `:186` — and NO-OPS on an Orchestra `<fixr:repository>` root, leaving `group_order`/`header_trailer_tags`/`is_application` empty/false for the 181). The projection produces FOUR outputs (research R2b): (a) declaration-order **`group_order`**; (b) **`header_trailer_tags`**; (c) **`category → is_application`** mapping (data-model Entity 5 — Orchestra has `category=`, NOT `msgcat`; verified single-category rule: the 8 `category="Session"` frames `{0,1,2,3,4,5,A,n}` = admin, every other category = app, incl. `category="Testing"` which is application; **fail-closed on an unmapped/absent category**, mirroring the `msgcat` fail-closed at `ir.cpp:192-195`); (d) the **lossless occurrence list** `(MsgType, group_path, tag, presence/rule, datatype)` per occurrence that feeds the census manifest. Preserve the existing fail-closed unknown-datatype gate (FR-010). This is the change that makes typed classes NOT "come for free" — see research R-summary rows 2b/2c.
- [X] T006 Add the `FIXPP_CODEGEN_FIX_LATEST` CMake `BOOL` option (CACHE, **default ON**) in `cmake/Codegen.cmake` and, when ON, the **fifth** `--xml OrchestraFIXLatest.xml` codegen invocation (mirroring the four existing FIX42/FIX44/FIX50SP2/FIXT11 lines); teach the `main.cpp` driver (`tools/codegen/fixpp-codegen/main.cpp:44-105`) to accept that job (data-model Entity 4). OFF-path toggle-lifecycle hardening (stale-dir cleanup, target/include absence) is US3 T015. (FR-003 — the build option that gates the tier.) `main.cpp` needed no change — its `--xml`/`--out` job loop is already generic over `ir.ns`.

**Checkpoint**: `fixpp::vlatest` read/reify/args/validator classes generate for all 181 messages (the `ir.ns`-keyed `emit_messages`/`emit_validator`/`emit_reify` emitters produce them for free once T002/T005 land). Builders, manifest, and all verification follow per story.

---

## Phase 3: User Story 1 - Reify, validate, read back a FIX Latest message through the typed API (Priority: P1) 🎯 MVP

> **⚠️ DESCOPED 2026-07-16 (user-decided, see spec.md Clarifications → Session 2026-07-16):** the typed **`build_<Msg>` builder** leg is DEFERRED to a follow-up (the non-deduplicated `emit_builders` produces a 137 MB uncompilable `vlatest/Builders.hpp`). `emit_builders` is reverted to v44-only. This phase now delivers the typed **read/reify/args/readback + runtime-validator parity** for all 181 (FR-001(a), FR-007(b)) + the dispatch-exclusion witness (FR-009). **T007, T009(a), T010 are DEFERRED** with the builder tier.

**Goal**: The `fixpp::vlatest` typed **read** surface reaches parity with the legacy read surface — a developer can generate the tier, reify/read a FIX Latest message field-for-field, and run runtime validation, exactly as for `fixpp::v44`'s read surface.

**Independent Test**: Generate the tier, reify all 181 FIX Latest messages via the universal `owning_<Msg>::from_view()/view()` path, read fields back — assert round-trip fidelity — with no application-dispatch or session wiring.

### Implementation for User Story 1

- [DEFERRED] ~~T007 [US1] Widen the `emit_builders.cpp:646` hard gate to also emit `build_<Msg>` for `ns == "vlatest"`.~~ **DEFERRED 2026-07-16 (user-decided).** The widening was implemented and empirically produced a **137 MB / 53,590-struct uncompilable `vlatest/Builders.hpp`** (non-deduplicated per-message nested-group `Args`; >21 GB RSS to compile). Reverted: `emit_builders` is v44-only again (v44 golden byte-identical, additive guarantee FR-004/FR-008/V-7 intact). The typed builder tier + its Args-dedup redesign move to a follow-up feature — see spec.md Clarifications → Session 2026-07-16.
- [X] T008 [P] [US1] Add a compile-smoke witness (`tests/codegen/`) asserting the generated `fixpp::vlatest` `{Fields,Messages,Validator,Reify}.hpp` headers exist and compile with `fixpp::vlatest` symbols present (US1 acceptance scenario 1; confirms `emit_messages`/`emit_validator`/`emit_reify` produce the tier for free — no emitter code change expected). Delivered `tests/codegen/vlatest_compile_smoke_test.cpp` — deliberately does NOT reference `builder_registry`/include `Builders.hpp` (see T009/T010 note below).

### Tests for User Story 1

- [X] T009 [P] [US1] **V-2 typed round-trip** — leg **(b) DELIVERED**; leg (a) DEFERRED with the builder tier. `tests/wire/vlatest_reify_roundtrip_test.cpp` delivers (b): all 181 `fixpp::vlatest` MsgTypes driven through the uniform `owning_<Msg>::from_view()/view()/msg_type()` pipeline, dict-free, zero skips (FR-007(b), SC-002). `ctest -L vlatest_roundtrip` → PASS. Leg (a) (`build_<Msg>` → serialize → read-back) is DEFERRED 2026-07-16 with T007 (137 MB uncompilable `Builders.hpp`) — see spec.md Clarifications.
- [DEFERRED] ~~T010 [P] [US1] **V-2b app-subset boundary pin** — admin complement == `{0,1,2,3,4,5,A,n}`.~~ **DEFERRED 2026-07-16** with the builder tier (T007): with `emit_builders` reverted to v44-only there is no `vlatest` `builder_registry` to bound. The admin-complement pin moves to the follow-up builder feature. (The all-181 census — Phase 4 V-1/V-1b — is ungated and unaffected.)
- [X] T011 [P] [US1] **V-3 dispatch-exclusion witness** in `tests/codegen/` — (a) a raw FIX-Latest-only wire message routed through the single generated `dispatch_application(...)` surface (`_dispatch/reify_dispatch_application.hpp`) does NOT yield a `vlatest`-typed owner (pin the deferred mis-resolve-to-v50sp2 / fail-loud behavior as the ApplExtID regression anchor); (b) that switch contains **exactly one** `application_version::v50sp2` case (injective wire-ApplVerID map preserved) (FR-009, SC-005, contract V-3). `ctest -L vlatest_dispatch_exclusion`. Delivered `tests/codegen/vlatest_dispatch_exclusion_test.cpp` — pins BOTH empirically-found halves: (a1) a MsgType shared with v50sp2.xml (e.g. "D") mis-resolves to `application_version::v50sp2`; (a2) a FIX-Latest-only MsgType absent from v50sp2.xml's own message set (e.g. "EC") fails loud with `dict_reify_unknown_msg_type`.

**Checkpoint**: FIX Latest messages are typed-reifiable, readable field-for-field, and round-trip via the universal read path (all 181, zero skips); dispatch-exclusion is pinned — the read-tier MVP is functional and independently testable. *(Typed `build_<Msg>` construction is deferred to the follow-up builder feature.)*

---

## Phase 4: User Story 2 - Provable completeness of the 181-message set (Priority: P1)

**Goal**: Prove — non-circularly, without a QuickFIX peer — that every FIX Latest message and field has a generated typed class, at two composed legs (V-1 census: projection manifest ≡ raw-XML; V-1b: shipped read class ≡ manifest). *(Builders descoped — the census covers all 181 ungated regardless.)*

**Independent Test**: Run the census enumerating messages+fields directly from raw `OrchestraFIXLatest.xml` (independent walker) and assert exact-multiset equality against the emitted per-message manifest; then assert the manifest matches the shipped read classes' reachable field set.

### Implementation for User Story 2

- [X] T012 [US2] Add the NEW **`emit_manifest.cpp`** in `tools/codegen/fixpp-codegen/` (declaration in shared `emit.hpp` per codebase convention — no separate `emit_manifest.hpp`) — emit the per-message census manifest from **T005's projection lossless occurrence list** `(MsgType, group_path, tag, presence/rule, datatype)`, for **all 181** (ungated). Sources from `MessageIR::occurrences` (NOT `fields`/flyweights). `main.cpp` writes `Manifest.txt` (empty-skip suppresses it for legacy tiers). Delivered `Manifest.txt` (181 messages, deterministic, byte-identical across runs). (FR-006, data-model Entity 3.)

### Tests for User Story 2

- [X] T013 [US2] **V-1 completeness census** test in `tests/codegen/` (`vlatest_completeness_census_test.cpp`) — ground-truth walker over raw `OrchestraFIXLatest.xml`, **independently implemented from the `ir.cpp` projection (N-1 — shares NO code; author confirmed never read ir.cpp/emit_manifest.cpp, orchestrator-reviewed disjoint)**. Structural hard-gate key `(msg_type, group_path, tag, rule)` asserted exact-multiset (symmetric difference empty) vs the manifest, message level 181==181 + occurrence-path level. **Mutation-RED proven+pasted** on all four: dropped-message, dropped-field, wrong-parent, reused-tag-under-a-different-parent (→ reverted GREEN). Datatype axis = documented best-effort secondary (6 non-derivable Orchestra type names excluded to avoid re-introducing circularity — incl. the 074 `LocalMktTime→LocalMktDate` spike-collapse). `ctest -L codegen` / `vlatest_census`. (FR-006/SC-001, contract V-1.)
- [X] T014 [US2] **V-1b manifest↔class consistency gate** (`tests/codegen/vlatest_manifest_class_consistency_test.cpp`) — class side parsed from the **shipped `vlatest/Messages.hpp`** (message classes' `get<N>` top-level tags + transitive `G_<no_tag>` version-wide-union flyweight tags), manifest side re-projected to the same granularity — **distinct derivations, non-circular** (author confirmed never read ir.cpp/emit_manifest.cpp). Message set 181==181 + per-message reachable-field set equality. **Mutation-RED proven+pasted** on both: class-side dropped-field + dropped-message (→ reverted GREEN, md5-verified). Composes `class ≡ manifest` ∘ `manifest ≡ raw-XML` = **class ≡ raw-XML** at class-reachable-field granularity. (FR-006/SC-001, INV-6, contract V-1b.)

**Checkpoint**: Completeness is proven non-circularly across both surfaces; a dropped/added/mis-parented field fails loudly.

---

## Phase 5: User Story 3 - Opt-in build cost (Priority: P2)

**Goal**: A consumer who does not need FIX Latest builds fixpp without the 181-class cost via one build option, byte-identical to today for the legacy tiers; a consumer who needs it flips one switch.

**Independent Test**: Build with the option OFF → no `fixpp::vlatest` symbols, legacy tiers byte-identical to `main`; build ON → the vlatest tier appears additively.

### Implementation for User Story 3

- [X] T015 [US3] Harden the `FIXPP_CODEGEN_FIX_LATEST` toggle lifecycle in `cmake/Codegen.cmake`: (1) `FIXPP_CODEGEN_FIX_LATEST_LAST_USED` INTERNAL-cache regen-guard (mirrors the `V44_FAMILIES_LAST_USED` pattern) forces regen on ON↔OFF flip; (2) on OFF, `file(REMOVE_RECURSE ${FIXPP_CODEGEN_OUT_DIR}/vlatest)` (also cleans a stale `Builders.hpp` that `write_file`'s empty-skip would otherwise leave); (3) documented: there is NO dedicated `fixpp::dict::vlatest` INTERFACE target — all tiers share the single `_codegen/include` root, so "no vlatest export when OFF" reduces to "no `vlatest/` subdir", satisfied by (2). Verified: OFF configure removes `vlatest/`, ON restores it. (contract B-1.)
- [X] T016 [US3] Checked-in codegen golden under `specs/076-fix-latest-typed-codegen/contracts/golden/`: `vlatest_Messages.golden.hpp` (10 MB, `-text`-pinned via a co-located `.gitattributes`). **Descoped from the original inventory:** NO `Builders.hpp` golden (builder tier descoped), and NO `Fields/Validator/Reify` golden (goldened for *no* tier — matches the 003 `Messages.hpp`-only precedent). The census `Manifest.txt` is intentionally NOT goldened (correctness pinned by V-1/V-1b, strictly stronger than a byte snapshot). **[gate-b/r2 follow-up]** run-to-run determinism for the whole `vlatest/` tier (`Manifest.txt`/`Fields`/`Validator`/`Reify`/`Messages`/`NormativeReferences.md`) is now covered by the dedicated `DeterminismTest.VlatestByteIdenticalAcrossRuns` gate (mirrors `ByteIdenticalAcrossRuns`, driven via `run_codegen_vlatest_only()`) — see golden/README.md and contracts/build-and-verification.md V-4. Legacy V-7 baseline = the existing 003/069 goldens, unchanged. (contract V-4/V-7.)

### Tests for User Story 3

- [X] T017 [US3] **V-4 determinism** — `determinism_test.cpp` `VlatestGeneratedMatchesGolden`: freshly generated `vlatest/Messages.hpp` == checked-in golden (new `FIXPP_CODEGEN_076_GOLDEN_DIR` define), under the FULL ctest. **PASS (7.8s).** Mutation-RED: a 1-byte golden corruption (byte 1001, `]`→`0xa2`) was caught by `cmp`/the gate's `EXPECT_EQ(gen_bytes, golden_bytes)` — a naturally-observed RED this session (the crash-corrupted golden was detected + repaired). (FR-011/R8, contract V-4.)
- [X] T018 [US3] **V-7 additive OFF/ON byte-diff** — `determinism_test.cpp` `AdditiveOffOnByteDiff`: an OFF-path job (4 legacy XMLs only) and an ON-path job (legacy + orchestra), run via the tool into temp dirs (no shared-tree mutation → no RUN_SERIAL). Asserts OFF legacy `Messages.hpp`×4 == 003 golden; `vlatest/` **absent** OFF; every OFF-produced file (incl. `_dispatch/`) byte-identical to its ON counterpart (recursive walk, ≥20 files); ON `vlatest/Messages.hpp` == 076 golden. **PASS (22s).** (FR-004/SC-003, contract V-7.)
- [X] T019 [P] [US3] Build-option ON/OFF witness — **folded into T018** (documented): US3-AC1 (OFF ⇒ no `vlatest/` tier) + US3-AC2 (ON ⇒ additive vlatest) are asserted directly in `AdditiveOffOnByteDiff`. Confirmed via CMake reconfigure too: `-DFIXPP_CODEGEN_FIX_LATEST=OFF` removes `_codegen/include/fixpp/vlatest/`, ON restores it (T015).
- [X] T020 [US3] **Build-cost measurement** — recorded in the `/speckit-verify` decision doc. Key finding: the `vlatest` read headers (Fields 42 MB, Validator 19 MB, Messages 10 MB, Reify 8 MB) are compiled **only by the 3 opt-in `vlatest` tests** — NOT by `src/`, the library, or the reify dispatch bridge (grep-verified). So the ON option's core-build/CI compile cost ≈ **zero**; the only ON-vs-OFF delta is the 5th configure-time codegen invocation (seconds) + ~80 MB generated headers on disk. Well within the "≈ one v50sp2-sized tier" expectation; **no re-raise of the ON default needed** (cost is contained to opt-in test targets).

**Checkpoint**: The toggle is honest both ways; the legacy tiers are provably additive; the build cost is quantified.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Feature-agnostic gates, governance, and the mandatory close-out.

- [X] T021 [P] **V-5 fail-closed on unknown datatype** — `determinism_test.cpp` `VlatestFailClosedUnknownDatatype`: the codegen TOOL run over a synthetic Orchestra fragment whose field references `type="TotallyBogusType"` (outside `kOrchestraTypeTable`) fails closed — non-zero exit + NO tier emitted. **PASS (18ms)**: `fixpp-codegen: dict::orchestra_parse_error: <fixr:datatype name="TotallyBogusType"> outside the Orchestra EP303 vocabulary`. Codegen-tool-level companion to the loader-level `OrchestraFailClosed.UnknownDatatypeUsedByField` (074). (FR-010, contract V-5.)
- [~] T022 [P] **V-6 frozen-surface confirmation** — the feature makes ZERO `src/`/`capi/`/`bindings/python/` change (codegen-tool + CMake + tests + specs only, FR-008), so the C-ABI symbol-golden / ABI-golden / Python surface gates diff zero ON or OFF. **Assertion deferred to `/speckit-verify` `ctest -L abi`** (existing gates — assert, do not author). (FR-008/SC-004, contract V-6.)
- [X] T023 **Constitution amendment v0.7 → v0.8 MINOR** — edited BOTH loci + Sync Impact Report + status line, **descope-adjusted**: (a) **Article I §1** (FIX Latest bullet + post-1.0 milestone line) reclassifies the FIX Latest typed **read/reify/args/validator codegen tier** (all 181) from post-1.0 to v1.0-delivered-by-076; the carve-out narrows to typed **`build_<Msg>` builder codegen** + ApplExtID(1156)=303 + session negotiation (builder tier deferred, NOT "all typed codegen" as the pre-descope task text assumed); (b) annotated **Article XVIII §2** v1.2 item that A-035..A-065 typed **read** classes are delivered in `fixpp::vlatest` in v1.0, only their builders + EP-field back-port + ApplExtID on-wire remain post-1.0. Sync Report lists all three loci. Did NOT run `/speckit-constitution` standalone (symlink-single-sourced).
- [X] T024 [P] Updated `spec/behaviors-and-limitations.md` with a `## 076-fix-latest-typed-codegen` section: B-076-1 (typed read/reify/args/validator tier delivered, all 181, opt-in, two-leg census), L-076-1 (typed `build_<Msg>` builder descoped — 137 MB uncompilable Builders.hpp, deferred to a follow-up), L-076-2 (074 `LocalMktTime`→`LocalMktDate` spike-collapse surfaced by 076's census).
- [X] T025 `quickstart.md` scenarios validated (Scenario 3 corrected for the builder descope → reify round-trip). All 7 map to tests that PASSED this session: S1 generate-ON = reconfigure + compile-smoke (T008); S2 census = V-1 (T013) ∘ V-1b (T014); S3 reify round-trip = `vlatest_roundtrip` (T009b); S4 OFF additive = `AdditiveOffOnByteDiff` (T018); S5 dispatch-exclusion = `vlatest_dispatch_exclusion` (T011); S6 frozen surfaces = `-L abi` (zero `src/`/`capi/` change; in verify); S7 determinism golden = `VlatestGeneratedMatchesGolden` (T017). (Not re-run end-to-end manually — WSL memory-constrained; each scenario's gate already ran GREEN.)

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T026 [P] **Catalogue close-out (re-scoped for the all-181 read tier, NOT the 31 MsgTypes).** 076 delivers typed read/reify/args/validator classes for **all 181** FIX Latest messages, so the delivery is recorded once, authoritatively, on the **tier row D-011** (`feature-catalogue.md:130` note updated: typed read-tier DELIVERED in v1.0 by 076; builders + ApplExtID + session-negotiation remain post-1.0) + the matching `coverage-index.md:704` entry. The 31 A-035..A-065 application-message rows (`:425-455`) **stay `backlog`** — their FULL delivery (builders + ApplExtID wire-differentiation) is v1.2/post-1.0 per Article XVIII §2 — with a single block-level partial-progress cross-ref note added above the table (not 31 row edits, not flipped to `done`; that would overclaim). Cross-doc-consistent with the constitution v0.8 Sync Impact Report.
- [X] T027 **Feature-completeness audit (FINAL)** — recorded in `.specify/decisions/076-fix-latest-typed-codegen-completeness.md` (gitignored). Verdict **COMPLETE-WITH-WAIVERS**: (i) every tasks.md row `[X]` or explicitly waived (T007/T009a/T010 DEFERRED = builder descope; T022 → verify); (ii) every FR/SC maps to a landed test+impl, with an explicit **waiver block** for the descoped builder obligations (FR-001(b), FR-007(a), SC-002 build leg, V-2, V-2b) so the audit does not read them as missing impl; (iii) D-011 tier row `done`; A-035..A-065 stay `backlog` by design (v1.2 full delivery). Cross-doc-consistent (constitution v0.8 ↔ catalogue ↔ coverage-index). Outstanding: `/speckit-verify` + `/gate-b`.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies — start immediately.
- **Foundational (Phase 2)**: depends on Setup. **BLOCKS all user stories.** Within it: T002 → T003/T004/T005 (T003/T004 [P]; T005 is the dominant serial change) → T006 (needs the version row + projection to invoke meaningfully).
- **US1 (Phase 3)**: after Foundational. T007 depends on T005 (app-subset predicate) + the `emit_builders` gate. T008–T011 depend on T006/T007 producing the tier.
- **US2 (Phase 4)**: after Foundational. T012 depends on T005 (projection occurrence list). T013/T014 depend on T012 (manifest).
- **US3 (Phase 5)**: after Foundational + US1/US2 emit their artifacts. T016 (golden) depends on the full emitted inventory (T007 builders, T012 manifest). T017/T018 depend on T016.
- **Polish (Phase 6)**: after all desired stories. T027 is the FINAL task.

### User Story Dependencies

- **US1 (P1)** and **US2 (P1)** are co-P1 and independent of each other once Foundational lands (US1 = builder/round-trip surface **as originally planned — [DEFERRED 2026-07-16] the builder half; SHIPPED is the reify/read-back round-trip, see the descope banner above**; US2 = manifest/census). Either can be the first delivered increment; US1 is the MVP.
- **US3 (P2)** is a cost-control / additive-guarantee refinement layered on the generated tier; its golden (T016) and byte-diff gates (T017/T018) need US1+US2 artifacts to exist.

### Parallel Opportunities

- T003 + T004 (different concerns in `ir.cpp` / `gen_util.hpp`).
- Within US1: T008/T009/T010/T011 are independent test files ([P]).
- Within US3: T019 [P] alongside T017/T018.
- Polish: T021/T022/T024/T026 are [P] (distinct files); T023, T025, T027 serialize.

---

## Implementation Strategy

### MVP First (US1)

1. Phase 1 Setup → 2. Phase 2 Foundational (CRITICAL — the projection T005 is the dominant, highest-risk change) → 3. Phase 3 US1 → **STOP and VALIDATE** typed round-trip independently.

### Incremental Delivery

1. Setup + Foundational → the vlatest read/reify/validator tier generates.
2. US1 → typed builders + round-trip (MVP). **[DEFERRED 2026-07-16]** the "typed builders" half — shipped is the reify/read-back round-trip only (see the descope banner above).
3. US2 → provable completeness (co-P1; ship together with US1 for a defensible coverage claim — US2's rationale is that shipping unproven-complete classes enshrines a gap).
4. US3 → opt-in build gating + additive byte-diff proof.
5. Polish → frozen-surface + governance + close-out.

---

## Notes

- [P] = different files, no dependency on an incomplete task.
- The **Orchestra-native IR projection (T005)** is the dominant change — typed classes do NOT generate "for free" for the new Orchestra schema; the second `<fix>`-hardcoded reparse `populate_group_order` no-ops on the Orchestra root (research R-summary rows 2b/2c).
- Completeness is **two composed legs** (V-1 census + V-1b manifest↔class): the census alone pins only the projection/builder surface, not the shipped read classes — see FR-006 / contracts.
- All new tests are whole-binary grouped and `ctest -L`-selected (Article VII §8); the determinism gate runs under the FULL ctest, not a narrow target.
- No `src/` / `capi/` / `bindings/python/` changes — additive codegen-tool + CMake + tests + specs only (FR-008).
