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

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Consumer compiles against a slim header and links a prebuilt builder library (Priority: P1)

A client application uses one or a few typed `build_<Msg>` functions for a given FIX version. Instead of `#include`-ing a 74 MB monolithic header and paying ~3.6 GiB of compiler RSS per TU, the client includes a **slim declaration header** (message `Args` structs + shared `groups` + `build_` declarations) and **links** the prebuilt `libfixpp_builders_<ver>`. Compilation of the client TU is cheap; the heavy builder bodies were compiled once when the library was built.

**Why this priority**: This is the core value of the feature — it removes the unavoidable per-consumer compile-cost cliff that motivated the issue. Without it, nothing else matters.

**Independent Test**: Build a small consumer TU that includes only the slim per-version builder header and calls one `build_<Msg>`, linking the prebuilt builder library. Verify it compiles with an order-of-magnitude lower peak RSS and wall-time than including today's monolith, links successfully, and produces a byte-identical wire message to the current typed builder output.

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

### User Story 4 - Existing consumers keep working unchanged (Priority: P2)

A consumer that today includes the monolithic `fixpp/<ver>/Builders.hpp` (or wants "everything") must not break. An aggregator header preserves today's behavior so the split is purely additive.

**Why this priority**: Backwards compatibility is a hard requirement consistent with the project's additive / byte-identical discipline (077, 076, 069), but it gates adoption rather than delivering the new value, so P2.

**Independent Test**: Take an existing consumer that includes the monolithic header and builds today; after the restructuring, rebuild it unchanged (via the aggregator) and verify it still compiles and produces byte-identical output.

**Acceptance Scenarios**:

1. **Given** a consumer that includes the "everything" aggregator for a version, **When** it is compiled after the restructuring, **Then** all of that version's `build_<Msg>` (and, when the aggregator includes them, `validate_<Msg>`) are available exactly as before, producing byte-identical output.
2. **Given** the pre-existing monolithic include path, **When** the restructuring lands, **Then** that include path continues to resolve (kept as, or aliased to, the aggregator) so existing client code does not have to change.

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
- **One-definition-rule at the link/inline boundary.** A message linked from the library and the same message forced inline in a consumer TU must not produce a duplicate-symbol error, and mixing must never yield two divergent definitions of the same `build_<Msg>`.
- **Shared `groups` included from many per-message headers.** The deduped `G_<no_tag>Args` region must be included exactly once effectively (include-guarded) even when a TU pulls in many per-message headers.
- **Cross-toolchain / prebuilt-binary consumers.** The precompiled `build_<Msg>(span, Args const&)` boundary is only sound when fixpp is built from source in the client's toolchain (shared `Args` layout). A client on a different toolchain or wanting runtime version selection uses the existing C-ABI runtime builder — that path is out of scope here (see Assumptions).
- **A version with no typed groups / admin-only version.** A version that emits no builders (e.g. `vt11` admin-only) must produce a coherent (possibly empty) library target and aggregator, not a broken build.
- **Both libraries always built even when a consumer links neither.** The release library build always produces both artifacts per version; a downstream that links neither still builds and links cleanly.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The codegen MUST emit, per app-bearing version, a **slim declaration surface** for typed builders — each message's `Args` plus the shared deduped `groups` plus a `build_<Msg>` declaration — such that including it does not require parsing the builder function bodies.
- **FR-002**: The codegen/build MUST compile each version's `build_<Msg>` bodies once into a precompiled **`libfixpp_builders_<ver>`** library artifact that consumers link, rather than requiring every consumer TU to compile the bodies.
- **FR-003**: The codegen/build MUST emit and always compile each version's `validate_<Msg>` functions into a **separate** precompiled **`libfixpp_validators_<ver>`** library artifact, distinct from the builder library. *(Mechanism divergence from issue #198 text — see Assumptions A1; lead item for `/speckit-clarify`.)*
- **FR-004**: Both `libfixpp_builders_<ver>` and `libfixpp_validators_<ver>` MUST be **always built** in a release library build; the choice of whether to use them MUST be **purely link-time** for the consumer (link the builder library, the validator library, both, or neither).
- **FR-005**: A consumer that links only the builder library MUST NOT carry any `validate_<Msg>` machine code and MUST have no link-time dependency on the validator library.
- **FR-006**: The system MUST provide a **per-message header-only inline mode**, selectable per message (e.g. via a documented macro), that emits the `build_<Msg>` body for inlining at the call site instead of resolving it from the prebuilt library.
- **FR-007**: A consumer MUST be able to **mix** modes within one build — link the bulk of a version's builders from the prebuilt library while force-inlining a chosen subset — without duplicate-symbol errors or divergent definitions.
- **FR-008**: The split MUST be **additive and backwards-compatible**: an aggregator include path that exposes "everything" for a version MUST be provided, and the pre-existing monolithic include path MUST continue to resolve (kept as, or aliased to, the aggregator) so existing consumer code does not change.
- **FR-009**: Typed builder and validator output after the restructuring MUST be **byte-identical** to the pre-restructuring output for the same inputs, across all shipped app-bearing versions (`v44`, `v50sp2`, `vlatest`), regardless of whether a message is reached via the linked library or inline mode.
- **FR-010**: Codegen MUST emit the split file set **deterministically** — a stable set of files, stable internal ordering, and byte-stable content across regeneration runs — and the checked-in goldens MUST be regenerated to the split layout so the codegen-determinism and git-cleanliness gates pass.
- **FR-011**: The restructuring MUST NOT change the library **core** — no change to `src/`, `capi/`, `bindings/`, or the C-ABI (frozen at 1.5.0); the typed builder/validator tier is client-facing and the core send/build path is unaffected.
- **FR-012**: The shared deduped `groups` region (post-077 `G_<no_tag>Args`) MUST be emitted once per version and safely includable from many per-message headers in one TU (include-guarded, no redefinition).

