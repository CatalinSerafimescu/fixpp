# Feature Specification: Native Orchestra Reader (FIX Latest)

**Feature Branch**: `074-orchestra-native-reader`

**Created**: 2026-07-13

**Status**: Draft

**Input**: User description: "native Orchestra reader — scope: spike-doc Deliverable #6 (`research/G19-fix-fpml-iso20022/remaining-work/orchestra-fix-latest-spike-and-plan.md`)"

## Overview *(context — not a template section)*

fixpp currently learns every FIX version it supports from **QuickFIX-format XML** dictionaries (the vendored "supported nine": FIX 4.0–5.0SP2 + FIXT.1.1). The Orchestra transpose spike (2026-07-10, verdict **GO**) proved that **FIX Latest (Extension Pack 303)** content — 181 messages, a depth-7 nested-group chain, heavily-reused group tags, 32 datatype tokens — fits fixpp's internal `Dictionary` model with zero structural blocker. The spike used a throwaway QuickFIX transpose as a *de-risk vehicle only*.

This feature delivers the chosen end-state: fixpp ingests the **official** FIX Trading Community machine-readable standard — `OrchestraFIXLatest.xml`, sourced from the Apache-2.0 channel — **natively**, via a new reader (sibling to the existing `XmlLoader`) that builds fixpp's own internal `Dictionary` **directly**, with no transpose, no QuickFIX-DataDictionary intermediate, and no dependency on any QuickFIX-community tool. Because the reader targets the *same* internal `Dictionary`, everything downstream that **consumes it on the read path** (the validator, the runtime `table_view`, the C-ABI read path) is unchanged. Codegen source is likewise untouched, but codegen does **not** consume a `vlatest` dictionary (`build_ir` throws on the unmapped session, `ir.cpp:265-270`) — typed FIX Latest codegen is a scheduled follow-on (see Non-Goals).

This is the **read path only** (Orchestra XML → internal `Dictionary`). It is v1.0-gating (`REMAINING-WORK.md` §A row 4b, user decision 2026-07-13).

## Clarifications

### Session 2026-07-13

- Q: How much of the FIX Latest version-identity surface should this read-path feature take on? → A: (initial) Full identity including a distinct `ApplVerID` enumerator. **SUPERSEDED by the reconcile below** once a factual check showed FIX Latest has no distinct wire ApplVerID.
- Q: FIX Latest has no distinct ApplVerID(1128) wire value (the enum caps at 9 = FIX50SP2; Extension Packs are signalled by ApplExtID(1156)=303, not a new ApplVerID) — how should version identity be modelled? → A: **`session_version::vlatest` only.** Add a distinct `session_version::vlatest` for the dictionary/codegen identity; model the **wire application version as the existing `v50sp2` (ApplVerID = 9)** — do **not** add an `application_version::vlatest` member and do **not** change `render_appl_ver_id` (keeps it injective / Gate-A-safe). FIX Latest's real on-wire differentiator, **ApplExtID(1156)=303, is deferred and explicitly scheduled for a next phase/round** (tracked in `REMAINING-WORK.md`). Session-layer negotiation wiring remains deferred.

### Session 2026-07-13 (Gate A round 1)

- Q: `session_to_application(vlatest)` maps FIX Latest onto the **existing** `application_version::v50sp2` slot (idx 8) of the 9-slot `version_registry` (keyed by `application_version`), whose ctor is documented silent last-writer-wins (`version_registry.cpp:71-72`). Since the ApplExtID-aware registry **re-keying** is deferred by the spike RECONCILE (`orchestra-fix-latest-spike-and-plan.md` L145-146), how should this feature avoid a silent registry-slot collapse when both a real FIX50SP2 dict and a FIX Latest dict are registered? → A: **Interim fail-loud guard, re-keying still deferred.** Add **FR-010**: a `version_registry` carrying both a FIX50SP2 and a FIX Latest dictionary MUST fail loud (release-effective — abort/fatal-in-ctor or an error-returning `build_version_registry`, not an NDEBUG-stripped `assert`), never silently overwrite one on the shared `v50sp2` slot. Record the shared-slot coexistence as a **Known Limitation** (L-074-1); full ApplExtID(1156)=303-aware re-keying stays scheduled for the follow-on phase. `SC-005` is reworded to scope "does not collide with the nine legacy identities" to the `session_version` (dictionary) layer and cross-reference FR-010 for the `application_version` registry-slot layer.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Load FIX Latest natively from the official Orchestra file (Priority: P1)

