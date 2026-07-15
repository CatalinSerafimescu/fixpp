# Phase 0 Research: FIX Latest Typed Codegen (`fixpp::vlatest`)

All decisions below are grounded in the emitter code-read performed for this feature (see spec.md Context, grade-1 `file:line` facts) and 074's shipped `OrchestraLoader`. No open `NEEDS CLARIFICATION` remains after this phase.

---

## R1 — How `build_ir` selects `OrchestraLoader` vs `XmlLoader`

**Decision**: Route by **root-element sniff**, not a CMake-supplied flag. `build_ir` peeks the input XML's root element: `<fixr:repository>` → `dict::OrchestraLoader`; `<fix>` → `dict::XmlLoader` (today's hardcoded path). The loader choice is intrinsic to the file's schema.

**Rationale**: The loader is unambiguously determined by the file, so auto-detection cannot be misconfigured by a CMake wiring mistake (pairing the wrong loader with a file would be a silent, hard-to-diagnose bug). It also keeps the `cmake/Codegen.cmake` invocation uniform — every version is still just `--xml <file> --out <dir>`; no per-file loader flag to keep in sync.

**Alternatives considered**: (a) an explicit `--orchestra` driver flag — simpler to implement but reintroduces the misconfiguration risk and asymmetry in the invocation list; kept as a fallback if the root-sniff proves awkward. (b) Path/extension heuristic (`*orchestra*`) — brittle, rejected.

**Fail-closed**: an unrecognized root element MUST throw (reuse the existing `dict::xml_parse_error` family), never silently pick a default loader.

---

## R2 — `kCodegenVersions` row + version identity

**Decision**: Add one row to `kCodegenVersions` (`ir.cpp:212`): `{.s = session_version::vlatest, .a = application_version::v50sp2, .ns = "vlatest"}`.

**Rationale**: The emitter matches `vm.s == ir.session` (`ir.cpp:258`); with `session_version::vlatest` already distinct (074), this row is all that is needed to make `build_ir` map (instead of throw at `:265-270`) and emit into `namespace fixpp::vlatest`. `.a = v50sp2` mirrors 074's `session_to_application(vlatest) → v50sp2` and is truthful (FIX Latest's wire application version is v50sp2 / ApplVerID 9); it is inert for namespace selection (the emitter keys on `ir.ns`) but kept consistent for any `.a` consumer.

**Namespace collision**: none — `ir.ns = "vlatest"` is distinct from `"v50sp2"`; `emit_messages`/`emit_validator` produce `fixpp::vlatest{,::groups,::validator}` with no overlap (spec Context fact 1).

---

## R3 — `app_version_enum("vlatest")` special-case (the generated `version_v`)

**Decision**: In `gen_util.hpp:248-253`, special-case `ns == "vlatest"` → `application_version::v50sp2` (NOT `Unknown`).

**Rationale**: Without a special-case, `app_version_enum` would emit `application_version::vlatest`, which does not exist (enum caps at `v50sp2`; 074 added no such member) — a compile error in generated code (spec Context fact 5). Mapping to `v50sp2` is **truthful**: a FIX Latest message's application version on the wire is v50sp2 (ApplVerID 9). This differs from the `vt11`→`Unknown` case (FIXT is a session-transport layer with no application version); FIX Latest *does* have one. The resulting `version_v = application_version::v50sp2` on `vlatest` classes creates no dispatch ambiguity because `vlatest` is excluded from `dispatch_application` (R7) — `version_v` is an identity tag, not a dispatch key.

**Alternative considered**: `→ Unknown` (literal vt11 mirror) — rejected as less truthful; it would erase the real application-version identity FIX Latest carries.

---

## R4 — Build option: name, default, CMake wiring

**Decision**: Add `FIXPP_CODEGEN_FIX_LATEST` (BOOL, CACHE, **default ON**) in `cmake/Codegen.cmake`. When ON, append a fifth codegen invocation `--xml <...>/OrchestraFIXLatest.xml --out <OUT>` to the existing four (`ir.cpp`/`main.cpp` need no driver change — the loader is auto-selected per R1). When OFF, the fifth invocation and the `vlatest/` output are omitted entirely.

**Rationale**: Mirrors the `FIXPP_CODEGEN_V44_FAMILIES` precedent (CACHE STRING, default `all`). Default ON satisfies the v1.0 "ship FIX Latest" obligation without a manual flip (spec Clarifications 2026-07-15), while the option gives downstream consumers and WSL2 devs an escape hatch. CI runs both paths: the ON path through the full sanitizer/preset matrix; a dedicated OFF-path job asserts the legacy-only output is byte-identical to today (FR-004/SC-003).

