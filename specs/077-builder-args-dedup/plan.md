# Implementation Plan: Typed builder tier for all FIX versions via group-Args deduplication

**Branch**: `077-builder-args-dedup` | **Date**: 2026-07-16 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/077-builder-args-dedup/spec.md`

## Summary

Redesign the builder emitter (`emit_builders.cpp`) so each repeating group's
input `Args` struct is emitted **once per distinct recursive structural plan**
into a shared `fixpp::<ns>::groups` namespace (named `G_<no_tag>Args`, or
`G_<no_tag>_<ordinal>Args` for a `no_tag` carrying multiple plans), instead of
once per message-path. This collapses the FIX Latest builder header from
26,806 per-path structs (137 MB, uncompilable) to **578 distinct plans** (read-
tier order, ~10 MB, single-TU-compilable), then re-enables the vlatest builder
tier 076 descoped and extends builders to every application-bearing typed
version (v42/v44/v50sp2/vlatest; vt11 is admin-only → none). v44's shipped
builder golden is deliberately regenerated to the deduped output; legacy read
tiers stay byte-identical.

The design key is **structural identity, not `no_tag`** — the `/plan` census
(research.md R2) proved a single `no_tag` maps to up to 8 genuinely-different
structures within one version, because a builder is order- and
required-ness-sensitive where the read tier's union-by-tag is not.

## Technical Context

**Language/Version**: C++23 (host codegen tool + generated headers). Clang
primary; GCC CI sanity; MSVC Windows Tier-2.

**Primary Dependencies**: build-only — `fixpp::dictionary` (`build_ir()` /
XmlLoader / Orchestra projection), pugixml (host-tool-local declaration-order
re-parse). No new dependency. `wire::body_builder` / `builder_validate.hpp`
(consumed by generated builders, unchanged).

**Storage**: N/A. Generated headers land in the build tree
(`build/<preset>/_codegen/include/fixpp/<ns>/Builders.hpp`); checked-in goldens
under `specs/*/contracts/golden/`.

**Testing**: ctest (codegen determinism + 067/069 v44 builder round-trip/
validate suites + new per-version round-trip + FR-010 completeness census);
`clang++ -fsyntax-only` compile-resource check for the deduped headers.

**Target Platform**: build-time host tool (Linux dev, CI matrix); generated
headers are header-only, consumed on all platforms.

**Project Type**: single project — C++ library + its build-only codegen host
tool. No frontend/backend split.

**Performance Goals**: (a) deduped `vlatest/Builders.hpp` compiles as one TU in
low-single-digit-GB RSS (down from >21 GB); (b) generated source ~10 MB order
(down from 137 MB); (c) codegen runtime stays in the existing configure-time
envelope (the emitter change is O(occurrences) with a signature-hash map).

**Constraints**: byte-deterministic emission (existing determinism gate);
legacy read tiers byte-identical (FR-009); C-ABI frozen at 1.5.0 (no runtime
surface change); Article VIII compile-budget — v50sp2/vlatest builder TUs are
expected to join v50sp2's read tier as recorded `KNOWN_OVERAGE` against the
≤3 s syntax-only ceiling (not a regression; measured + logged, mitigation
tracked), see Complexity Tracking.

**Scale/Scope**: 4 builder-bearing versions; in-scope app messages 39/83/156/173
(v42/v44/v50sp2/vlatest); distinct emitted plans 29/89/558/578; 5 goldens
(v44 all + v44 official regenerated; v42/v50sp2/vlatest new).

## Constitution Check

*GATE: re-checked after Phase 1 design — PASS with two folded amendments.*

| Gate (Article) | Status | Notes |
|---|---|---|
| **I §1 — v1.0 codegen scope / FIX-Latest carve-out** | **AMEND (Gate A)** | Removes the "typed `build_<Msg>` builder codegen … remain post-1.0" carve-out **for FIX Latest**; the 076 close-out named exactly this ("pending a component-identity Args-dedup redesign") — 077 delivers that redesign. ApplExtID(1156)=303 + session negotiation stay post-1.0 (unchanged). |
| **XVIII §7 — v42/v50sp2 app-builder widening** | **AMEND (Gate A)** | Reclassifies "`fixpp::v42` and `fixpp::v50sp2` application-message widening … remains v1.x-deferred" as **v1.0-delivered-by-077**. v44 in-scope set unchanged (83 all / 33 official). |
| **XVIII §2 (v1.2 annotation)** | Update | The "only their typed `build_<Msg>` builders … remain post-1.0" annotation is narrowed to ApplExtID/EP-back-port only. |
| Appendix A — Codegen layout trigger | **Satisfied** | All four mandatory controls: `/clarify` ✓ (done, 3 Q), `/analyze` (pre-`/implement`), Codex **Gate A** (after `/plan`), user `/plan` sign-off. |
| X — ABI Policy | PASS | Header-only generated code; no C-ABI symbol/signature/error-code change (frozen 1.5.0). |
| VI — 100% FIX / no silent omissions | PASS+ | FR-010 exact-set completeness census per version *strengthens* coverage; catalogue rows for v42/v50sp2/vlatest builders move to delivered. |
| VII — Testing | PASS | Golden + determinism + round-trip + red-provable completeness gate (research.md R5). |
| VIII — Perf budgets | PASS w/ recorded overage | Compile-time is the only budget touched; v50sp2/vlatest builder TUs recorded `KNOWN_OVERAGE` like the v50sp2 read tier (Complexity Tracking); runtime hot path untouched. |
| IX — Coverage/sanitizers/static-analysis | PASS | Host tool + generated headers under existing tiers; new emitter code carries unit coverage. |
| XVI — Spec Kit workflow | PASS | Pipeline order honored (Gate A after `/plan`, before `/tasks`). |

No unjustified violations. The two amendments are the expected, precedented
(074/075/076) codegen re-narrowing folded into Gate A.

## Project Structure

### Documentation (this feature)

```text
specs/077-builder-args-dedup/
├── plan.md              # this file
├── research.md          # Phase 0 — census + structural-key decision
├── data-model.md        # Phase 1 — entities (structural plan, shared Args, census)
├── quickstart.md        # Phase 1 — validation scenarios
├── contracts/
│   ├── generated-builder-dedup.md   # deduped Builders.hpp shape contract
│   ├── builder-completeness.md      # FR-010 census/gate contract
│   └── golden/                      # regenerated + new byte-goldens (created at /implement)
└── tasks.md             # /speckit-tasks output (NOT this command)
```

### Source Code (repository root = the library submodule)

```text
tools/codegen/fixpp-codegen/
├── emit_builders.cpp        # PRIMARY CHANGE — structural-plan dedup, remove v44-only gate,
│                            #   shared groups namespace, per-plan writer_traits/helpers
├── ir.hpp / ir.cpp          # group_order already carries declaration-order plans (no change
│                            #   expected; confirm structural-signature inputs are all present)
├── emit_messages.cpp        # read tier — REFERENCE ONLY (proven G_<no_tag> namespace pattern)
└── main.cpp                 # --families stays v44-only; other versions emit their app set

cmake/Codegen.cmake          # remove the vlatest/Builders.hpp deletion (076 descope artifact);
                             #   wire per-version Builders.hpp into regen-guard/determinism

tests/session/               # 067/069 v44 builder tests NAME message-rooted nested Args types
                             #   (46 refs / 7 files, e.g. NewOrderListOrdersPartyIDsArgs) that dedup
                             #   RENAMES to groups::G_<no_tag>Args — these sources will not compile
                             #   on golden-update alone. Resolution = ALIAS vs BREAK (see below);
                             #   plus new per-version round-trip + FR-010 completeness census tests
tests/codegen/               # codegen unit + determinism + completeness census

specs/077-builder-args-dedup/contracts/golden/   # 5 goldens (v44×2 regen, v42/v50sp2/vlatest new)
docs/src/dictionary/codegen.md                    # update: builders now all-version + deduped
spec/behaviors-and-limitations.md                 # L-076-1 → resolved-by-077
```

**Structure Decision**: Single-project layout. The change is concentrated in one
host-tool source file (`emit_builders.cpp`) plus CMake wiring, goldens, tests,
and doc/limitation updates — mirroring the read-tier dedup that already lives in
`emit_messages.cpp`.

## Complexity Tracking

| Item | Why | Simpler alternative rejected because |
|---|---|---|
| Structural-identity key (recursive signature) instead of `no_tag` | `/plan` census: `no_tag` → up to 8 distinct builder structures/version; the builder is order- + required-ness-sensitive | `no_tag`-alone (spec's original framing) is **unsound** — would silently corrupt serialization/validation for the 22 colliding vlatest `no_tag`s |
| `G_<no_tag>_<ordinal>Args` variant naming | genuine structural variants must not collide | one struct per `no_tag` re-introduces the union problem the builder cannot tolerate |
| v50sp2 / vlatest builder-TU compile-time `KNOWN_OVERAGE` | ~558/578 plans × nested groups exceeds the ≤3 s syntax-only ceiling, exactly as the v50sp2 *read* tier already does | tightening now would block delivery; overage is recorded (Article VIII exit-0 `KNOWN_OVERAGE`), mitigation (fwd-decl/split-header/PCH) tracked with the read tier's existing note — not this feature's scope |

### Decision (resolved at `/plan` sign-off, 2026-07-16): **BREAK — no aliases, rewrite tests**

**v44 nested-Args rename: backward-compat ALIAS vs. accept BREAK.** Dedup moves
`fixpp::v44::<Msg>…Args` → `fixpp::v44::groups::G_<no_tag>[_ord]Args`. 46 refs
across 7 v44 builder test files (and any example/doc snippets) name the old
message-rooted types.
- **ALIAS** — emit `using <old name> = groups::G_<no_tag>[_ord]Args;` for v44's
  ~730 old per-path names in `fixpp::v44`. Existing sources compile unchanged;
  header gains ~730 typedef lines (cheap vs the struct-body win). Only v44 needs
  this (v42/v50sp2/vlatest are new).
- **BREAK** — no aliases; rewrite the 7 v44 test files (46 refs, mechanical) to
  name `groups::G_…Args`. Cleaner, full size win, source-breaking for the v44
  builder API (defensible pre-1.0 — this feature already supersedes v44's
  byte-identical golden guarantee). No user code exists yet (builders were fresh
  in 067/069).

**CHOSEN: BREAK** (user, 2026-07-16). `/tasks` includes a **test-rewrite task**
(7 v44 files / 46 refs → `groups::G_…Args`), **no** alias-emission task. The v44
builder type names `fixpp::v44::<Msg>…Args` are a source-breaking change; no
downstream user code exists (builders were fresh in 067/069, pre-1.0).