A fixpp user (or a downstream fixpp subsystem) points the library at the official `OrchestraFIXLatest.xml` and gets a fully-populated internal `Dictionary` for FIX Latest — fields, datatypes, codeset values, components, and groups — queryable exactly like any of the existing nine dictionaries. No QuickFIX transpose, no external tool, no intermediate file.

**Why this priority**: This is the feature. Without it there is no native FIX Latest support and v1.0 cannot ship (row 4b). Every other story is a property *of* this one.

**Independent Test**: Load the vendored `OrchestraFIXLatest.xml` through the new reader and assert the resulting `Dictionary` reports 181 messages and answers field/group queries — delivering a usable FIX Latest dictionary on its own.

**Acceptance Scenarios**:

1. **Given** the vendored `OrchestraFIXLatest.xml`, **When** the native reader loads it, **Then** the resulting `Dictionary` reports exactly 181 messages with zero drops.
2. **Given** a loaded FIX Latest `Dictionary`, **When** a caller queries an ordinary field by tag, **Then** it returns the field's datatype, name, required-ness, and (for codeset fields) the enumerated values with their descriptions.
3. **Given** a loaded FIX Latest `Dictionary`, **When** downstream **read-path** code builds the runtime `table_view` and validator input from it, **Then** those surfaces behave with no code change (the internal `Dictionary` is the same shape the existing loader produces). (Codegen `build_ir` is **not** a consuming surface here — it throws on an unmapped session for `vlatest`, `ir.cpp:265-270`; typed FIX Latest codegen is a separate follow-on — see Non-Goals.)

---

### User Story 2 - Deep and reused group shapes resolve queryably (Priority: P1)

The spike's discriminating result was not "the XML parsed" but "the runtime group-context machinery represents FIX Latest's deep and heavily-reused groups queryably." A user validating FIX Latest messages must be able to resolve the members and first-field of a group even when that group sits seven levels deep, and even when its tag is reused under many different parents.

**Why this priority**: Group-context correctness is the load-bearing invariant the validator depends on; a reader that parses but mis-represents groups is silently broken. Co-P1 with Story 1.

**Independent Test**: Query the deepest group of `MassQuoteAck` (msgtype `b`, the depth-7 chain) via its full parent path `296→295→555→40241→41686→41680→41683` and a reused group tag (e.g. `NoLegs`/555, reused under many parents) and assert the expected member lists and correct first-field via the context-keyed lookup (full-path for the depth-7 chain; non-empty-per-parent for the reused tag).

**Acceptance Scenarios**:

1. **Given** a loaded FIX Latest `Dictionary`, **When** the group-context tables are built, **Then** they build without throwing or truncating (depth 7 is well within the `K=16` context-depth cap).
2. **Given** the built context tables, **When** a caller looks up the members of the depth-7 group by its parent-path context key, **Then** the lookup returns the expected members and first field, and a reused-tag lookup (e.g. 555) resolves non-empty under each distinct parent.

---

### User Story 3 - FIX Latest carries a real, distinct version identity (Priority: P2)

FIX Latest is loaded under its **own** version identity derived from `version="FIX.Latest_EP303"` — not disguised as `FIX.5.0SP2`. An honestly-labelled FIX Latest dictionary is recognised as FIX Latest and does not collide with any of the legacy nine **at the `session_version` (dictionary) identity layer**. (Its wire `application_version` deliberately maps to the existing `v50sp2`; the interim guard against silent registry-slot double-registration is **FR-010**.)

