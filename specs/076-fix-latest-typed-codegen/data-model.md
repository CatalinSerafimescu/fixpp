# Phase 1 Data Model: FIX Latest Typed Codegen (`fixpp::vlatest`)

> **⚠️ [DEFERRED 2026-07-16 — see spec.md Clarifications → Session 2026-07-16]** The typed **`build_<Msg>`/`validate_<Msg>` builder tier** (Option A / RC-B app-subset) documented below as an entity/attribute of this feature is **deferred to a follow-up feature** (137 MB uncompilable `vlatest/Builders.hpp` from the non-deduped `emit_builders`). What ships in this PR is the **read/reify/args/readback + runtime-validator tier for all 181** messages, plus the completeness census (Entities 3/3b). Builder-specific sentences below are marked `[DEFERRED 2026-07-16]` inline and retained for traceability — do not read them as delivered.

This feature adds no runtime data structures; its "entities" are codegen-time constructs and the verification sets. Documented for traceability.

## Entities

### 1. `kCodegenVersions` row (the `vlatest` version map)

The codegen version-partition record (`tools/codegen/fixpp-codegen/ir.cpp:206-227`).

| Field | Value for vlatest | Notes |
|-------|-------------------|-------|
| `s` (`session_version`) | `session_version::vlatest` | The partition key (074 kept it distinct). Matched at `ir.cpp:258`. |
| `a` (`application_version`) | `application_version::v50sp2` | Truthful (074 `session_to_application`); inert for namespace selection. |
| `ns` (namespace tag) | `"vlatest"` | Drives `namespace fixpp::vlatest`, output dir, and (via `app_version_enum`) `version_v`. |

**Uniqueness rule**: `ns` MUST be unique across rows (`"vlatest"` ≠ `"v50sp2"`), guaranteeing namespace disjointness.

### 2. Generated typed message artifact (per FIX Latest message, ×181)

Emitted into `namespace fixpp::vlatest` — **identical shape to the legacy tiers** (067/069), no new artifact kinds. **Scope split (Option A, RC-B) — [DEFERRED 2026-07-16, see banner above]:** the universal artifacts are emitted for **all 181** messages; `build_<Msg>`/`validate_<Msg>` were DESIGNED to be emitted for the **application subset** only (per the Entity 5 `category→is_application` mapping), exactly as 069 scopes builders — **but this PR emits neither** (`emit_builders` stays v44-only; no `vlatest/Builders.hpp`).

- **[all 181, DELIVERED]** typed **args** struct — the message's typed field parameters.
- **[all 181, DELIVERED]** **readback** accessors — typed field reads.
- **[all 181, DELIVERED]** per-message `dict::reify()` round-trip participation.
- **[all 181, DELIVERED]** `version_v` member constant = `application_version::v50sp2` (R3).
- **[app-subset, DEFERRED 2026-07-16]** `build_<Msg>(...)` — typed builder over `wire::body_builder`.
- **[app-subset, DEFERRED 2026-07-16]** `validate_<Msg>(...)` — typed **required/group-presence** validator over the typed `Args` (thin wrapper over `wire::validate_required<T>`, `emit_builders.cpp:510-573`); **dict-free, no enum/type/domain check**. Enum/type/domain validation is the separate runtime `dictionary_driven_validator`, constructed with a caller-supplied `vlatest` `table_view` (`validator.hpp:111-112`) and run over a parsed `MessageView` (075) — not part of `validate_<Msg>`.

**Field membership** derives from the `vlatest` `Dictionary` (074): header + body + repeating-group members at all depths, components resolved. Codeset fields carry flattened enum values (minimal model, per 074); `unionDataType` second arm dropped.

### 3. Census sets (verification-time, FR-006)

Two independently-derived sets, compared for exact equality:

| Set | Source | Role |
|-----|--------|------|
| **Ground-truth set** | raw `OrchestraFIXLatest.xml`, parsed by a walker **independently implemented from the `ir.cpp` Orchestra projection** (N-1 — shares no code) | authoritative; multiset of occurrence-path keys `(MsgType, group_path/no_tag_path, tag, presence/rule, datatype)` |
| **Emitted set** | a dedicated **per-message manifest emitted from the Orchestra IR projection's lossless occurrence list** (research R2b). **NOT** from `MessageIR.fields` (tag-deduped, immediate-parent-only — lossy) and **NOT** the read-side group flyweights `fixpp::<ns>::groups::G_<no_tag>` (version-wide deduped UNION, `emit_messages.cpp:403-419`). Emitted for all 181, ungated by the app-subset builder filter. | under test |

**Assertion**: `ground_truth == emitted` as **exact multisets of occurrence-path keys** (symmetric difference empty) at both the message level (181 == 181) and the per-message occurrence-path level (all depths). **Non-circularity (N-1):** because both sides are raw declaration-order walks (the manifest is **projection-sourced on all dimensions**, not loader/Dictionary-sourced), non-circularity is a **double-entry cross-check of two independent walkers** uniformly — the census walker MUST share no code with the projection. A **flat per-message tag set is insufficient** — it cannot distinguish a top-level tag from an in-group tag, a field moving between groups, or a tag reused under different parents. Any asymmetry ⇒ FAIL; discrimination is proven RED under dropped-message, dropped-field, **and wrong-parent / reused-tag-under-different-parents** mutations. **Surface pinned:** this census pins the **projection + app-subset builder** surface (`group_order`, the same projection output, feeds `emit_builders`) at occurrence-path granularity — NOT the shipped universal read/reify/args classes, which are a different `MessageIR`/Dictionary derivation pinned by the separate leg below.