**Naming note**: a dedicated boolean (not folding into `FIXPP_CODEGEN_V44_FAMILIES`) — the families knob is a *within-version* family filter; this is a *whole-version-tier* toggle. Distinct concerns → distinct options.

---

## R5 — Non-circular completeness census (FR-006, the load-bearing check)

**Decision**: A census test that builds its **ground-truth set by parsing the raw `OrchestraFIXLatest.xml` directly** (independent pugixml walk, NOT via `OrchestraLoader`/`Dictionary`), transitively resolving `<component>` references and recursing `<group>` to enumerate, per message, the **full member-field set at all depths**. It then asserts **exact-set equality** (symmetric difference empty — not subset) against the **emitted `fixpp::vlatest` set** (message set and per-message field set).

**Non-circularity argument**: the ground truth comes from an independent raw-XML walk; the set-under-test comes from the emitter output (which flows through `OrchestraLoader` → `Dictionary` — the code under test). A shared Orchestra-read bug (dropped/mis-nested field) therefore appears in the set-under-test but NOT in the ground truth → the census fails. This is the property a differential-vs-runtime-XML-baseline check lacks (both sides share the loader), which is why FR-006 mandates this census specifically. Directly addresses the "corpus built from the read it checks is blind" and "exact-SET not subset" anti-patterns.

**Emitted-set source**: prefer a deterministic per-tier **manifest** (message → ordered field list) emitted alongside the headers, which the census diffs against the raw-XML set; falls back to reflecting the generated typed-args structures if a manifest is not already produced. Either way the emitted side derives from the Dictionary-under-test, preserving non-circularity.