**Why this priority**: The spike relabelled FIX Latest to `FIX.5.0SP2` as a hack to slip past the version gate; the real feature must give FIX Latest a genuine identity so it is nameable and distinguishable. This subsumes spike catalogue #0 (the single required loader change). It is P2 because Story 1 can load content first; the identity makes that load honest.

**Independent Test**: Load the real FIX Latest artifact and assert its resolved version identity is the new FIX Latest identity (not `v50sp2`), and that loading it does not require or produce the `FIX.5.0SP2` relabel.

**Acceptance Scenarios**:

1. **Given** `OrchestraFIXLatest.xml` with its genuine `version="FIX.Latest_EP303"`, **When** the reader resolves the version, **Then** it yields the new FIX Latest version identity, distinct from all nine legacy identities.
2. **Given** the new FIX Latest identity, **When** its wire application version is resolved, **Then** it maps to the existing `v50sp2` (ApplVerID = 9) — FIX Latest has no distinct ApplVerID(1128) value, so no new `application_version` enumerator is introduced and `render_appl_ver_id` is unchanged. (FIX Latest's ApplExtID(1156)=303 differentiation and session-layer negotiation are out of scope — see Non-Goals.)

---

### User Story 4 - Source is vendored, pinned, and attributed (Priority: P2)

The FIX Latest source artifact is vendored into the repository from the Apache-2.0 channel, pinned by upstream commit + content hash, and carries the Apache-2.0 §4 attribution plus an `UPSTREAM.txt` recording the Extension Pack. Builds are reproducible and license-clean.

**Why this priority**: Reproducibility and license hygiene are release-gating obligations, but they follow the reader existing. Independent of Stories 1–3's runtime behavior.

**Independent Test**: Inspect the vendored artifact + its provenance metadata and confirm the recorded commit/sha match the pinned upstream and the Apache-2.0 attribution is present.

**Acceptance Scenarios**:

1. **Given** the vendored `OrchestraFIXLatest.xml`, **When** its provenance is inspected, **Then** `UPSTREAM.txt` records the Apache-channel repo, commit `236d4a4054f0818f1931601713f7a6a68b275df7`, the file sha1, and the Extension Pack (EP303).
2. **Given** the vendored artifact, **When** the repository's attribution files are inspected, **Then** the Apache-2.0 §4 attribution for the Orchestra source is present.

---

### Edge Cases

- **Genuinely-unknown Orchestra datatype**: A datatype token with no fixpp equivalent must fail closed (thrown parse error), not silently drop or mis-map. EP303 has zero such tokens, so this is proven with a synthetic input, not a real one.
- **Orchestra union datatype** (`unionDataType`, e.g. `SettlType = SettlTypeCodeSet ∪ Tenor`): the minimal model keeps the codeset base type and **drops** the second (union) arm — deterministically, without error.
- **Codeset abstraction**: Orchestra names and shares codesets; the minimal model flattens each into per-field enumerated values (values + descriptions preserved), losing only the shared-codeset naming abstraction.
- **Scenarios**: EP303 declares zero `scenario=` variants; the reader has nothing to flatten. (Re-audit per future EP.)
- **A legacy (QuickFIX-XML) dictionary fed to the native reader, or an Orchestra file fed to the QuickFIX loader**: each reader targets its own format; cross-feeding is not a supported path and must fail closed rather than mis-parse.
- **Malformed / truncated Orchestra XML, missing required attributes, dangling component references**: fail closed with a thrown parse error (mirror the existing loader's fail-closed dispositions).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide a native Orchestra reader — a sibling to the existing `XmlLoader` — that parses an `OrchestraFIXLatest.xml` (`fixr:repository` schema: datatypes, codesets, components, groups, messages) **directly** into the *same* internal `Dictionary` the existing loader produces, so the validator, runtime `table_view`, and C-ABI read path are unchanged downstream (codegen source is likewise untouched, but does not consume a `vlatest` dictionary — typed FIX Latest codegen is a follow-on).
- **FR-002**: The reader MUST map Orchestra constructs to the internal model as follows: Orchestra datatype names → internal field datatype (reusing the existing `kFieldTypeTable` mapping logic); codesets → per-field enumerated values with values **and** descriptions preserved; `unionDataType` second arm → **dropped** (minimal model); Orchestra `<group>` and `<component>` (resolved transitively) → the internal group/component definitions.
- **FR-003**: Loading the vendored FIX Latest artifact MUST yield exactly **181 messages** with zero drops at any stage.
- **FR-004**: The reader MUST produce group-context tables that build without throwing or truncating and that resolve **queryably** for (a) the deepest FIX Latest group (the depth-7 `MassQuoteAck` chain, within the `K=16` context-depth cap) and (b) reused group tags disambiguated by parent-path context key (e.g. tag 555 reused under multiple parents).
- **FR-005**: The reader MUST establish a **real** FIX Latest version identity — a distinct internal `session_version::vlatest` value, derived from the Orchestra root `version="FIX.Latest_EP303"` — and MUST NOT relabel the artifact as `FIX.5.0SP2`. (This subsumes spike catalogue #0.) The **wire application version** for FIX Latest MUST be the existing `v50sp2` (ApplVerID = 9): FIX Latest has no distinct ApplVerID(1128) value, so this feature MUST NOT add an `application_version::vlatest` member and MUST NOT change `render_appl_ver_id`. FIX Latest's on-wire differentiator **ApplExtID(1156)=303 is out of scope for this feature and scheduled for a next phase** (see Non-Goals + Clarifications 2026-07-13).
- **FR-006**: The reader MUST fail closed (throw a dictionary parse error) on a genuinely-unknown Orchestra datatype rather than silently dropping or mis-mapping it. The set of unknown-vs-known tokens is bounded and enumerable; none occur in EP303.
- **FR-007**: The FIX Latest source artifact MUST be vendored and pinned by upstream commit and content hash, with Apache-2.0 §4 attribution and an `UPSTREAM.txt` recording the Extension Pack.
- **FR-008**: The change MUST be strictly **additive**: the nine vendored QuickFIX-XML dictionaries and the existing `XmlLoader`'s behavior are unchanged, and runtime-XML coverage of all nine legacy versions does not regress.
- **FR-009**: The reader MUST fail closed on inputs outside its format contract (malformed/truncated XML, missing required attributes, dangling references, or a wrong-format file), mirroring the existing loader's fail-closed dispositions — never a silent partial load.
- **FR-010**: Because `session_to_application(vlatest)` maps FIX Latest onto the **existing** `application_version::v50sp2` registry slot (idx 8; FIX Latest has no distinct ApplVerID — FR-005) of the 9-slot `application_version`-keyed `version_registry`, a `version_registry` that carries **both** a real FIX50SP2 dictionary and a FIX Latest dictionary MUST **fail loud** — it MUST NOT silently last-writer-wins overwrite one on the shared `v50sp2` slot (`version_registry.cpp:71-72`). This is an **interim guard** and MUST be **release-effective** (a fatal-in-ctor or an error-returning `build_version_registry`, **not** an NDEBUG-stripped `assert`; the ctor is currently `noexcept`, so the guard surfaces via abort/fatal or an error-returning builder, not a thrown exception — exact mechanism chosen at `/speckit-tasks`). Preferred mechanism (to be finalized at `/speckit-tasks`): the **contained fatal-in-ctor** variant (keeps the change additive within the dictionary/registry layer); an error-returning `build_version_registry` is the fallback only if a non-fatal path is required, and would extend into the engine-construction layer. Full ApplExtID(1156)=303-aware registry **re-keying** stays deferred to the follow-on phase (see Known Limitations + Non-Goals). Single-dictionary configurations (FIX Latest alone, or FIX50SP2 alone) are unaffected.

### Key Entities

- **Orchestra repository document** (`fixr:repository`): the official machine-readable FIX standard file. Sub-entities consumed by the reader: datatypes, codesets, fields, components, groups, messages.
- **Internal Dictionary**: fixpp's version-agnostic representation of a FIX dictionary (fields, datatypes, required-ness, group/component structure, group-context tables). The reader's sole output; the contract boundary with everything downstream.
- **FIX Latest version identity**: the new distinct `session_version::vlatest` value, derived from the Orchestra root `version="FIX.Latest_EP303"`. Its wire application version is the existing `v50sp2` (ApplVerID = 9); no distinct ApplVerID exists for FIX Latest.
- **Vendored provenance record**: the pinned artifact plus `UPSTREAM.txt` (repo, commit, sha, Extension Pack) and Apache-2.0 attribution.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The native reader loads the vendored `OrchestraFIXLatest.xml` and reports exactly **181 messages, zero drops** — matching the spike baseline.
- **SC-002**: **Zero** unknown-datatype failures occur on EP303 (every datatype token maps), and the fail-closed path is **proven RED** on a synthetic input carrying a genuinely-unknown Orchestra datatype.
- **SC-003**: The depth-7 `MassQuoteAck` group resolves via its **full parent path** `296→295→555→40241→41686→41680→41683` (asserted end-to-end, not merely a non-empty lookup) to the expected member set and correct first field; a reused group tag (e.g. 555) resolves non-empty under each distinct parent via the parent-path context key.
- **SC-004**: Every downstream **read-path** consuming surface (validator, runtime `table_view`, C-ABI read path) is **unchanged** — no API or behavior change is required to consume a natively-read FIX Latest dictionary. Codegen consumption of `OrchestraLoader` output is **out of scope** (`build_ir` throws on `vlatest`, `ir.cpp:265-270`) until the follow-on `fixpp::vlatest` namespace feature; the invariant here is only that **codegen tests do not regress** (they run over the nine dicts). No downstream source code changes.
- **SC-005**: An honestly-labelled FIX Latest dictionary loads under a **distinct** `session_version` (dictionary) identity (`vlatest ≠ v50sp2`) and does not collide with any of the nine legacy identities **at the `session_version` layer**; the `FIX.5.0SP2` relabel hack is not used anywhere. At the `application_version` **registry-slot** layer FIX Latest deliberately shares `v50sp2`/ApplVerID 9 (the intended consequence of the ApplVerID reconcile, not a dictionary-layer collision); the interim fail-loud guard against silent same-slot double-registration is **FR-010**, and full ApplExtID-aware re-keying is deferred (Known Limitations + Non-Goals).
- **SC-006**: All nine legacy QuickFIX-XML dictionaries continue to load with **unchanged** message counts and group-query results (no regression).
- **SC-007**: The vendored artifact is pinned (upstream commit + content hash recorded) and carries Apache-2.0 §4 attribution and an `UPSTREAM.txt` naming EP303.

## Assumptions

- **Read-path / dictionary scope only.** This feature is Orchestra XML → internal `Dictionary`. Typed `owning_<Message>` codegen for the 181 FIX Latest classes, live wire-message validation, and round-trip/field-accessor correctness are explicitly out of scope (see Non-Goals) — consistent with the spike's "Not exercised" section.
- **Minimal semantic-richness dial.** v1.0 reproduces today's flattened model: codeset values as per-field enums, `unionDataType` second arm dropped, scenarios N/A for EP303. The "richer" dial (codesets-as-typed-enums, scenario-aware typing) is a later, demand-driven step enabled by native ingestion — not part of this feature.
- **Version identity = `session_version::vlatest` only; wire app-version = `v50sp2`** *(reconciled — Clarifications 2026-07-13)*. This feature adds `session_version::vlatest` as the distinct dictionary/codegen identity. Because FIX Latest has no distinct ApplVerID(1128) wire value (the enum caps at 9 = FIX50SP2; Extension Packs use ApplExtID(1156)), the wire application version is the existing `v50sp2`/ApplVerID 9 — **no** new `application_version` member, **no** `render_appl_ver_id` change. FIX Latest's ApplExtID(1156)=303 wire differentiation **and** session-layer `DefaultApplVerID` negotiation are deferred; ApplExtID(1156)=303 is explicitly **scheduled for a next phase** (`REMAINING-WORK.md`).
- **pugixml is reused, not added.** pugixml (already vendored at 1.15, MIT; consumed only in the dictionary loader TU) is the parsing library for the new reader. No new third-party dependency. (Per the dependency-management rule, confirm the current pinned pugixml version at `/speckit-plan` time.)
- **Pinned artifact = EP303.** `OrchestraFIXLatest.xml` from the Apache channel (`FIXTradingCommunity/orchestrations`), commit `236d4a4054f0818f1931601713f7a6a68b275df7`, root `version="FIX.Latest_EP303"` (Orchestra v1.0, created 2026-06-03).
- **Legacy stays QuickFIX-sourced.** Official Orchestra files exist only for 4.2/4.4/Latest; the nine legacy dictionaries remain QuickFIX-sourced. Native Orchestra makes **FIX Latest** independent of QuickFIX; it does not free the legacy dicts.
- **Verification method (durable invariants).** Success is anchored on durable, reproducible invariants (message count 181, zero unknown datatypes, depth-7 + reused-tag group resolution, downstream surfaces unchanged, fail-closed proven RED). Any differential comparison against the spike's throwaway transposed dict is a plan-level *method* only — the transpose artifact lived in a disposable scratchpad worktree and may no longer exist; it is **not** a success criterion.

## Normative References

*(Article VI §5. The registered `coverage-index.md` DocAbbrev **`FIX-Latest`** (FIX Latest living online standard) and the existing catalogue anchors — the **D-011** row ("FIX Orchestra / Rules of Engagement machine-readable format", Post-1.0 Gap Registry) and the **A-035..A-065** FIX Latest MsgType rows — are the real anchors this feature builds on and are cited below. The FIX Orchestra `fixr:repository` machine-readable **schema** has no registered `[DocAbbrev §X.Y.Z]` section slugs (it is a schema, not a §-numbered prose document), so no such slugs are manufactured here. **Pre-implement (before-land) obligation, per Article VI §4:** register an Orchestra-schema DocAbbrev in the `coverage-index.md` registry and add the bidirectional `coverage-index.md` entries promoting D-011 (from Post-1.0 Gap → in-scope for FIX Latest read-path) and linking the A-035..A-065 rows to this feature. Tracked as a `/speckit-analyze` (pipeline step 6) reconciliation item; the plan's Constitution-Check Article VI row is marked **CONDITIONAL** accordingly — not an unbacked PASS.)*

- **FIX Orchestra v1.0** — machine-readable standard schema (`fixr:repository`: datatypes, codesets, fields, components, groups, messages). The parse contract for the reader.
- **FIX Latest, Extension Pack 303 (EP303)** — the message/field/group content set (`OrchestraFIXLatest.xml`, Apache channel `FIXTradingCommunity/orchestrations` @ `236d4a4054f0818f1931601713f7a6a68b275df7`, root `version="FIX.Latest_EP303"`, Orchestra v1.0, 2026-06-03).
- **FIXT.1.1 / FIX 5.0 SP2 application layer** — FIX Latest's wire base (ApplVerID = 9); FIX Latest extends it via backward-compatible Extension Packs signalled by **ApplExtID(1156)** (out of scope here — scheduled follow-on).
- **Apache License 2.0 §4** — attribution obligation for the vendored Orchestra source.

## Dependencies

- **Constitution amendment (process dependency).** v1.0 scope is currently locked to FIX 4.0–5.0SP2 + FIXT.1.1 (`[const §I.1]`, `[const §XVIII.1]`). Adding FIX Latest as a supported (runtime/dictionary-tier) version requires a constitution amendment, expected to be folded at Gate A per the row-4b v1.0-gating promotion (precedent: 035/043/068/069 Gate-A-folded amendments).
- **Mandatory-trigger controls (Appendix A).** This feature touches the **dictionary loader / multi-version coexistence** (Appendix A "Codegen layout" trigger) and version/wire identity. Therefore all four mandatory controls apply: `/clarify`, `/analyze`, Codex Gate A, and user `/plan` sign-off (`[const §XVII]`, `[const §XVIII.3]`, Appendix A).
- **Existing internal model.** Reuses the internal `Dictionary`/`VersionIR`, the `table_view` group-context machinery (`kMaxGroupContextDepth = 16`), and the `kFieldTypeTable` datatype-mapping logic — none of which this feature changes in shape.
- **Vendored source.** Requires the Apache-channel `OrchestraFIXLatest.xml` vendored into the repository (FR-007).

## Non-Goals *(explicit — bounds the read-path scope)*

- **Typed `owning_<Message>` codegen for the 181 FIX Latest classes.** A separate follow-on feature, gated on the still-open "per-version typed generation must be a build OPTION" decision (compile-time / binary-size / sanitizer-matrix cost). Not this feature.
- **Live FIX Latest wire-message validation.** Feeding live FIX Latest messages through the `dictionary_driven_validator` needs message fixtures and belongs to the FIX-Latest runtime feature's test surface. This feature only proves the validator's *input* (the `table_view`) builds and is queryable.
- **Round-trip / field-accessor correctness on FIX Latest content.** Not attempted here.
- **ApplExtID(1156)=303 wire differentiation — scheduled for a next phase.** FIX Latest's real on-wire version differentiator is ApplExtID(1156)=303 (not a new ApplVerID). Modelling/emitting/validating it is out of scope here and **explicitly scheduled** as a follow-on round (`REMAINING-WORK.md`).
- **Session-layer `DefaultApplVerID` negotiation.** The `session_version::vlatest` dictionary identity is defined here; wiring version negotiation into the FIXT session FSM is deferred to the session/runtime surface.
- **Richer semantic modeling** (codesets-as-typed-enums, scenario-aware typing, Orchestra workflow/actors/rules). Minimal flattened model only.
- **Regenerating or replacing the nine legacy QuickFIX dictionaries.** Legacy stays QuickFIX-sourced (strictly additive per FR-008).
- **The QuickFIX-dict license README correction / top-level `NOTICE`.** Tracked separately (`REMAINING-WORK.md` row 15d; README already corrected on submodule branch `docs/dict-license-fix`, commit `2c0b4947`). Not carried drive-by in this feature.

## Known Limitations *(operator-facing — behaviors-and-limitations style)*

- **L-074-1 (interim `v50sp2` registry-slot coexistence).** FIX Latest and real FIX 5.0SP2 share the single `application_version::v50sp2` `version_registry` slot (`session_to_application(vlatest)→v50sp2`; the enum is not re-keyed in this feature — deferred per the spike RECONCILE, `orchestra-fix-latest-spike-and-plan.md` L145-146). Until the ApplExtID(1156)=303-aware registry re-keying lands (follow-on, `REMAINING-WORK.md` row 4b), an `EngineConfig` MUST NOT carry **both** a FIX50SP2 and a FIX Latest dictionary in one `version_registry`: FR-010's fail-loud guard rejects that combination rather than silently dropping one (the QuickFIX-style last-writer-wins documented at `version_registry.cpp:71-72`). Single-dictionary use (FIX Latest alone, or FIX50SP2 alone) is unaffected. To be promoted to a `behaviors-and-limitations.md` L-row at land.
