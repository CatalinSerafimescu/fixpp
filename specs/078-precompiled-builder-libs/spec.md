# Feature Specification: Precompiled per-version builder/validator libraries

**Feature Branch**: `078-precompiled-builder-libs`

**Created**: 2026-07-17

**Status**: Draft

**Input**: User description: "on #198 — Split typed builder/validator codegen: per-message headers + opt-in validators + shared groups (post-077 dedup)"

## Overview

The typed builder/validator codegen tier ships **one monolithic `fixpp/<ver>/Builders.hpp` per version** containing every message. Post-077 (Args-dedup) it compiles and is correct, but every translation unit that `#include`s it pays for **all** messages — even to use one — and there is **no code sharing across versions**. Measured cost: **~3.6 GiB peak RSS to merely `#include` one version's header**, and **~39–40 MiB of `.text`** for the three shipped versions combined, with near-total duplication between `v50sp2` and `vlatest`.

This feature restructures the tier around a single organizing principle decided by the user:

> **Everything is always emitted and always compiled once, per version, into precompiled libraries. All opt-in is purely link-time.**

Concretely: each version's builders compile once into `libfixpp_builders_<ver>`; each version's validators compile once into a **separate** `libfixpp_validators_<ver>`. A consumer includes a **slim declaration header** and **links** whichever libraries it needs — pay nothing in compile time or binary size for what it does not link. A per-message **header-only inline mode** (behind a macro) lets a client force zero-overhead inlining for the few hot messages while linking the bulk. The tier is **100% client-facing**: the library core does not consume typed builders, so this restructuring does not touch the core send/build path.

## Clarifications

### Session 2026-07-17

