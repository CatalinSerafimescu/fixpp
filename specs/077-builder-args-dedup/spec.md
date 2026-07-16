# Feature Specification: Typed builder tier for all FIX versions via group-Args deduplication

**Feature Branch**: `077-builder-args-dedup`

**Created**: 2026-07-16

**Status**: Draft

**Input**: User description: "Typed builder tier for FIX Latest via version-wide group-Args deduplication — redesign emit_builders to emit each repeating group's Args struct once per version (keyed by no_tag), apply uniformly across ALL versions, regenerate the v44 builder golden, re-enable the vlatest builder tier descoped by 076. Scope extended (user, 2026-07-16) to emit typed builders for ALL typed versions, not just v44 + vlatest."

## Overview

The typed **builder** tier (`build_<Msg>` / `validate_<Msg>` over `wire::body_builder`, delivered for `fixpp::v44` by features 067/069) generates each nested repeating-group input struct (`<Msg>...Args`) **once per message, per structural path**, with message-rooted names (e.g. `ExecutionReportPartyIDsPartySubIDsArgs`). On FIX44's shallow groups this is harmless (83 messages → ~1,624 structs / 3.8 MB). On FIX Latest's depth-7 reused components (Instrument / Underlying / Leg / Parties re-nested across 173 application messages) it explodes combinatorially into a **137 MB / 53,590-struct `vlatest/Builders.hpp`** that no consumer translation unit can compile (measured >21 GB RSS). Feature 076 was forced to descope the FIX Latest builder tier for exactly this reason (limitation **L-076-1**), and no version other than v44 has ever had a builder tier (v42/v50sp2 app-message builder widening is v1.x-deferred per Article XVIII §7).

