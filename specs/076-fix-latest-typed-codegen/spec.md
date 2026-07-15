# Feature Specification: FIX Latest Typed Message Classes via Native Orchestra Codegen (`fixpp::vlatest` tier)

**Feature Branch**: `076-fix-latest-typed-codegen`

**Created**: 2026-07-15

**Status**: Draft

**Input**: User description: "FIX Latest typed message classes via native Orchestra codegen (fixpp::vlatest tier) — generate the typed build/validate/args/readback layer for the 181 FIX Latest (EP303) messages into a distinct fixpp::vlatest namespace via the existing codegen emitter, fed by 074's native OrchestraLoader dictionary. Behind a build option. Non-circular completeness census mandatory (no QuickFIX peer). Application-version dispatch reachability (ApplExtID re-keying) explicitly deferred."

---

## Context (why this feature exists)

FIX Latest support has been delivered bottom-up, one tier per feature:

- **074-orchestra-native-reader** (MERGED, PR #192) delivered the **read/dictionary tier**: a native `dict::OrchestraLoader` parses the pinned `dictionaries/orchestra/OrchestraFIXLatest.xml` (Extension Pack 303, **181 messages**) into a runtime `Dictionary` under a new, distinct `session_version::vlatest`. Wire application-version maps to the existing `v50sp2` (ApplVerID 9); **no** `application_version::vlatest` member exists and `render_appl_ver_id` is untouched (the wire-ApplVerID map stays injective).
- **075-live-wire-enum-validation** (MERGED, PR #193) made runtime enum-domain checking real across all ten dictionaries, including the FIX Latest codeset store.

What is still missing is the **typed ergonomic tier**: today a caller can load and runtime-validate FIX Latest messages, but cannot construct/validate/read them through the same typed `build_<Msg>` / `validate_<Msg>` / typed-args / readback API that every legacy version already has (delivered for v44 by 067/069). This feature adds that tier for FIX Latest.

### Source-verified facts (emitter code-read, this session — grade-1)

1. **The emitter partitions on `session_version`, so a `fixpp::vlatest` namespace is collision-free with `v50sp2`.** `build_ir` reads `dict.which_session_version()` (`tools/codegen/fixpp-codegen/ir.cpp:254`); the namespace tag `ir.ns` is chosen by matching that **session** version against `kCodegenVersions` (`ir.cpp:258`, deciding line `if (vm.s == ir.session)`; `ir.ns = vm.ns` at `:260`). Every downstream emitter keys on `ir.ns` (`emit_messages.cpp:440/453/469`, `emit_validator.cpp:37-39/84-86`, output dir `main.cpp:86`), never on `application_version`. Because 074 kept `session_version::vlatest` distinct, FIX Latest classes get their own namespace with no v50sp2 collision.

2. **Today the emitter *rejects* vlatest — it does not collapse it.** `kCodegenVersions` (`ir.cpp:212-227`) has rows for v42/v44/v50sp2/vt11 only; there is **no `vlatest` row**. `build_ir` over a FIX Latest dictionary therefore matches nothing (`mapped=false`) and **throws** at `ir.cpp:265-270`. There is no silent-collapse path to guard against in the typed-class layer.

3. **The one place the vlatest↔v50sp2 tension actually bites is application dispatch — and it is out of scope here.** `emit_dispatch_application` (`emit_dispatch.cpp:174`) builds its outer switch by iterating its own static `kAppVersions` table (v42/v44/v50sp2 only, `:62-64`), matching a `VersionIR` by `ns` (`:224-232`). As-is, a vlatest `VersionIR` is **silently excluded** from `dispatch_application` (its classes generate but are unreachable via app-dispatch; incoming messages fall to the fail-loud default). Making vlatest reachable would require a `kAppVersions` row keyed on `application_version::v50sp2` — producing a **duplicate `case application_version::v50sp2`** and breaking the injective wire-ApplVerID map 074 deliberately preserved. That re-keying is the deferred **ApplExtID(1156)=303** feature, not this one.

4. **The codegen driver hardcodes `XmlLoader`.** `build_ir` instantiates `fixpp::dict::XmlLoader` directly (`ir.cpp:250`) and cannot parse the Orchestra schema. The real added work of this feature is teaching the codegen path to load the FIX Latest dictionary via **074's `OrchestraLoader`**. The per-version driver is invoked once per dictionary (`fixpp-codegen --xml <FIXxx.xml> --out <dir>`, `main.cpp:44-105`), wired at CMake configure time in `cmake/Codegen.cmake` (currently four `--xml` lines: FIX42/FIX44/FIX50SP2/FIXT11).

5. **`app_version_enum` derives the per-class `version_v` constant from the namespace string.** `gen_util.hpp:248-253` maps `ns` verbatim to `application_version::<ns>`, with `vt11`→`Unknown` as the existing special-case. An unguarded `ns="vlatest"` would emit `application_version::vlatest`, which **does not exist** (the enum caps at `v50sp2`; 074 shipped no such member) — a compile error in generated code. A special-case is required, mirroring the `vt11` precedent.

### Why now / scope authority

Per the **v1.0 SCOPE DECISION (2026-07-15)**, v1.0 ships the full OFFICIAL tag-value FIX tail, including the FIX Latest typed classes (catalogue rows A-035..A-065; retires the typed-tier half of D-011). This feature is the typed-tier successor to 074 (reader) and 075 (enum validation).

---

## Clarifications

### Session 2026-07-15

- Q: How should the 181-class FIX Latest codegen be gated and covered in CI? → A: Build option **defaults ON** (release ships FIX Latest automatically; consumers opt out); the `vlatest` tier runs the **full sanitizer/preset matrix** identical to legacy tiers; CI **also builds the OFF path** to keep the toggle honest. `/plan` must measure the actual compile-time/binary-size delta and may revisit the default only if the cost is surprising.
- Q: How strong should the non-circular completeness census (FR-006) be, given FIX Latest has no QuickFIX peer? → A: **Strongest** — assert exact-set equality of the 181 messages AND per-message **full field-set equality including nested group-member fields at all depths**, enumerated directly from the raw `OrchestraFIXLatest.xml`.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Construct, validate, and serialize a FIX Latest message through the typed API (Priority: P1)

A developer targeting FIX Latest builds a FIX Latest business message (e.g. a message unique to EP303, or a 5.0SP2-shared message) using the generated typed builder in `fixpp::vlatest`, validates it, serializes it to the wire, and reads it back — exactly as they would for FIX 4.4 via `fixpp::v44`. The FIX Latest typed surface reaches parity with the legacy typed surface.

**Why this priority**: This is the feature's reason to exist — the typed ergonomic layer for FIX Latest. Without it, FIX Latest is loadable but not typed-constructible. It is the MVP: delivering only this story (even for a subset of messages) already provides standalone value.

**Independent Test**: Generate the `fixpp::vlatest` tier, then in a test construct a FIX Latest message via its typed builder, call its `validate_<Msg>`, serialize, and read the fields back — asserting round-trip fidelity — without any application-dispatch or session wiring.

**Acceptance Scenarios**:

1. **Given** the FIX Latest codegen tier is generated, **When** a developer constructs a FIX Latest message via its typed builder in `fixpp::vlatest` and serializes it, **Then** the produced wire bytes parse and read back field-for-field identically.
2. **Given** a typed FIX Latest message with an out-of-domain enum or a missing required field, **When** its `validate_<Msg>` runs, **Then** validation fails with the same diagnostics a legacy typed message would produce.
3. **Given** the FIX Latest codegen tier is generated, **When** the existing v42/v44/v50sp2/vt11 typed tiers are built, **Then** their generated output is byte-identical to before this feature (additive-only, no regression).

---

### User Story 2 - Provable completeness of the 181-message set (Priority: P1)

A maintainer needs assurance that **every** FIX Latest message (and its fields) has a generated typed class — not a silently-truncated subset — despite there being no QuickFIX reference engine to cross-check against.

**Why this priority**: FIX Latest has no QuickFIX peer, so the usual differential-vs-reference bar does not apply. A differential-vs-baseline check alone is **circular** (the typed emitter and any runtime-XML baseline both consume the same `vlatest` Dictionary — a shared Orchestra-read bug is green on both). Completeness must be proven against an **independent** source or the coverage claim is blind. This is co-P1 with Story 1: shipping typed classes whose completeness is unproven would enshrine a coverage gap.

**Independent Test**: Run a census that enumerates messages and fields directly from the raw `OrchestraFIXLatest.xml` (not via the loader/Dictionary the emitter uses) and assert EXACT-SET equality against the emitted typed-class set — failing on any message or field present in one set but not the other.

**Acceptance Scenarios**:

1. **Given** the raw `OrchestraFIXLatest.xml` and the emitted `fixpp::vlatest` class set, **When** the completeness census runs, **Then** the two message sets are exactly equal (181 == 181, no subset pass) and per-message field sets match.
2. **Given** a hypothetical dropped or extra message, **When** the census runs, **Then** it fails loudly (the census is proven to discriminate, not merely observe).

---

### User Story 3 - Opt-in build cost (Priority: P2)

A downstream consumer who does not need FIX Latest can build fixpp without paying the compile-time, binary-size, and sanitizer-matrix cost of 181 additional generated message classes; a consumer who needs FIX Latest turns one build option on.

**Why this priority**: Generating 181 typed classes is a material build cost (the spike explicitly deferred quantifying it). Making generation opt-out-able protects consumers who only use legacy versions, following the `FIXPP_CODEGEN_V44_FAMILIES` precedent. It is P2 because the capability (Stories 1–2) is deliverable regardless of the gating default; gating is a cost-control refinement.

**Independent Test**: Build with the FIX Latest codegen option OFF and confirm no `fixpp::vlatest` symbols are compiled and the build is byte-identical to today for the existing tiers; build with it ON and confirm the vlatest tier appears additively.

**Acceptance Scenarios**:

1. **Given** the FIX Latest codegen build option OFF, **When** the project builds, **Then** no `fixpp::vlatest` code is generated or compiled and the existing tiers' output is unchanged.
2. **Given** the build option ON, **When** the project builds, **Then** the `fixpp::vlatest` tier is generated additively with no change to existing tiers.

### Edge Cases

- **Unknown Orchestra datatype at codegen time**: if a future EP introduces a datatype the codegen mapping does not know, generation MUST fail closed (thrown/enumerated error), never silently drop or mis-type a field. (EP303 introduces none — bounded and verified by 074's datatype catalogue.)
- **Codeset / union-arm fields**: FIX Latest codesets are ingested minimally-flattened (codeset values as field enums, `unionDataType` second arm dropped) exactly as 074 already loads them; the typed layer reflects that same model, not a richer one.
- **A message shared between FIX Latest and 5.0SP2**: it is generated independently into `fixpp::vlatest` (own namespace) — it does not reuse or collide with the `fixpp::v50sp2` class.
- **An incoming FIX Latest wire message routed by application-version dispatch**: NOT handled by this feature — it falls to the existing fail-loud dispatch default (reachability is the deferred ApplExtID feature). This is an intentional, documented boundary, not a regression.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The codegen MUST emit the typed message layer — typed builder (`build_<Msg>`), `validate_<Msg>`, typed args, and readback — for **all 181** FIX Latest (EP303) messages into a distinct `fixpp::vlatest` namespace, collision-free with `fixpp::v50sp2` and every other existing tier.
- **FR-002**: The codegen MUST source the FIX Latest dictionary via 074's native `OrchestraLoader` (parsing the pinned `OrchestraFIXLatest.xml`), NOT `XmlLoader`.
- **FR-003**: FIX Latest typed-class generation MUST be gated behind a dedicated build option that **defaults ON**. When the option is OFF, no `fixpp::vlatest` code is generated or compiled. CI MUST exercise **both** the ON path (through the full sanitizer/preset matrix, identical coverage to the legacy tiers) and the OFF path (to prove the toggle stays honest and the OFF build remains byte-identical to today's legacy-only output).
- **FR-004**: Generation MUST be strictly additive: with the option ON or OFF, the generated output for the existing v42/v44/v50sp2/vt11 tiers MUST be byte-identical to `main` (no legacy regression).
- **FR-005**: The generated per-class version-identity constant (`version_v`) for the vlatest tier MUST resolve without referencing a non-existent `application_version::vlatest` (special-cased, mirroring the `vt11`→`Unknown` precedent).
- **FR-006**: Closure verification MUST include a **non-circular completeness census** that enumerates messages and fields directly from the raw `OrchestraFIXLatest.xml` and asserts **EXACT-SET equality** (not subset-presence) against the emitted `fixpp::vlatest` class set, at two levels: (a) the 181-message set, and (b) per-message **full field-set equality including nested group-member fields at all depths**. This census is mandatory precisely because FIX Latest has no QuickFIX golden — a subset or message-level-only check would let a dropped/added field pass silently.
- **FR-007**: Closure verification MUST include differential round-trip of all 181 messages through the typed API vs the FIX Latest baseline (the `vlatest` runtime path), asserting field-level round-trip fidelity with zero skips.
- **FR-008**: This feature MUST introduce zero C-ABI, Python, or link-ABI surface change (the C-ABI is GA-frozen at 1.5.0). It is additive codegen + build-wiring + tests only.
- **FR-009**: The vlatest tier MUST NOT be wired into **either** application-version dispatch surface — neither `dispatch_application` (validator dispatch) **nor** the runtime auto-dispatch `reify_dispatch_application` path (typing a raw inbound wire message). Both key on `application_version`, which collides `vlatest` onto the `v50sp2` slot; wiring either would break the injective wire-ApplVerID map and mis-route the ~25 FIX-Latest-only messages to the 156-message v50sp2 dictionary. Generated classes are reachable via the **direct typed API with a caller-supplied FIX Latest dictionary** only; raw-inbound auto-dispatch into vlatest types is out of scope (deferred ApplExtID / `version_registry` re-keying feature). The wire-ApplVerID map MUST remain injective.
- **FR-010**: Codegen over the FIX Latest dictionary MUST fail closed on any genuinely-unknown Orchestra datatype (bounded and enumerated; none present in EP303), consistent with the existing datatype gate.
- **FR-011**: Generated `fixpp::vlatest` output MUST be deterministic and reproducible; the checked-in codegen golden / determinism guard MUST be updated to cover the vlatest tier so a stale or non-deterministic emit fails CI.

### Key Entities *(include if feature involves data)*

- **FIX Latest dictionary (`vlatest`)**: the runtime `Dictionary` 074 builds from `OrchestraFIXLatest.xml` (EP303, 181 messages), keyed by `session_version::vlatest`. Input to codegen.
- **`fixpp::vlatest` typed message class**: the generated per-message artifact — typed builder, validator, typed args, readback — one per FIX Latest message.
- **Completeness census source**: the raw `OrchestraFIXLatest.xml`, read independently of the loader/Dictionary, providing the ground-truth message+field set for the exact-set equality check.
- **FIX Latest codegen build option**: the CMake switch gating whether the vlatest tier is generated/compiled.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 181 of 181 FIX Latest messages have a generated typed class with builder + validator + args + readback; the non-circular exact-set census passes (message set equality 181==181, and per-message full field-set equality including nested group-member fields at all depths), and fails if any message OR field is dropped or added.
- **SC-002**: 181 of 181 FIX Latest messages round-trip losslessly (construct → serialize → read back, field-for-field) through the typed API, with zero skips, vs the FIX Latest baseline.
- **SC-003**: With the FIX Latest codegen option OFF, the build produces no `fixpp::vlatest` symbols and the existing v42/v44/v50sp2/vt11 generated tiers are byte-identical to `main`; with it ON, those four tiers remain byte-identical and the vlatest tier is present.
- **SC-004**: Zero change to the C-ABI symbol golden, the Python surface, and the link-ABI (verified by the existing ABI-golden gate).
- **SC-005**: No FIX Latest message is reachable via `dispatch_application` (the injective wire-ApplVerID map is preserved); attempting an application-version dispatch for a FIX Latest message hits the existing fail-loud default rather than a duplicate/ambiguous case.

## Out of Scope

- **ApplExtID(1156)=303 on-wire differentiation, `version_registry` re-keying, `dispatch_application` reachability for FIX Latest, and `DefaultApplVerID` negotiation** — these constitute the separate deferred ApplExtID feature (074's L-074-1). This feature deliberately stops at typed-class generation reachable via the direct API.
- **Richer semantic typing of Orchestra codesets** (codesets-as-typed-enums, scenario-aware typing). This feature reproduces 074's minimal-flattened model (codeset values as field enums, union arm dropped); a richer `VersionIR` is a later, demand-driven step.
- **Regenerating the legacy dictionaries from Orchestra.** The nine QuickFIX-sourced dicts stay as-is; only FIX Latest is Orchestra-native. No legacy read/write coverage regresses.
- **New wire encodings / session protocols** (FIXP, SOFH, SBE, etc.) — post-v1.0.

## Assumptions

- **Build-option default = ON (decided, Clarifications 2026-07-15).** The FIX Latest codegen option defaults **ON** so the v1.0 release ships FIX Latest without a manual flip, following the `FIXPP_CODEGEN_V44_FAMILIES=all` precedent, with CI covering **both** the ON and OFF paths so neither bit-rots. `/plan` MUST measure the actual compile-time/binary-size/sanitizer-matrix delta (deferred by the 074 spike); the ON default holds unless that measurement is surprising, in which case it is re-raised with the user.
- **Vehicle = the existing 067/069 codegen emitter**, extended by threading the `OrchestraLoader` load into `build_ir` + a `kCodegenVersions` vlatest row + the `app_version_enum` special-case — NOT hand-authoring the 181 classes.
- **The FIX Latest dictionary is the pinned EP303 `OrchestraFIXLatest.xml`** (181 messages) that 074 already vendored and verifies; this feature does not re-pin or bump the Extension Pack.
- **Semantic richness = minimal-flattened**, identical to what `OrchestraLoader` ingests today (074).
- **Governance**: any constitution touch (e.g. confirming the typed-codegen scope amendment already applied via 069's Article XVIII §7 / 074–075's Article I §1) is handled at Gate A per the Article XX single-source process — no standalone `/speckit-constitution` run. To be assessed at `/plan`.

## Dependencies

- **074-orchestra-native-reader** (MERGED) — provides `OrchestraLoader`, `session_version::vlatest`, and the vlatest `Dictionary` this feature feeds into codegen.
- **067-codegen-writer-emitter / 069-v44-all-families** (MERGED) — provide the `build_<Msg>`/`validate_<Msg>`/args/readback emitter and the `CoverageMode`/build-option pattern this feature extends.
- **075-live-wire-enum-validation** (MERGED) — provides the runtime enum-domain check the typed validators rely on for FIX Latest.