**Discrimination proof**: the census MUST be shown to go RED under a synthetic dropped-message and a synthetic dropped-field mutation (mutation-tested witness, per the project's false-green discipline) — an observing-but-not-asserting census is a false-green.

**Group-member resolution**: the raw-XML walker must match the emitter's flattening exactly — resolve components transitively, recurse nested groups (depth-7 max per the 074 spike), and include reused group tags under each parent context. This is the census's core complexity.

---

## R6 — Build-cost measurement (owed by the spec; informs the ON default)

**Decision**: Treat the precise cost as a **measured quantity captured as the first `/implement` task**, not an ex-ante guess baked into the plan. The measurement records: configure-time delta, clean-compile wall-time delta (ON vs OFF, per preset), and binary-size delta (`libfixpp` + test binaries). The ON default (R4) **holds** unless the measurement is surprising — concretely, if the ON build materially risks a CI-job timeout or the binary-size delta is disproportionate — in which case it is re-raised with the user before merge.

**Ex-ante bound (not a substitute for measurement)**: the `vlatest` tier is **181 messages**, comparable in scale to the existing `v50sp2` tier (156 messages) that every build already compiles. So enabling `vlatest` is expected to add roughly **one v50sp2-sized tier's** compile/binary cost — a known, already-tolerated order of magnitude, not a novel blow-up. This bound is why default-ON is a reasonable starting posture; the T1 measurement confirms or refutes it. (Ex-ante vs measured discipline: predictions here are ex-ante; the lcov/'time'/size numbers are captured at `/implement`.)

---

## R7 — Dispatch exclusion (BOTH surfaces) + injective-map preservation (FR-009 / SC-005)

**Verified precondition (World A — code-read this session, grade-1)**: the generated `validate`/`reify` paths acquire their dictionary **caller-supplied**, not via a version-keyed registry — so generating the vlatest typed classes with correct typed validate + reify + round-trip is **INDEPENDENT of the deferred `version_registry` re-keying**, and R3's `version_v = v50sp2` is genuinely inert. Evidence: the generated validator emits only `constexpr static` rule tables (`emit_validator.cpp:24-88`) — there is no generated `validate_<Msg>` runtime lookup; the runtime `dictionary_driven_validator` takes its `table_view` as a **ctor argument** (`include/fixpp/wire/validator.hpp:111-112`). Generated reify (`emit_reify.cpp`) is a **dict-free** deep byte copy (`:328-345`); `version_v` (`:151-153`) is only returned by `which()` (`:184-185`), never a lookup key. `version_registry::get()` is a session/engine-config artifact, never called by generated code. **⇒ The deferral holds; this feature does not need the re-keying.**

**Two deferred surfaces, BOTH excluded (no emitter change needed)**: `application_version`-keyed dispatch collides `vlatest` onto the `v50sp2` slot at two runtime auto-dispatch sites, and `vlatest` is already absent from both:
1. **`dispatch_application`** (validator dispatch) — `kAppVersions` (`emit_dispatch.cpp:62-64`) lists only v42/v44/v50sp2, matched by `ns` (`:224-232`); no `vlatest` case is produced.
2. **`reify_dispatch_application`** (raw-inbound message typing) — outer switch keys on `resolved_application` with arms only v42/v44/v50sp2 (`emit_dispatch.cpp:214-234`); `resolve_application_version(...)` can only return `v50sp2` for FIX Latest (no `application_version::vlatest`), so a raw inbound Latest message would mis-type to v50sp2 — the deferred path.

Both are the desired behavior: FIX Latest classes generate/validate/round-trip via the **direct typed API + caller-supplied dict**, but are unreachable via either auto-dispatch, and the wire-ApplVerID map stays injective (no duplicate `case application_version::v50sp2`).

**Witnesses (required by SC-005)**: assert (a) `dispatch_application` for a FIX-Latest-only MsgType → fail-loud default (not a `vlatest` class); (b) `reify_dispatch_application` for a raw FIX-Latest-only wire message → does NOT silently produce a `vlatest`-typed owner (it either mis-resolves to v50sp2's default or fails loud — pin the *actual* deferred behavior so the ApplExtID feature has a regression anchor when it fixes it); (c) exactly one `application_version::v50sp2` case exists in each generated switch (no duplicate). These pin the deliberate exclusion of **both** surfaces so the future ApplExtID feature adds reachability + re-keys consciously, not by a silent `kAppVersions` edit that breaks injectivity.

---

## R8 — Codegen determinism golden

**Decision**: Regenerate and check in the codegen golden extended with the `vlatest` tier; the existing codegen determinism test MUST cover `vlatest` so a non-deterministic or stale emit fails CI (byte-diff against the checked-in golden). The golden regen is a first-class task, run under the full ctest (narrow-target verify misses the determinism hang — established lesson).

**Rationale**: The codebase already ships checked-in codegen goldens and a determinism gate; omitting `vlatest` from the golden would leave the largest new tier unguarded against non-deterministic ordering (e.g., unordered map iteration in the emitter) and against stale-tool regeneration. The golden also becomes the artifact the census's manifest side (R5) can be validated against.

---

## R9 — Fail-closed on unknown Orchestra datatypes (FR-010)

**Decision**: Inherit 074's datatype gate — no new codegen-side gate. `OrchestraLoader` already fails closed (`orchestra_parse_error : xml_parse_error`) on a datatype outside the mapped set; codegen consuming that loader therefore never sees an unknown-datatype field (EP303 has none — 074's Deliverable #1 catalogue). Add a small witness: codegen over a synthetic Orchestra fragment carrying an unmapped datatype MUST fail closed (thrown, non-zero exit), not emit a mis-typed field.

**Rationale**: Reuses the proven fail-closed path rather than adding a parallel one (mirror an existing fail-closed disposition; don't invent a second). The witness proves the path is live for the codegen entry point, not just the runtime loader.

---

## Summary of code touch points (from R1–R9)

| # | File | Change | Risk |
|---|------|--------|------|
| 1 | `tools/codegen/fixpp-codegen/ir.cpp` | root-sniff loader branch in `build_ir` (R1) + `kCodegenVersions` vlatest row (R2) | med — the one real logic change |
| 2 | `tools/codegen/fixpp-codegen/gen_util.hpp` | `app_version_enum("vlatest")→v50sp2` (R3) | low |
| 3 | `cmake/Codegen.cmake` | `FIXPP_CODEGEN_FIX_LATEST` option (default ON) + 5th invocation (R4) | low |
| 4 | `tools/codegen/golden/` | extend determinism golden with vlatest (R8) | low (mechanical, gated) |
| 5 | `tests/codegen/` | non-circular census (R5) + dispatch-exclusion witness (R7) + unknown-datatype witness (R9) | med — census walker is the hard part |
| 6 | `tests/` (round-trip) | 181-message typed round-trip + build-option ON/OFF behavior witness (R5/R4) | low-med |

**Deliberately NOT touched**: `emit_messages.cpp`, `emit_validator.cpp`, `emit_dispatch.cpp`, `main.cpp`, all `src/` runtime, `capi/`, `bindings/python/`.