The **read** tier already solved this identical problem: `emit_messages.cpp` emits each distinct repeating group **exactly once** per version as a `G_<no_tag>` flyweight in a shared `fixpp::<ns>::groups` namespace (v44 59, v50sp2 505, vlatest 524 distinct read-tier group flyweights — read-tier counts span all messages incl. admin; the app-only builder census reports v44 58, reconciled in research.md R2), and the resulting single-file `Messages.hpp` compiles (vlatest's is ~9.7 MB / 140 k lines). This feature applies the same **shared-namespace deduplication** to the builder emitter, then extends the deduplicated builder tier to **every typed version that carries application messages** (v42, v44, v50sp2, vlatest).

**Deduplication key — refined at `/plan` (structural identity, not `no_tag` alone).** The read tier dedups by taking the *union of members keyed by `no_tag`*: reading is order-independent and required-ness-agnostic, so one superset `G_<no_tag>` flyweight serves every message. A **builder** cannot union — it is serialization-**order**-sensitive (declaration order) and **required-ness**-sensitive (`validate_<Msg>`). A `/plan` IR census (all four versions, keyed on ordered members + each member's `FieldRef.rule` + nested children) found that a single `no_tag` routinely resolves to **2–8 genuinely different structural plans** within one version (`NoOrders`/73, `NoLegs`/555, `NoRelatedSym`/146, …). So the builder dedups by the key **`(no_tag, recursive structural signature)`** — the recursive signature (delimiter + ordered members + required-ness + child signatures) discriminates variants within a count tag, and pairing it with the group's own `no_tag` makes the naming contract well-defined and keeps two distinct count tags from ever collapsing into one plan. Emitted once into `fixpp::<ns>::groups`, named `G_<no_tag>Args` when a `no_tag` maps to exactly one signature; when a `no_tag` has two or more signatures, no bare name is emitted and ALL variants are ordinaled `G_<no_tag>_1Args` … `G_<no_tag>_kArgs` (deterministic first-encounter ordinal). This collapses vlatest from 26,806 per-path structs to **578** distinct `(no_tag, signature)` pairs (v50sp2 25,927 → 558) — the read tier's order of magnitude, restoring single-file compilability.

## Clarifications

### Session 2026-07-16

- Q: Should the builder tier cover admin/session messages, or stay application-message-only? → A: **Application-message-only.** vt11 (FIXT, admin-only) emits no builders; admin/session frames (Logon/Logout/Heartbeat/Reject/…) stay engine-internal — no typed `build_<Msg>` for them.
- Q: What happens to the v44 `official` (frozen 33) families mode under the deduplicated regeneration? → A: **Keep both.** Retain the `all` and `official` v44 modes and the `FIXPP_CODEGEN_V44_FAMILIES` knob; regenerate BOTH v44 builder goldens to the deduplicated output.
- Q: Which application-message set should each new builder-bearing version (v42, v50sp2) emit builders for? → A: **Reconsider per version.** Do NOT blindly inherit v44's `{BE,BF,BW,BX,BY}` exclusion; determine each version's genuine application-builder set at `/plan` (may include BW/BX/BY — UserRequest/UserResponse/UserNotification — on FIX 5.0 SP2 where they are real application messages).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Construct FIX Latest messages with compilable typed builders (Priority: P1)

A developer building on FIX Latest (EP303, `fixpp::vlatest`) wants to construct any of the FIX Latest application messages on the wire using the same typed, validated `build_<Msg>` / `validate_<Msg>` surface that `fixpp::v44` already offers, and have the generated builder header compile as an ordinary translation unit within normal build resources.

**Why this priority**: This is the headline value — it unblocks the tier descoped by 076 (L-076-1) and simultaneously proves the deduplication on the worst case (FIX Latest's deep reused components are what made the naive emitter uncompilable). Delivering just this story yields a usable, compilable FIX Latest builder tier — a viable MVP.

**Independent Test**: With `FIXPP_CODEGEN_FIX_LATEST=ON`, generate the `vlatest` tier, compile a translation unit that includes `vlatest/Builders.hpp`, and call `build_<Msg>` for a representative deep-group message (one carrying Instrument + Legs + Underlyings, e.g. a multi-leg order); read the produced frame back field-for-field and confirm equality. Confirm the header compiles within ordinary developer build limits (peak RSS in the low single-digit GB, not tens of GB).

**Acceptance Scenarios**:

1. **Given** `FIXPP_CODEGEN_FIX_LATEST=ON`, **When** codegen runs, **Then** `vlatest/Builders.hpp` is emitted and each distinct repeating group's input struct appears exactly once (no message-path duplication).
2. **Given** a translation unit including `vlatest/Builders.hpp`, **When** it is compiled, **Then** it compiles successfully within normal build resource limits (no >21 GB RSS, no OOM).
3. **Given** a populated `<Msg>Args` for any FIX Latest application message, **When** `build_<Msg>` is called, **Then** the serialized frame round-trips (build → read-back) field-for-field with the same values.
4. **Given** an `<Msg>Args` missing a required field, **When** `validate_<Msg>` is called, **Then** it reports the missing required field (parity with the v44 builder-validate contract).

---

### User Story 2 - Typed builders for all application-bearing FIX versions (Priority: P2)

A developer targeting FIX 4.2 or FIX 5.0 SP2 wants the same typed `build_<Msg>` surface that v44 and vlatest offer, so message construction is symmetric across every version that has a typed namespace and application messages.

**Why this priority**: Realizes the "all FIX versions" goal (user decision, 2026-07-16). It depends on US1's deduplicated emitter (v50sp2's 505 distinct groups would otherwise explode much like vlatest's did), so it is sequenced after the mechanism is proven, but it is the primary reason the scope is version-wide rather than vlatest-only.

**Independent Test**: For each of v42, v50sp2, generate the tier, compile a translation unit including that version's `Builders.hpp`, and round-trip a representative application message (one with at least one repeating group) through `build_<Msg>` then read-back.

**Acceptance Scenarios**:

1. **Given** any typed version that has application messages (v42, v44, v50sp2, vlatest), **When** codegen runs, **Then** that version emits a `Builders.hpp` covering its in-scope application-message set.
2. **Given** vt11 (FIXT session layer, admin-only), **When** codegen runs, **Then** no application-message builders are emitted for it (consistent with the existing application-message scope rule) and this is not an error.
3. **Given** each per-version builder header, **When** compiled independently, **Then** each compiles within normal build resource limits.

---

### User Story 3 - v44 builder behavior is unchanged after uniform deduplication (Priority: P2)

A maintainer needs assurance that switching the v44 builder tier from per-message to deduplicated emission changes only the generated struct identities/layout — never the serialization bytes or validation outcomes of `build_<Msg>` / `validate_<Msg>` — and that the checked-in golden is regenerated once and stays deterministic.

**Why this priority**: The uniform-mechanism decision (Option B) deliberately changes v44's shipped builder output, superseding 076's byte-identical-builder guarantee for v44. This story is the regression guard that keeps the *behavior* stable even though the *text* changes.

**Independent Test**: Drive each v44 `build_<Msg>` and compare its bytes against a **frozen pre-077 external byte corpus** (QuickFIX-authored `.fix` goldens + the 061 hand exemplars — fixtures NOT regenerated from the emitter, so they survive the nested-Args rename). `tests/session/test_067_builder_shape_oracle.cpp` already does this for **5 of 83** messages (D/8/9/E/AS); this feature **extends the frozen-byte differential to every distinct structural plan / every multi-plan `no_tag`** (`NoLegs`/555 = 8 plans, `NoOrders`/73, `NoRelatedSym`/146 — where a mis-share is most likely), or at minimum all 83 in-scope messages. Round-trip (build → read-back) alone is **NOT** a differential — reads scan by tag, so a build that silently reordered or dropped a member of a collapsed variant round-trips clean both ways. Then run the 067/069 validation suite against the regenerated golden and the determinism check (generate twice, byte-compare).

**Acceptance Scenarios**:

1. **Given** the deduplicated emitter, **When** the v44 builder tier is generated, **Then** every one of the 83 in-scope v44 application messages still produces a `build_<Msg>` and `validate_<Msg>` with the same serialized output and validation results as before dedup.
2. **Given** the regenerated v44 builder golden, **When** codegen runs twice in separate directories, **Then** the two outputs are byte-identical (determinism preserved).
3. **Given** the whole feature, **When** the legacy typed **read** tiers (Messages/Fields/Validator/Reify for v42/v44/v50sp2/vt11) are generated, **Then** they are byte-identical to pre-feature output (this feature touches only the builder emitter).

---

### User Story 4 - Per-version builder completeness is provable (Priority: P3)

A reviewer needs a non-circular guarantee that each version's builder tier covers exactly its in-scope application-message set — re-instating the V-2 / V-2b builder-completeness verification legs that 076 descoped for FIX Latest, and extending them to every builder-bearing version.

**Why this priority**: Completeness is a correctness property, but it rides on US1/US2 existing first. It closes the descoped verification obligation.

**Independent Test**: For each builder-bearing version, run a completeness census that asserts the set of emitted `build_<Msg>` entry points equals the version's in-scope application-message set derived independently (not from the emitter's own walk).

**Acceptance Scenarios**:

1. **Given** any builder-bearing version, **When** the builder-completeness census runs, **Then** the emitted builder set exactly equals the independently derived in-scope application-message set (exact-set equality, not subset).
2. **Given** a deliberately dropped message in the emitter, **When** the census runs, **Then** it fails (the gate is proven able to go red).

---

### Edge Cases

- **Same group under multiple parents / at multiple depths within one message** → the builder must reference the single shared per-group Args struct, never a per-path copy.
- **A group shared across two messages** → exactly one shared Args struct and exactly one set of validation metadata (`writer_traits` specialization + required/count/entry-validation helpers); no duplicate or ODR-conflicting definitions.
- **Cyclic or over-deep component reuse** → bounded like the read tier's depth cap; a bounded-out edge must never leave a referenced-but-undefined Args type.
- **A version with no application messages (vt11)** → no builder output; not an error.
- **A message (or hypothetical version) with zero repeating groups** → builders still emit correctly (flat scalar `Args`, no `groups::G_…` reference); the dedup is a no-op **for that message**. NB: no in-scope version is group-free — the `/plan` census (research.md R2/R3) shows even the shallowest, FIX 4.2, has 18 distinct `no_tag`s (7 multi-plan, a 38→29 / 1.3× dedup); this is therefore a per-message degenerate case, NOT a per-version one (the earlier "e.g. FIX 4.2 shallow set / no-op" framing conflated the v42 *read-tier* zero-flyweight metric with the builder's app-group census and is corrected here).
- **Length+Data coupled fields, required-ness folding, framing/header-trailer exclusion** → preserved exactly as the current emitter handles them; dedup changes only where a group's Args is *defined*, not what it contains.
- **`FIXPP_CODEGEN_FIX_LATEST=OFF`** → no vlatest builders (and no stale `vlatest/Builders.hpp` left behind); other versions' builders unaffected.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The builder emitter MUST emit each distinct repeating-group input Args structure **exactly once per version**, keyed by **`(no_tag, full recursive structural signature)`** — the recursive signature (ordered members + each member's required-ness + nested children) is the variant discriminator within a `no_tag`, NOT the whole key, because the `/plan` census proved one `no_tag` maps to multiple distinct structures AND the signature alone excludes the group's own count tag. Emitted into a shared per-version namespace — rather than once per message-path. Naming MUST be deterministic: `G_<no_tag>Args` when a `no_tag` carries exactly one signature; when a `no_tag` carries two or more signatures, no bare name is emitted and ALL variants are ordinaled `G_<no_tag>_1Args` … `G_<no_tag>_kArgs` (first-encounter ordinal over the bytewise-sorted message list). Two distinct `no_tag`s whose bodies share a byte-identical signature MUST stay separate plans (the key includes `no_tag`).
- **FR-002**: Each per-message `build_<Msg>` MUST reference the shared structural-plan Args structures, so a group/component reused **with identical structure** across N messages/paths yields **one** Args definition, not N; groups that merely share a `no_tag` but differ structurally MUST get distinct Args definitions.
- **FR-003**: The builder validation metadata for each distinct structural plan (required-field / group-count / entry-validation helpers and the `writer_traits` binding) MUST be emitted **once per shared plan**, with no duplicate or ODR-conflicting definitions across messages.
- **FR-004**: The FIX Latest (`vlatest`) typed builder tier — `build_<Msg>` / `validate_<Msg>` for every in-scope FIX Latest application message — MUST be generated and MUST compile as a single translation unit within normal build resource limits.
- **FR-005**: The deduplication MUST be implemented through a **single, version-agnostic** emitter path (no per-version special-casing); the v44 builder tier MUST be produced by that same deduplicated path.
- **FR-006**: The builder tier MUST be emitted for **every typed version that carries application messages** — v42, v44, v50sp2, and vlatest. The **per-version application-message set is decided per version at `/plan`** (NOT by blindly inheriting v44's exclusion): each version's builder set is its genuine application-message set, which MAY include messages v44 excludes (e.g. BW/BX/BY — UserRequest/UserResponse/UserNotification — on FIX 5.0 SP2). **Admin/session messages get no typed builders**: vt11 (FIXT, admin-only) MUST emit no builder output without error, and admin frames stay engine-internal.
- **FR-007**: The v44 builder golden MUST be regenerated to the deduplicated output and MUST remain byte-deterministic across runs, machines, and compilers.
- **FR-007a**: The v44 `FIXPP_CODEGEN_V44_FAMILIES` control (`all` | `official`) MUST be retained. The `official` (33-message) v44 builder golden MUST be **regenerated** to the deduplicated output at its existing path (`specs/069-v44-all-families/contracts/golden/v44_Builders_official.golden.hpp`); the `all` (83-message) v44 builder golden MUST be **newly created** — there is **no** checked-in `v44_Builders_all` golden today (069 verified `all` by differential round-trip, not a checked-in all-builder golden). Both pin the deduplicated output.
- **FR-008**: The typed v44 builders MUST remain **behaviorally correct** for all 83 in-scope application messages after dedup — identical serialized bytes and identical validation outcomes; only the generated struct identities/layout change. This MUST be pinned against a **frozen pre-077 external byte corpus** (not a regenerated golden, which would only prove the new emitter self-consistent), extended past the 5 exemplars in `test_067_builder_shape_oracle.cpp` to every distinct structural plan / multi-plan `no_tag` (or at minimum all 83). Round-trip is not a sufficient differential (see US3 Independent Test).
- **FR-009**: The legacy typed **read** tiers (Messages / Fields / Validator / Reify for v42 / v44 / v50sp2 / vt11) MUST remain **byte-identical** to pre-feature output, verified by a **recursive byte-diff of every generated legacy read artifact against a pre-077 baseline** — NOT `codegen_determinism_test` alone (determinism proves run-to-run stability, not pre-vs-post identity, so a stable-but-changed read emitter would pass it) and NOT only the checked-in `*_Messages.golden.hpp` (Fields/Validator/Reify have no golden). The change is confined to `emit_builders.cpp` (`ir.hpp`/`ir.cpp` are marked no-change, plan.md), so the read tiers are identical **by construction**; per the project's stale-object / narrow-verify-misses-golden lessons this confinement MUST be gated, not trusted. If a full four-artifact byte-diff is not built, FR-009/SC-005 MUST be narrowed to the goldened `Messages` artifact and the residual risk (Fields/Validator/Reify ungoldened) recorded explicitly.
- **FR-010**: For every builder-bearing version, builder-tier completeness MUST be verified **non-circularly at 076's raw-XML strength** — the set of emitted `build_<Msg>` entry points MUST exactly equal the version's in-scope application-message set derived from a **raw-XML / Orchestra walk independent of `emit_builders`** (a standalone parser reading each message's `msgtype` + `msgcat`/`category` directly from the source dictionary, NOT the emitter's own `ir(V).messages` walk — mirroring 076's V-1 census N-1). The red-proof MUST be a **committed test-only mutation seam**, not documented-in-prose. Re-instates and generalizes 076's descoped V-2 / V-2b legs.
- **FR-011**: Shared group Args emission MUST be dependency-ordered (a nested group's Args is complete before any group that references it) and MUST bound pathological cyclic/over-deep component reuse without emitting a referenced-but-undefined Args type.
- **FR-012**: The vlatest builder tier MUST remain governed by the existing `FIXPP_CODEGEN_FIX_LATEST` option (generated when ON, absent when OFF). 076's unconditional CMake logic that *deletes* `vlatest/Builders.hpp` MUST be removed (vlatest now emits when ON), but the OFF path MUST still leave **no stale `vlatest/Builders.hpp`**: the configure-time regen-guard MUST clean a previously-generated `vlatest/Builders.hpp` on an ON→OFF toggle (i.e. delete-when-OFF is retained *conditionally on the option being OFF*, replacing 076's unconditional delete — not simply dropped). New per-version builder outputs MUST participate in the same configure-time regen-guard / determinism discipline as existing generated headers.
- **FR-013**: New and regenerated builder goldens MUST be added for every builder-bearing version so the CI determinism/golden gates cover them.

### Key Entities

- **Repeating group (dedup unit)**: identified within a version by the key **`(no_tag, recursive structural signature)`** — the signature is delimiter + ordered members + each member's required-ness + nested children (NOT `no_tag` alone, which maps to multiple structures; and NOT the signature alone, which excludes the group's own count tag). The unit of deduplication; two occurrences collapse iff they share the same `no_tag` AND their signatures match.
- **Group Args structure**: the per-plan typed input struct a builder consumes for one group level; after this feature, defined once per distinct structural plan per version (named `G_<no_tag>Args` / `G_<no_tag>_<ordinal>Args`).
- **Per-message builder / validator**: `build_<Msg>` (serialize) and `validate_<Msg>` (required/group-presence check) entry points, one pair per in-scope application message.
- **Builder golden(s)**: checked-in reference output pinning byte-exact deterministic emission — five total: v44 `official` **regenerated**; v44 `all`, v42, v50sp2, vlatest **newly created** (see data-model Entity 5 for exact paths).
- **Builder-completeness census**: an independent derivation of a version's in-scope application-message set, asserted equal to the emitted builder set.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The FIX Latest builder header compiles as a single translation unit within ordinary developer build limits — peak memory in the low single-digit GB range (down from the >21 GB that made the pre-dedup header uncompilable).
- **SC-002**: The generated FIX Latest builder **struct surface** shrinks by an order of magnitude — the operative, compiler-limiting dimension. **MEASURED at /implement:** **576 distinct `(no_tag, signature)` group plans** (app-scope; the `/plan` census's 578 was an all-message count) down from **26,806 per-path group structs → a 46× collapse** (or 93× vs the pre-dedup 53,590 total structs incl. per-message re-nesting), the read tier's order (524 flyweights). **Size reconciliation (supersedes the pre-implement "~10 MB order" estimate):** the estimate extrapolated from the read tier's struct-only `Messages.hpp` (~9.7 MB) and did NOT account for the builder tier's per-message inline `build_<Msg>` serialization bodies, which dominate the byte count. The actual deduped `vlatest/Builders.hpp` is **~78 MB** — a ~1.75× byte reduction from 137 MB, but the **struct-count collapse (46–93×) is what restores single-TU compilability**, which is the criterion's intent and is achieved (SC-001: 3.66 GiB peak RSS, down from >21 GB). The byte-size figure is corrected here; the struct/plan-collapse + compilability criterion is MET.
- **SC-003**: 100% of each builder-bearing version's in-scope application messages have a typed builder that round-trips (build → read-back) field-for-field with zero skips.
- **SC-004**: The v44 typed-builder tests — **rewritten to name the new shared `groups::G_<no_tag>Args` types** (the nested-Args rename is an accepted source-API break, user-decided 2026-07-16; no compat aliases) — pass, and the **frozen pre-077 external byte corpus differential** (FR-008: every distinct structural plan / multi-plan `no_tag`, or ≥ all 83) shows identical serialized bytes and validation outcomes; generated builder output is byte-identical across repeated generation runs (determinism holds for every builder-bearing version).
- **SC-005**: The legacy typed read tiers are byte-identical to pre-feature output (0 diffs), measured by a recursive byte-diff of every generated read artifact against a pre-077 baseline (or, if narrowed, the goldened `Messages` artifact with the ungoldened-tier residual recorded — FR-009).
- **SC-006**: Each builder-bearing version's builder-completeness census passes as exact-set equality, and is demonstrated able to fail (proven red) when a message is dropped.

## Assumptions

- **`no_tag` does NOT uniquely identify a group's structural plan within a version** — REFUTED by the `/plan` IR census (2026-07-16). 22 `no_tag`s on vlatest (and 12 on v44, up to 8 plans for `NoLegs`/555) resolve to multiple genuinely-different ordered-member/required-ness structures. The read tier tolerates this because it *unions* members into a superset flyweight (order-independent reads); the builder cannot, so it keys by `(no_tag, full recursive structural signature)` (FR-001) — the signature discriminating variants within a count tag. The read tier compiling proves nothing about builder-key soundness — a distinction the earlier draft missed.
- **Builders are emitted only for typed versions that carry application messages** — v42, v44, v50sp2, vlatest. vt11 (FIXT session layer) has only admin/session messages, so it emits no builders. **Admin/session-message builders (e.g. a typed Logon builder) are OUT of scope** — the engine constructs admin frames internally. *(Decided at `/speckit-clarify`, 2026-07-16.)*
- **Each version's builder message-set is decided per version at `/plan`** — NOT by blindly inheriting v44's `{BE,BF,BW,BX,BY}` exclusion. A message excluded on v44 may be a genuine application builder on another version (e.g. BW/BX/BY on FIX 5.0 SP2). *(Decided at `/speckit-clarify`, 2026-07-16.)*
- **v44 retains both families modes.** The `all` (83) and `official` (33) v44 builder modes and the `FIXPP_CODEGEN_V44_FAMILIES` knob are kept; both goldens are regenerated to the deduplicated output. Whether a families-style breadth control generalizes to other versions is a `/plan` design detail. *(Decided at `/speckit-clarify`, 2026-07-16.)*
- **Per-version specifics must be enumerated at `/plan`** — application-message counts and repeating-group counts per version (e.g. the v42 read tier currently shows zero group flyweights; v50sp2 ~470 messages / 505 groups). These drive golden sizes and test surface and are not yet pinned here.
- **The dedup changes generated struct identities/layout for v44 but not the observable `build_<Msg>` / `validate_<Msg>` behavior.** The regenerated v44 golden is an intentional, accepted supersession of 076's byte-identical-builder guarantee for v44 (Option B, user-decided 2026-07-16).
- **The v44 nested-Args type NAMES are a source-breaking change, accepted without compat aliases** (user-decided 2026-07-16 at `/plan` sign-off). `fixpp::v44::<Msg>…Args` → `fixpp::v44::groups::G_<no_tag>[_ord]Args`; the existing v44 builder test files are rewritten to the new names (exact surface — distinct old names + total occurrences — **re-counted at `/tasks`**; the earlier "46 refs / 7 files" estimate under-counts, ~96 occurrences / 6 files by partial grep), and no `using`-alias shim is emitted. Defensible pre-1.0: no downstream user code exists (builders were fresh in 067/069). v42/v50sp2/vlatest are new tiers with no prior names.
- **No runtime / library / C-ABI / Python surface change.** Builders are header-only generated code; the C-ABI stays frozen at 1.5.0. This feature changes only the build-only codegen host tool, generated headers, goldens, and tests.
- **Out of scope: file-splitting** of generated output (per-message/per-category header folders + `all.hpp`). Deferred; revisit only on measured compile-cost evidence after dedup lands — dedup alone is expected to restore single-file compilability.
- **Out of scope: ApplExtID(1156)=303 differentiation and session negotiation** for FIX Latest (separate deferred work, unchanged by this feature).

## Dependencies

- **Constitution amendment (folded into Gate A, per the 074/075/076 precedent).** Delivering builders for FIX Latest re-narrows **Article I §1** (removes the "typed `build_<Msg>` builder codegen" post-1.0 carve-out for FIX Latest); delivering v42/v50sp2 builders reclassifies **Article XVIII §7** (v42/v50sp2 app-message builder widening, currently v1.x-deferred) as v1.0-delivered-by-077. Codegen is an Appendix-A mandatory Gate-A trigger.
- **Feature 076** (`fixpp::vlatest` read/reify tier + Orchestra-native IR / group model) — the builder consumes the same IR and group structure.
- **Features 067 / 069** (the v44 builder emitter, `wire::body_builder`, `builder_validate.hpp`, and the v44 builder golden) — the surface being deduplicated.
- **The read tier's `G_<no_tag>` dedup pattern** (`emit_messages.cpp`) — the proven, in-repo template the builder mirrors.