### 3b. Manifest ↔ shipped-class consistency set (V-1b — read-surface leg)

The census (Entity 3) proves *manifest ≡ raw-XML*, but the **shipped typed read/reify/args classes** (Entity 2, all 181) are emitted from `MessageIR`/Dictionary, a *different, unchanged* derivation the manifest is forbidden from sourcing — so the census pins the projection/builder surface, not the read surface (the only other leg, the FR-007 differential round-trip, is circular per US2 and blind to an absent field). V-1b closes this: it compares the **projection-sourced manifest** field set against the **actually-reachable field set extracted from the shipped `fixpp::vlatest` class** — message set (all 181) and per-message field set at **class-reachable-field granularity** (tag-deduped top-level + version-wide union group flyweights). Because the two sides are *different derivations* (projection vs Dictionary), composing `class ≡ manifest` (V-1b) ∘ `manifest ≡ raw-XML` (census) pins **class ≡ raw-XML** for the read surface **non-circularly**, at class-reachable-field granularity. Proven RED under a class-side (`emit_messages`/reify) dropped-field/dropped-message mutation.

### 5. Orchestra `category → is_application` mapping (codegen-time, RC-A)

Orchestra has **no `msgcat`**; it carries `category=` (30 message-level values). The IR projection (research R2b) maps each message's `category` to `is_application` via a **verified single-category rule**: the **8 `category="Session"` frames** — Heartbeat(0), TestRequest(1), ResendRequest(2), Reject(3), SequenceReset(4), Logout(5), Logon(A), XMLnonFIX(n) — → `admin` (`is_application=false`); **every other category** → `app` (`true`). NB `category="Testing"` (AlgoCertificate*/TestSuite*/TestAction*) is an **application** category, not session admin. It **fails closed** on an unmapped/absent category (mirrors the `msgcat` fail-closed at `ir.cpp:192-195`). This mapping was DESIGNED as the sole source of the app-subset used by the vlatest builder coverage predicate (Entity 2 / FR-001); it is NOT pinned to a count. **[DEFERRED 2026-07-16]** **INV-5 (admin-complement pin):** a witness asserts the derived admin complement equals the exact set `{0,1,2,3,4,5,A,n}`, so a category silently misclassified as admin (which would drop a `build_<Msg>` with no other RED test — the census manifest is emitted for all 181 ungated) fails loudly. **This whole entity is moot for the current PR** (no `build_<Msg>` at all is emitted for `vlatest`) — retained for the follow-up builder feature.

### 4. `FIXPP_CODEGEN_FIX_LATEST` build option

| Attribute | Value |
|-----------|-------|
| Type | CMake BOOL, CACHE |
| Default | `ON` |
| ON effect | 5th codegen invocation over `OrchestraFIXLatest.xml`; `fixpp::vlatest` generated + compiled |
| OFF effect | no `vlatest` generation/compilation; legacy tiers byte-identical to today |

## Relationships / invariants

- `vlatest` row (Entity 1) → enables Entity 2 emission → measured by Entity 3 census; gated by Entity 4 option.
- **INV-1 (additive)**: toggling Entity 4 never alters v42/v44/v50sp2/vt11 generated bytes (FR-004/SC-003).
- **INV-2 (injective wire-ApplVerID)**: Entity 2's `version_v = v50sp2` is an identity tag only; `vlatest` is NOT added to `dispatch_application`'s `kAppVersions`, so no duplicate `case application_version::v50sp2` exists (FR-009/SC-005).
- **INV-3 (exact completeness, projection/builder surface)**: Entity 3 sets are equal (not subset) — no message or field dropped/added at occurrence-path granularity (FR-006/SC-001, census/V-1). This pins the projection + app-subset builder surface — the "builder" half of that surface name is **[DEFERRED 2026-07-16]** (no builders are emitted in this PR); the census itself (projection ≡ raw-XML) is DELIVERED and holds regardless, since it never gates on the app-subset filter.
- **INV-4 (determinism)**: Entity 2 output is byte-stable vs the checked-in golden (R8).
- **INV-6 (read-surface completeness, non-circular)**: Entity 3b holds — the projection-sourced manifest's per-message field set equals the shipped read/reify/args class's reachable field set (message set 181==181, class-reachable-field granularity), so `class ≡ manifest ≡ raw-XML` pins the **universal read surface ≡ raw-XML** non-circularly (V-1b ∘ census). Proven RED under an `emit_messages`/reify class-side drop; the circular FR-007 round-trip cannot substitute (blind to an absent field). Closes the gap that INV-3 alone leaves for the read surface (FR-006/SC-001/US2 co-P1).