### Key Entities *(include if feature involves data)*

- **Slim builder declaration header**: Per version (and per message under the split), the compile-cheap surface a consumer includes — message `Args` structs, shared `groups`, and `build_`/`validate_` declarations — deliberately excluding function bodies.
- **`libfixpp_builders_<ver>`**: Precompiled per-version artifact holding all `build_<Msg>` bodies; always built; linked by builder consumers.
- **`libfixpp_validators_<ver>`**: Precompiled per-version artifact holding all `validate_<Msg>` bodies; always built; linked only by consumers that want the outbound pre-build required-field check.
- **Per-message inline unit**: The emitted body for one message usable in header-only inline mode, selectable per message and mixable with the linked libraries.
- **Aggregator ("everything") include**: The backwards-compatible surface that exposes a whole version's builders (and, per configuration, validators), preserving the pre-restructuring monolithic-include behavior.
- **Shared `groups` region**: The deduped `G_<no_tag>Args` structs (one region per version post-077) included once and shared across per-message headers.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A consumer TU that uses one typed builder for one version compiles with **peak memory at least an order of magnitude below** the monolithic-header baseline (baseline: ~3.6 GiB RSS just to `#include` one version's monolith).
- **SC-002**: A consumer links only the builders it uses per version: a binary that calls a subset of a version's builders contains **only the machine code for the called messages** (plus shared groups), not the full ~18–20 MiB `.text` of the whole version's builder set.
- **SC-003**: A send-only consumer that links no validator library carries **zero `validate_<Msg>` machine code** (baseline: ~4,500 validator functions exist in `vlatest`).
- **SC-004**: Typed builder and validator output is **byte-identical** to the pre-restructuring output for every shipped app-bearing version and every message, whether reached via the linked library or inline mode (0 diffs).
- **SC-005**: An existing consumer that included the monolithic header **builds unchanged** after the restructuring (via the preserved include path / aggregator) and produces byte-identical output.
- **SC-006**: The library's own heavy builder-test translation units compile with **peak RSS below the hosted-runner limit** that necessitated the #197 heavy-test gating, because they link the prebuilt library instead of recompiling the monolith.
- **SC-007**: Codegen regeneration is **deterministic** — regenerating the split layout twice yields byte-identical files and leaves the working tree clean against the checked-in goldens.

## Assumptions

- **A1 — Validators always-built separate library, link-time opt-in (memory) vs. codegen-switch default-OFF (issue #198 text).** The public issue #198 proposes emitting typed validators only behind a codegen/CMake switch (default OFF). The user's later same-day decision record supersedes this: validators are **always emitted and always compiled** into a separate `libfixpp_validators_<ver>`, and the opt-in is **purely link-time** (the library boundary is the opt-in). This spec is written on the memory's decision. **This is the single most load-bearing choice and the lead question for `/speckit-clarify`** — confirm always-built-separate-lib (this spec) vs. codegen-switch-gated emission (issue text). The tradeoff: always-built pays to compile ~4,500 `validate_` functions into a release library even for send-only clients, buying link-simplicity and always-available artifacts.
- **A2 — Precompiled library is the PRIMARY approach; per-message headers are the inline-mode vehicle.** Issue #198 leads with per-message headers (proposal 2) and lists the shared library as "consider" (proposal 3). The user's decision promotes the **precompiled per-version library** to primary, with per-message `.inl`/header emission serving the header-only **inline** mode. This spec follows the memory. Confirm during `/clarify`.
- **A3 — ABI boundary: built-from-source client toolchain.** The precompiled `build_<Msg>(span, Args const&)` boundary is sound because fixpp is built from source in the client's toolchain (shared `Args` layout). Cross-toolchain consumers and true runtime version selection use the existing C-ABI runtime builder (`src/capi/message_write.cpp`) and are **out of scope** here.
- **A4 — #197 CI stopgap removal is a scope decision to confirm.** The user's decision says #198 "supersedes" the 077/PR-197 heavy-test config-gating + Ninja job pool. Whether *this* feature actively **removes** that stopgap, or leaves removal as a named follow-up once the tests link the prebuilt library, is a scope boundary to confirm at `/clarify`. Default assumption: this feature makes the tests link the prebuilt library (delivering the win) and removes the now-unnecessary heavy-test gating as part of the same change.
- **A5 — Read/reify tier split is OUT of scope.** Issue #198 lists extending the split to the read/reify tier as "optional, for symmetry." This feature scopes to the **builder and validator** tiers only; read/reify restructuring is deferred.
- **A6 — Ordering: #198 before #196.** This restructuring lands before #196 (v42 typed builders) so that #196 emits into the split layout directly rather than into the monolith and migrating later. #196's read-tier group-detection fix is independent and can proceed in parallel. (Sequencing note; not a requirement of this feature.)
- **A7 — App-bearing versions in scope.** The restructuring applies to the versions that currently emit typed builders: `fixpp::{v44, v50sp2, vlatest}`. Admin-only versions (e.g. `vt11`) emit no builders and must still yield coherent (possibly empty) targets. v42 is descoped to #196.
- **A8 — "Consumers" = client applications and the fixpp maintainers.** The stakeholders here are downstream client applications linking the typed tier and the fixpp maintainers building and testing it; the library core does not consume the typed builders (verified: zero `Builders.hpp` includes / zero `vX::build_`/`validate_` calls in `src/`, `include/`, `capi/`, `bindings/`).