- Q: Validators packaging & opt-in mechanism — always-built separate lib with link-time opt-in (memory) vs. codegen-switch default-OFF (issue #198 text)? → A: **Always emit + always compile a separate `libfixpp_validators_<ver>`; opt-in is purely link-time.** (Confirms Assumption A1; resolves the issue-text divergence.)
- Q: Header-only inline mode (per-message force-inline + mixing, US3/FR-006/FR-007) — ship in this feature or defer? → A: **Ship in this feature** — precompiled libs (default) AND per-message header-only inline mode with mixing, from one generation. (Confirms Assumption A2.)
- Q: #197 CI heavy-test stopgap (`FIXPP_BUILD_HEAVY_BUILDER_TESTS` config-gating + Ninja job pool) — remove in this feature or defer? → A: **Remove in this feature** — convert heavy builder tests to link the prebuilt lib, delete the gating + pool, verify CI RSS stays under runner limits (all legs, incl. gcc ~2× RSS + MSVC). (Confirms Assumption A4.) **[Delivery note — Gate A round 2 + PR #200: the deletion is CI-gated and DEFERRED; the #197 stopgap is retained in this PR pending a `workflow_dispatch`/push sanitizer-leg evidence run — see the evidence-path clarification below (this section) and A4 as revised.]**
- Q: Existing monolithic `#include <fixpp/<ver>/Builders.hpp>` path — keep working, or migrate consumers? → A: **Migrate** — the monolithic `Builders.hpp` include path is **replaced** by an `all.hpp` aggregator and is **not retained**. This is an accepted breaking change to the include layout, sound because the typed builder tier is opt-in and **not yet consumed in production** (no consumer has pinned the monolith). The library's own tests/goldens migrate to the new layout. (Overrides the earlier additive-compat Assumption; see FR-008.)
- Q: Read/reify tier — also split per-message in this feature, or defer? → A: **Out of scope** — this feature restructures the builder and validator tiers only; read/reify split deferred to a follow-up. (Confirms Assumption A5.)

### Session 2026-07-17 (Gate A round 1)

- Q: Install/export scope — make `install(TARGETS)`/export (installed external consumer) part of this feature, or narrow to build-tree + in-tree consumers? → A: **Narrow to build-tree + in-tree consumers only.** The six new libs are build-tree targets consumed by the library's own tests + in-tree examples; `install(TARGETS)`/`install(EXPORT)` (+ Conan/package-config) for an installed external consumer is **deferred** to a follow-up. For coherence, the slim generated headers are **not installed for external linking** while the targets are unexported (installing a declaration surface with no exported target to link is incoherent). US1/FR-002/FR-004 narrowed accordingly (resolves Gate A Codex-3; see cmake-targets.md R3).

### Session 2026-07-17 (Gate A round 1 — main-CI OOM findings)

- Q: Which CI legs must the #197 removal be gated on? → A: **The sanitizer-instrumented legs, including the python-bindings `asan`/`ubsan`/`tsan` matrix.** Two FAILED `main` CI runs (Tier1 `29565095705` / Tier2 `29565095713`, commit `df04e7df`) show three Tier-1 legs OOM (exit-143) building the heavy builder TUs — `linux-clang-asan` **and** the python-bindings `asan`/`ubsan`/`tsan` legs, which build the heavy TUs via the reused `linux-clang-debug` preset (`FIXPP_BUILD_HEAVY_BUILDER_TESTS=ON`). The flag-ON build passes uninstrumented but OOMs once ASan/UBSan lands on the ~78 MB monolith TUs, so the #197 stopgap is insufficient under sanitizers and the removal must be CI-gated on those legs (not on clang-only local `/speckit-verify`). Referenced from A4 and SC-006.

### Session 2026-07-17 (Gate A round 2)

- Q: The #197 removal is gated on the python-bindings sanitizer legs, but those legs are **path-gated** and **skip on the 078 PR** (`python_touched` filter, `PY_RE='^(bindings/python/|include/fix/c_api|.*\.i$)'`, `tier1.yml:131,628-631`; 078 touches none of those paths) — so a pre-merge PR gate on them passes vacuously. How is the removal's CI evidence actually obtained? → A: **Gate the option/pool deletion on an explicit `workflow_dispatch` (or feature-branch `push`) evidence run** captured on the post-split tree — any non-`pull_request` event sets `python_touched=true` unconditionally (`tier1.yml:141`) and `proceed=true` (`:150-153`), so it exercises the python-bindings `asan`/`ubsan`/`tsan` legs **and** all tier1 C++ sanitizer legs against the split. **Fallback:** split the deletion into a follow-up PR that lands only after a `push:main`/dispatch run has proven those legs on the merged split. **Do NOT permanently broaden `PY_RE`** (over-couples the ~7-min wheel build + four sanitizer legs to every future CMake change). Local `/speckit-verify` (clang-only + local) cannot supply this evidence. (Codex round-2 P2 / Opus CONFIRM; folded into A4, R8, cmake-targets.md, quickstart.md Scenario 7.)
- Q: SC-004/FR-009 say typed builder **and validator** output is "byte-identical" — but `validate_<Msg>` returns a validation success/error, not wire bytes. → A: **State the two invariants separately** — the **builder** (`build_<Msg>`) produces byte-identical **wire bytes** in linked and inline mode; the **validator** (`validate_<Msg>`) is **result-identical** (same success/error and same offending tag for the same `Args`), not a byte comparison. Corrected across FR-009, SC-004, data-model.md Entity 3, completeness-and-golden.md, quickstart.md Scenario 4a (Codex round-2 P3, locus-escalated).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Consumer compiles against a slim header and links a prebuilt builder library (Priority: P1)

A consumer (the library's own tests + an in-tree example, or a client built in-tree against the build-tree layout) uses one or a few typed `build_<Msg>` functions for a given FIX version. Instead of `#include`-ing a 74 MB monolithic header and paying ~3.6 GiB of compiler RSS per TU, the consumer includes a **slim declaration header** (message `Args` structs + shared `groups` + `build_` declarations) and **links** the prebuilt `fixpp_builders_<ver>`. Compilation of the consumer TU is cheap; the heavy builder bodies were compiled once when the library was built.

**Scope (Gate A round 1):** this feature targets **build-tree + in-tree consumers only** — `install(TARGETS)`/export for an *installed external* consumer is deferred to a follow-up, and the slim headers are not installed for external linking while the targets are unexported (see Clarifications / cmake-targets.md R3).

**Why this priority**: This is the core value of the feature — it removes the unavoidable per-consumer compile-cost cliff that motivated the issue. Without it, nothing else matters.

**Independent Test**: Build a small consumer TU that includes only the slim per-version builder header and calls one `build_<Msg>`, linking the prebuilt builder library. Verify it compiles with peak RSS and wall-time materially below including today's monolith, proportional to the message's group-plan closure (order-of-magnitude for v44; ~2.6–7.9× for v50sp2/vlatest depending on group density — see SC-001/L-078-1), links successfully, and produces a byte-identical wire message to the current typed builder output.

**Acceptance Scenarios**:

1. **Given** a consumer TU that includes the slim builder header for one version and calls one `build_<Msg>`, **When** it is compiled, **Then** peak compiler RSS is a small fraction of the monolithic-header baseline and the object does not contain the machine code for builders it did not call.
2. **Given** that consumer TU, **When** it is linked against the prebuilt `libfixpp_builders_<ver>`, **Then** it resolves the called `build_<Msg>` symbol and runs, producing wire output byte-identical to the pre-restructuring typed builder for the same inputs.
3. **Given** a consumer that needs two FIX versions, **When** it links both versions' builder libraries, **Then** each version's builders resolve from its own library and there is no symbol collision between versions.

---

### User Story 2 - Send-only consumer skips validator code entirely by not linking it (Priority: P1)

A client that only *builds and sends* messages (relying on the library's runtime inbound validation, #075) does not want the ~4,500 typed `validate_<Msg>` functions in its binary. Because validators live in a **separate** `libfixpp_validators_<ver>`, the client simply does not link that library and pays nothing for it — no code in its binary, no compile cost — while a client that wants the outbound pre-build required-field check links the validator library and gets it.

**Why this priority**: Validators are the heaviest part of the tier (~4,500 functions in `vlatest`) and are genuinely optional for clients relying on library-side inbound validation. Separating them is the highest binary-size lever after the builder split.

**Independent Test**: Build one consumer that links only the builder library and one that links both builder and validator libraries. Verify the builder-only binary contains zero `validate_<Msg>` machine code, and the both-linked binary resolves and runs a `validate_<Msg>` call correctly.

**Acceptance Scenarios**:

1. **Given** a consumer that links only `libfixpp_builders_<ver>`, **When** it is built, **Then** its binary contains no `validate_<Msg>` machine code and it has no link dependency on the validator library.
2. **Given** a consumer that links `libfixpp_validators_<ver>`, **When** it calls a `validate_<Msg>` on an `Args` value missing a required field, **Then** the call reports the missing required field consistently with the pre-restructuring typed validator behavior.
3. **Given** both libraries exist for a version, **When** the library is built in release, **Then** both `libfixpp_builders_<ver>` and `libfixpp_validators_<ver>` are always produced (link choice is the consumer's; the artifacts are always available).

---

### User Story 3 - Consumer force-inlines a few hot messages while linking the rest (Priority: P2)

A latency-sensitive client wants zero call-overhead for two or three hot messages (e.g. `NewOrderSingle`, `ExecutionReport`) but does not want to pay the compile cost of inlining everything. Per-message granularity plus a header-only inline mode (a macro) lets the client **mix**: link the bulk of the version's builders from the prebuilt library, and force-inline just the hot few by including them in inline mode.

**Why this priority**: Recovers the cross-TU inlining lost by the precompiled-library boundary, but only where the client asks for it. Valuable for hot paths, but not required for the core compile-cost win, so P2.

**Independent Test**: Build a consumer that links the builder library for all messages except one, which it includes in inline (header-only) mode. Verify the inlined message's body is emitted into the consumer's own object (inlinable at the call site) while every other `build_<Msg>` resolves from the library, and both produce byte-identical output.

**Acceptance Scenarios**:

1. **Given** a consumer that includes one message in header-only inline mode and links the rest, **When** it is compiled and linked, **Then** the inlined message's `build_` body is available for inlining at the call site and the non-inlined messages resolve from the prebuilt library, with no duplicate-symbol error.
2. **Given** the same message built via inline mode and via the linked library, **When** each is exercised with identical inputs, **Then** both produce byte-identical wire output.

---

### User Story 4 - A consumer that wants "everything" uses the aggregator (Priority: P2)

A consumer that wants a whole version's builders in one include uses an `all.hpp` aggregator that pulls in every per-message surface for that version. This replaces the old monolithic `fixpp/<ver>/Builders.hpp` include path, which is **removed** — an accepted breaking change to the include layout, sound because the typed builder tier is opt-in and not yet consumed in production.

**Why this priority**: Preserves the "give me everything" capability (now via `all.hpp`) and is the migration target for the library's own goldens/tests, but delivers no new consumer value beyond the split itself, so P2.

**Independent Test**: Build a consumer that includes the `all.hpp` aggregator for a version and verify every `build_<Msg>` for that version is available and produces byte-identical output to the pre-restructuring monolith; separately verify the old `Builders.hpp` include path no longer exists.

**Acceptance Scenarios**:

1. **Given** a consumer that includes the `all.hpp` aggregator for a version, **When** it is compiled after the restructuring, **Then** all of that version's `build_<Msg>` are available and produce byte-identical output to the pre-restructuring monolith.
2. **Given** the restructuring has landed, **When** a build references the old `fixpp/<ver>/Builders.hpp` monolithic include path, **Then** that path no longer resolves (it has been replaced by `all.hpp`), and the library's own goldens/tests have been migrated to the new layout.

---

### User Story 5 - The library's own test suite compiles the heavy builders once, not per test TU (Priority: P2)

Today the heavy builder-test translation units each re-parse and re-compile the monolithic builder header, which forced a CI stopgap (heavy-test config-gating + a Ninja job pool, from PR #197). With builders precompiled into a per-version library, the library's own tests **link** that prebuilt library, so the giant compile happens **once** at the library-build step rather than once per test TU.

**Why this priority**: Delivers the "instrument the app, not the library" win and is the basis for retiring the #197 stopgap, but is internal build-infra rather than consumer-facing value, so P2.

**Independent Test**: Rebuild the builder test suite after the restructuring; verify the test TUs link the prebuilt builder library instead of re-including the monolith, and that peak build-time RSS for those TUs drops below the thresholds that necessitated the #197 gating.

**Acceptance Scenarios**:

1. **Given** the builder test suite, **When** it is built after the restructuring, **Then** the test TUs link the prebuilt builder library and their peak compile RSS no longer exceeds the hosted-runner limits that motivated the #197 heavy-test gating.
2. **Given** the restructuring is complete, **When** the CI build configuration is reviewed, **Then** the #197 heavy-test stopgap is either removed (if this feature's scope includes removal) or explicitly recorded as a named follow-up — see Assumptions.

---

### Edge Cases

- **Deterministic emission across the file split.** Splitting one monolith into per-message files + a shared groups file + an aggregator multiplies the number of generated files. Regeneration MUST be deterministic (stable file set, stable ordering, byte-stable content) and the checked-in goldens MUST be regenerated to match, or the codegen-determinism / git-cleanliness gate will hang or false-red (a repeated prior hazard).
- **One-definition-rule at the link/inline boundary.** `FIXPP_BUILDERS_HEADER_ONLY[_<Msg>]` (and the validator twin) is a **program-wide per-message switch**: a given message is force-inlined in *every* TU of the program that references it, **or** left in link mode (resolved from the archive) in *every* TU — never both within one program. The *same* message forced inline in one TU while linked (strong external) in another TU of the same program is **unsupported** — an ODR violation under [dcl.inline]/4 (IFNDR, no diagnostic required) — and MUST NOT be relied upon. DIFFERENT-message mixing (force-inline a chosen subset, link the rest) IS ODR-safe and never yields two divergent definitions of the same `build_<Msg>`. See quickstart.md Scenario 4d for the full contract.
- **Shared `groups` included from many per-message headers.** The deduped `G_<no_tag>Args` group headers (per-plan `groups/<Plan>.hpp`, each `#pragma once`-guarded) must be included exactly once effectively even when a TU pulls in many per-message headers that overlap in their closure.
- **Cross-toolchain / prebuilt-binary consumers.** The precompiled `build_<Msg>(span, Args const&)` boundary is only sound when fixpp is built from source in the client's toolchain (shared `Args` layout). A client on a different toolchain or wanting runtime version selection uses the existing C-ABI runtime builder — that path is out of scope here (see Assumptions).
- **A version with no typed groups / admin-only version.** A version that emits no builders (e.g. `vt11` admin-only) must be handled as a coherent absence — no builder/validator target and no aggregator are emitted (no `vt11/all.hpp`, no `fixpp_builders_vt11`); the no-emit itself is the coherent, non-broken outcome.
- **Both libraries always built even when a consumer links neither.** The release library build always produces both artifacts per version; a downstream that links neither still builds and links cleanly.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The codegen MUST emit, per app-bearing version, a **slim declaration surface** for typed builders — each message's `Args` plus the shared deduped `groups` plus a `build_<Msg>` declaration — such that including it does not require parsing the builder function bodies.
- **FR-002**: The codegen/build MUST compile each version's `build_<Msg>` bodies once into a precompiled **`libfixpp_builders_<ver>`** library artifact that consumers link, rather than requiring every consumer TU to compile the bodies. *(Scope, Gate A round 1: build-tree + in-tree consumers; `install(TARGETS)`/export for an installed external consumer is deferred — R3.)*
- **FR-003**: The codegen/build MUST emit and always compile each version's `validate_<Msg>` functions into a **separate** precompiled **`libfixpp_validators_<ver>`** library artifact, distinct from the builder library. *(Confirmed 2026-07-17: always-built separate lib, not a codegen switch — see Clarifications / Assumption A1.)*
- **FR-004**: Both `libfixpp_builders_<ver>` and `libfixpp_validators_<ver>` MUST be **always built** in a release library build; the choice of whether to use them MUST be **purely link-time** for the consumer (link the builder library, the validator library, both, or neither). *(Scope, Gate A round 1: the linkable targets are build-tree + in-tree; installed-external-consumer export is deferred — R3.)*
- **FR-005**: A consumer that links only the builder library MUST NOT carry any `validate_<Msg>` machine code and MUST have no link-time dependency on the validator library.
- **FR-006**: The system MUST provide a **per-message header-only inline mode**, selectable per message (e.g. via a documented macro), that emits the `build_<Msg>` body for inlining at the call site instead of resolving it from the prebuilt library.
- **FR-007**: A consumer MUST be able to **mix** modes within one build — link the bulk of a version's builders from the prebuilt library while force-inlining a chosen subset — without duplicate-symbol errors or divergent definitions.
- **FR-008**: The system MUST provide an **`all.hpp` aggregator** per version that exposes every message's surface for that version. The pre-existing monolithic `fixpp/<ver>/Builders.hpp` include path MUST be **replaced by** `all.hpp` and is **not** retained (an accepted breaking change to the include layout — the typed builder tier is opt-in and not yet consumed in production). The library's own goldens and tests MUST be migrated to the new layout.
- **FR-009**: After the restructuring, for the same inputs and across all shipped app-bearing versions (`v44`, `v50sp2`, `vlatest`), regardless of whether a message is reached via the linked library or inline mode: the typed **builder** (`build_<Msg>`) MUST produce **byte-identical wire bytes** to the pre-restructuring output, and the typed **validator** (`validate_<Msg>`) MUST be **result-identical** — the same success/error (and the same offending tag) for the same `Args`. (`validate_<Msg>` returns a validation result, not wire bytes, so its equivalence is result-identity, not byte-identity — Gate A round 2.)
- **FR-010**: Codegen MUST emit the split file set **deterministically** — a stable set of files, stable internal ordering, and byte-stable content across regeneration runs — and the checked-in goldens MUST be regenerated to the split layout so the codegen-determinism and git-cleanliness gates pass.
- **FR-011**: The restructuring MUST NOT change the library **core** — no change to `src/`, `capi/`, `bindings/`, or the C-ABI (frozen at 1.5.0); the typed builder/validator tier is client-facing and the core send/build path is unaffected.
- **FR-012**: The shared deduped `groups` group headers (post-077 `G_<no_tag>Args`, one per-plan header per deduped plan) MUST be emitted once per version and safely includable from many per-message headers in one TU (include-guarded, no redefinition).

### Key Entities *(include if feature involves data)*

- **Slim builder declaration header**: Per version (and per message under the split), the compile-cheap surface a consumer includes — message `Args` structs, shared `groups`, and `build_`/`validate_` declarations — deliberately excluding function bodies.
- **`libfixpp_builders_<ver>`**: Precompiled per-version artifact holding all `build_<Msg>` bodies; always built; linked by builder consumers.
- **`libfixpp_validators_<ver>`**: Precompiled per-version artifact holding all `validate_<Msg>` bodies; always built; linked only by consumers that want the outbound pre-build required-field check.
- **Per-message inline unit**: The emitted body for one message usable in header-only inline mode, selectable per message and mixable with the linked libraries.
- **Aggregator ("everything") include**: The **replacement whole-version surface** (the migration target for `Builders.hpp`, which is removed by FR-008) that exposes a whole version's builders (and, per configuration, validators). It preserves the pre-restructuring **output behavior** (byte-identical wire), **not** the include path — it is not include-path-compatible ("backwards-compatible" is reserved for output/behavior here).
- **Shared `groups` region**: The deduped `G_<no_tag>Args` structs, one per-plan header (`groups/<Plan>.hpp`, post-077 dedup) per version, each included once and shared across the per-message headers whose closure needs it.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001** *(AMENDED 2026-07-17 — measured; see `.specify/decisions/078-precompiled-builder-libs-verify.md` `## compile-bench` and L-078-1)*: A consumer TU that uses one typed builder for one version compiles with peak RSS **proportional to that message's transitive group-plan closure**, materially below the ~3.6 GiB monolithic-header baseline — but the original **universal** "≥ order-of-magnitude (≤ ~0.36 GiB) for *any* single-message include" target is **not achievable on the large versions (v50sp2/vlatest) under 077's value-semantics `Args` API**. A message's `<Msg>Args` embeds its group `Args` **by value**, so the full transitive group-plan closure must be a *complete* type at the include site; the deduplicated group graph is densely connected, so a typical large-version message's closure is ~100–400 of ~560 plans and cannot be trimmed by header layout alone. **Measured** (per-plan-header layout, clang, `-fsyntax-only`, peak RSS): **v44 — all messages ~0.21 GiB (≥ order-of-magnitude below the ~3.6 GiB baseline; MET)**; **v50sp2/vlatest — median message ~0.47 GiB (~7.9×), common message (NewOrderSingle, 233-plan closure) ~0.88 GiB (~4.2×), group-densest (TradeCaptureReport, 393-plan closure) ~1.42 GiB (~2.6×)** — each materially below the 1.573 GiB cost of pulling the whole per-version `groups.hpp`, but **not order-of-magnitude**. The residual is inherent to the value-semantics typed-`Args` design; a forward-declared / handle-based `Args` that would meet the universal target is a distinct API change **deferred to a follow-up** (out of scope — see L-078-1 / research "Option 3"). The per-plan-header split is retained because it delivers the ~2–3× typical-message reduction over pulling the full `groups.hpp` at no correctness cost (ODR-safe, deterministic).
- **SC-002**: A consumer links only the builders it uses per version: a binary that calls a subset of a version's builders contains **only the machine code for the called messages** (plus shared groups), not the full ~18–20 MiB `.text` of the whole version's builder set.
- **SC-003**: A send-only consumer that links no validator library carries **zero `validate_<Msg>` machine code** (baseline: ~4,500 validator functions exist in `vlatest`).
- **SC-004**: For every shipped app-bearing version and every message, whether reached via the linked library or inline mode: the typed **builder** produces **byte-identical wire bytes** vs the pre-restructuring output (0 diffs), and the typed **validator** is **result-identical** (same success/error and same offending tag for the same `Args`). The validator returns a validation result, not wire bytes — its check is result-identity, not a byte comparison (Gate A round 2).
- **SC-005**: A consumer that includes the `all.hpp` aggregator for a version gets **every** `build_<Msg>` for that version producing **byte-identical** output to the pre-restructuring monolith (0 diffs), and the library's own goldens/tests build clean on the new layout with the old `Builders.hpp` path removed.
- **SC-006**: The library's own heavy builder-test translation units compile with **peak RSS below the hosted-runner limit** that necessitated the #197 heavy-test gating, because they link the prebuilt library instead of recompiling the monolith. The binding measurement is on the **sanitizer-instrumented legs (ASan/UBSan)** — including the **python-bindings sanitizer matrix** (`asan`/`ubsan`/`tsan`), which build the heavy TUs via the reused `linux-clang-debug` preset — since the main-CI OOM (exit-143) reproduces only once instrumentation lands on the monolith TUs; the uninstrumented budget is not the binding constraint (see Clarifications — main-CI OOM findings).
- **SC-007**: Codegen regeneration is **deterministic** — regenerating the split layout twice yields byte-identical files and leaves the working tree clean against the checked-in goldens.

## Assumptions

- **A1 — Validators always-built separate library, link-time opt-in (memory) vs. codegen-switch default-OFF (issue #198 text).** The public issue #198 proposes emitting typed validators only behind a codegen/CMake switch (default OFF). The user's later same-day decision record supersedes this: validators are **always emitted and always compiled** into a separate `libfixpp_validators_<ver>`, and the opt-in is **purely link-time** (the library boundary is the opt-in). **Confirmed 2026-07-17 (see Clarifications): always-built separate lib with purely link-time opt-in** (not codegen-switch-gated emission). The tradeoff accepted: always-built pays to compile ~4,500 `validate_` functions into a release library even for send-only clients, buying link-simplicity and always-available artifacts.
- **A2 — Precompiled library is the PRIMARY approach; per-message headers are the inline-mode vehicle.** Issue #198 leads with per-message headers (proposal 2) and lists the shared library as "consider" (proposal 3). The user's decision promotes the **precompiled per-version library** to primary, with per-message `.inl`/header emission serving the header-only **inline** mode. **Confirmed 2026-07-17: both the precompiled libs (default) and the per-message header-only inline mode with mixing ship in this feature, from one generation** (see Clarifications).
- **A3 — ABI boundary: built-from-source client toolchain.** The precompiled `build_<Msg>(span, Args const&)` boundary is sound because fixpp is built from source in the client's toolchain (shared `Args` layout). Cross-toolchain consumers and true runtime version selection use the existing C-ABI runtime builder (`src/capi/message_write.cpp`) and are **out of scope** here.
- **A4 — #197 CI stopgap retained pending CI evidence (deletion deferred).** This feature makes the library's own heavy builder tests **link the prebuilt library**; the 077/PR-197 stopgap (`FIXPP_BUILD_HEAVY_BUILDER_TESTS` config-gating + the Ninja job pool) is **retained** in this PR — its removal is gated on the CI evidence run below, landing either in-PR after that evidence or as a named follow-up (see Evidence path). `/plan` MUST verify that with the stopgap gone the heavy TUs still fit the hosted-runner memory limits on **every** CI leg — including the **sanitizer-instrumented legs** (`linux-clang-asan`/`ubsan`/`tsan`), the **python-bindings `asan`/`ubsan`/`tsan` legs** (which build the heavy TUs via the reused `linux-clang-debug` preset — a census gap the main-CI OOM findings exposed, see Clarifications), gcc (~2× clang RSS) and MSVC (where `/bigobj` and the pool had also been helping) — before the removal lands; if a leg regresses, the minimum still-needed guard is re-scoped, not silently dropped. **The removal must be gated on ACTUAL CI (those sanitizer legs), not on `/speckit-verify`, which is clang-only + local and does not exercise the python-bindings sanitizer matrix.** **Evidence path (Gate A round 2):** those python-bindings legs are **path-gated** and **skip on the 078 PR** (`python_touched=false` — 078 touches none of `PY_RE='^(bindings/python/|include/fix/c_api|.*\.i$)'`, `tier1.yml:131,628-631`), so a PR gate on them passes vacuously. The deletion is therefore gated on an explicit **`workflow_dispatch` (or feature-branch `push`) evidence run** — any non-`pull_request` event sets `python_touched=true` unconditionally (`tier1.yml:141`), exercising the python-bindings + all tier1 C++ sanitizer legs on the post-split tree — **or**, as fallback, split the deletion into a follow-up PR that lands only after a `push:main`/dispatch run has proven those legs. Do not permanently broaden `PY_RE`. See Clarifications — Session 2026-07-17 (Gate A round 2) and research.md R8.
- **A5 — Read/reify tier split is OUT of scope (confirmed 2026-07-17).** Issue #198 lists extending the split to the read/reify tier as "optional, for symmetry." This feature scopes to the **builder and validator** tiers only; read/reify restructuring is deferred to a follow-up.
- **A6 — Ordering: #198 before #196.** This restructuring lands before #196 (v42 typed builders) so that #196 emits into the split layout directly rather than into the monolith and migrating later. #196's read-tier group-detection fix is independent and can proceed in parallel. (Sequencing note; not a requirement of this feature.)
- **A7 — App-bearing versions in scope.** The restructuring applies to the versions that currently emit typed builders: `fixpp::{v44, v50sp2, vlatest}`. Admin-only versions (e.g. `vt11`) emit no builders and emit no target/aggregator at all (coherent absence, not an empty target); the codegen skips them cleanly. v42 is descoped to #196 (also emits nothing).
- **A8 — "Consumers" = client applications and the fixpp maintainers.** The stakeholders here are downstream client applications linking the typed tier and the fixpp maintainers building and testing it; the library core does not consume the typed builders (verified: zero `Builders.hpp` includes / zero `vX::build_`/`validate_` calls in `src/`, `include/`, `capi/`, `bindings/`).

## Normative References

**None.** This feature is a pure **implementation-layout restructure** of the typed builder/validator tier already delivered by feature 077 — it splits one monolithic `Builders.hpp` into precompiled per-version libraries + a slim declaration surface + per-message header-only inline mode. It adds **no** new FIX coverage and **no** new `OFFICIAL` catalogue row; the on-wire output is byte-identical to 077 (FR-009 / SC-004). The governing prior artifact is feature 077 (`specs/077-builder-args-dedup/`) and its tracking issue #198; there is no external standard or spec text this feature is the first to implement. (Section included per Constitution Article VI §5, which requires a Normative References section in every `/specify` artifact regardless of content.)
