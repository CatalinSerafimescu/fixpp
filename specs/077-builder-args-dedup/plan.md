# Implementation Plan: Typed builder tier for all FIX versions via group-Args deduplication

> **⚠️ /implement amendments (2026-07-16) — this doc's per-version numbers are superseded; the authoritative record is `.specify/decisions/077-builder-args-dedup-verify.md`:**
> 1. **v42 builders DESCOPED** (user-decided, issue #196): FIX 4.2 types `NumInGroup` as legacy XML `INT`, so the emitter materializes 0 typed groups (L-063-1) and a scalar-only v42 builder would silently omit required groups (invalid FIX 4.2). **Builder-bearing versions = {v44, v50sp2, vlatest}**; v42 joins vt11 as non-builder-bearing. Any "v42" builder count/golden/round-trip below is void.
> 2. **Plan-count correction (T005):** the 29/89/558/578 figures were an all-message census; the builder's app-scope counts are **v44 88 / v50sp2 558 / vlatest 576** (v42 descoped). The census pins (558/576) were CORRECT — an interim build-tree under-count (555/573) was a real bug fixed at T017 (`is_n002_n003_excluded` was applied version-unscoped, wrongly dropping BE/BF/BW/BX/BY from v50sp2/vlatest; scoped to v44). vlatest source size is ~77 MB (not the ~10 MB estimate; dedup collapsed 26,806→576 structs, so it compiles at ~3.7 GiB RSS — SC-002 size wording reconciled at T013/T015).

**Branch**: `077-builder-args-dedup` | **Date**: 2026-07-16 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/077-builder-args-dedup/spec.md`

## Summary

Redesign the builder emitter (`emit_builders.cpp`) so each repeating group's
input `Args` struct is emitted **once per distinct recursive structural plan**
into a shared `fixpp::<ns>::groups` namespace (named `G_<no_tag>Args` for a
`no_tag` with exactly one plan, or — for a `no_tag` carrying two or more plans —
no bare name and all variants ordinaled `G_<no_tag>_1Args` … `G_<no_tag>_kArgs`),
instead of once per message-path. This collapses the FIX Latest builder header from
26,806 per-path structs (137 MB, uncompilable) to **578 distinct plans** (read-
tier order, ~10 MB, single-TU-compilable), then re-enables the vlatest builder
tier 076 descoped and extends builders to every application-bearing typed
version (v42/v44/v50sp2/vlatest; vt11 is admin-only → none). v44's shipped
builder golden is deliberately regenerated to the deduped output; legacy read
tiers stay byte-identical.

The design key is **`(no_tag, recursive structural signature)`** — the signature
(not `no_tag` alone) discriminates variants within a count tag, and pairing it
with `no_tag` (which the signature by itself omits) keeps the naming
deterministic and never collapses two distinct count tags. The `/plan` census
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
surface change); **compile-time resource budget** — v50sp2/vlatest builder TUs
are expected to join v50sp2's read tier as a recorded `KNOWN_OVERAGE` against the
≤3 s single-version syntax-only ceiling, tracked via the **003 compile-bench /
decision-record convention** (`.specify/decisions/003-dictionary-codegen-verify.md:101`,
T046 compile-time bench — NOT an Article VIII mechanism; Article VIII is
runtime-only). Measured + logged, mitigation tracked; see Complexity Tracking.

**Scale/Scope**: 4 builder-bearing versions; in-scope app messages 39/83/156/173
(v42/v44/v50sp2/vlatest); distinct emitted plans (# distinct
`(no_tag, signature)` pairs) 29/89/558/578; 5 goldens (v44 official
**regenerated**; v44 all + v42/v50sp2/vlatest **new** — no `v44_Builders_all`
golden exists today).

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
| VIII — Perf budgets | PASS | Article VIII is **runtime-only** (Google Benchmark, ±5% regression, hot-path allocator, latency targets) and the runtime hot path is untouched, so VIII trivially PASSes. Article VIII has **no** compile-time ceiling / `KNOWN_OVERAGE` hatch. The compile-time overage of the v50sp2/vlatest builder TUs is tracked separately via the **003 compile-bench / decision-record convention** (T046), cited in Complexity Tracking — not under this Article. |
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
                             #   (e.g. NewOrderListOrdersPartyIDsArgs) that dedup RENAMES to
                             #   groups::G_<no_tag>Args — these sources will not compile on
                             #   golden-update alone. Resolution = BREAK (see below); the exact
                             #   rewrite surface (distinct old names + total occurrences) is
                             #   RE-COUNTED at /tasks (the "46 refs / 7 files" note under-counts —
                             #   ~96 occurrences / 6 files by partial grep); plus new per-version
                             #   round-trip + FR-010 completeness census tests
tests/codegen/               # codegen unit + determinism + completeness census

specs/077-builder-args-dedup/contracts/golden/   # 4 NEW goldens (v44_Builders_all, v42/v50sp2/vlatest);
                             #   v44 official regenerated in place at specs/069-.../v44_Builders_official.golden.hpp
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
| v50sp2 / vlatest builder-TU compile-time `KNOWN_OVERAGE` | ~558/578 plans × nested groups exceeds the ≤3 s single-version syntax-only ceiling, exactly as the v50sp2 *read* tier already does | tightening now would block delivery; overage is recorded via the **003 compile-bench / decision-record convention** (`.specify/decisions/003-dictionary-codegen-verify.md:101`, T046 — the SAME convention that recorded the v50sp2 read tier's ~8–9 s overage; NOT an Article VIII mechanism), captured RSS + wall time in the feature verify record, mitigation (fwd-decl/split-header/PCH) tracked with the read tier's existing note — not this feature's scope |

### Decision (resolved at `/plan` sign-off, 2026-07-16): **BREAK — no aliases, rewrite tests**

**v44 nested-Args rename: backward-compat ALIAS vs. accept BREAK.** Dedup moves
`fixpp::v44::<Msg>…Args` → `fixpp::v44::groups::G_<no_tag>[_ord]Args`. The v44
builder test files (and any example/doc snippets) name the old message-rooted
types; the exact surface is **re-counted at `/tasks`** (the earlier "46 refs / 7
files" estimate under-counts — a partial grep already finds ~96 occurrences
across 6 files, before other group names or the 7th file).
- **ALIAS** — emit `using <old name> = groups::G_<no_tag>[_ord]Args;` for v44's
  ~730 old per-path names in `fixpp::v44`. Existing sources compile unchanged;
  header gains ~730 typedef lines (cheap vs the struct-body win). Only v44 needs
  this (v42/v50sp2/vlatest are new).
- **BREAK** — no aliases; rewrite the v44 test files (mechanical, over the
  `/tasks`-measured surface) to name `groups::G_…Args`. Cleaner, full size win,
  source-breaking for the v44 builder API (defensible pre-1.0 — this feature
  already supersedes v44's byte-identical golden guarantee). No user code exists
  yet (builders were fresh in 067/069).

**CHOSEN: BREAK** (user, 2026-07-16). `/tasks` includes a **test-rewrite task**
(rewrite the v44 builder test files' message-rooted Args names → `groups::G_…Args`),
**no** alias-emission task. The v44 builder type names `fixpp::v44::<Msg>…Args`
are a source-breaking change; no downstream user code exists (builders were
fresh in 067/069, pre-1.0). **Scope RE-COUNT at `/tasks` (do not trust the
"46 refs / 7 files" figure — it is unverified and appears to under-count: a
partial grep already found ~96 occurrences across 6 files):** `/tasks` measures
BOTH the distinct old type-names AND the total occurrences across the v44
builder test/example/doc sources, and sizes the rewrite task from the measured
number.

## Gate A

- Round 1 applied 2026-07-16: Codex P1=0 P2=6 P3=1; Opus post-judging P1=0 P2=6 P3=4; rewrite addresses Root cause #1 (dedup key → (no_tag, recursive_signature)), Root cause #2 (independent-derivation verification: raw-XML completeness census + mutation seam, extended v44 frozen-corpus differential, read-tier OFF/ON byte-diff), and the citation cluster (Article VIII compile-budget miscite → 003 compile-bench convention; golden inventory → 5 enumerated paths). Reviews: research/reviews/codex_077-builder-args-dedup_gate_a_review.md, research/reviews/opus_077-builder-args-dedup_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-16: Codex P1=0 P2=4 P3=0; Opus post-judging P1=0 P2=3 P3=1; rewrite addresses R2-A (delete stale "lower bound" count hedge — emit-N table is the exact distinct-(no_tag,signature)-pair count for measured dicts, sole caveat = census↔emitter fidelity via golden), R2-B (naming rule → G1a exact wording: ≥2 signatures ⇒ all ordinaled, no bare name, propagated to 5 loci), R2-C (completeness actual(V) → compile-time entry-point-existence census over build_<Msg>/validate_<Msg>, registry parse secondary), and the P3 (name FIXPP_CODEGEN_DROP_BUILDER_MSGTYPE + builder_completeness_mutation_witness as THE committed seam). Reviews: research/reviews/codex_077-builder-args-dedup_gate_a_2_review.md, research/reviews/opus_077-builder-args-dedup_gate_a_2_adversarial_review.md.
- Round 3 converged 2026-07-16: Codex P1=0 P2=0 P3=1; Opus post-judging P1=0 P2=0 P3=1 — **CONVERGED, gate-a-done, no waivers**. Sole residual P3 = the completeness gate's `actual(V) ⊆ expected(V)` leg rests on an unproven **co-emission invariant** (`build_<Msg>`/registry-entry/`validate_<Msg>` emitted from one loop, `emit_builders.cpp:715/717/718`); carried into `/tasks` as a T023 note + optional header-symbol-census closure (not a blocker — the underlying failure mode is source-proven unreachable). Canonical record: `.specify/decisions/077-builder-args-dedup-gatea.md`. Reviews: research/reviews/codex_077-builder-args-dedup_gate_a_3_review.md, research/reviews/opus_077-builder-args-dedup_gate_a_3_adversarial_review.md.
