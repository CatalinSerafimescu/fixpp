# Implementation Plan: FIXT version-registry serviceability guard at open()

**Branch**: `042-fixt-version-serviceability-guard` | **Date**: 2026-06-17 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/042-fixt-version-serviceability-guard/spec.md`

## Summary

Extend the existing FQ-1 FIXT config-load guard in `Session::open()` (`src/session/session.cpp:940-943`)
with a **third disjunct**: when a FIXT session (`begin_string == "FIXT.1.1"`) has a configured
`default_appl_ver_id` AND a non-null engine `app_version_registry_`, but the registry **cannot serve**
that configured version (`!app_version_registry_->get(*cfg_.default_appl_ver_id).has_value()`), `open()`
fails closed with `error::invalid_session_config` — before any observable state mutation or wire
emission. This converts the L-033-5 operator footgun (a misconfigured acceptor that opens "successfully"
then silently `Reject`s every inbound FIXT Logon) into a loud config-load failure, matching QuickFIX,
which validates the FIXT `AppDataDictionary` at config-load.

**Role-agnostic** (resolved in `/speckit-clarify`): the guard fires for both acceptor and initiator,
at the shared `open()` path, with no role gate — grounded in QuickFIX-cpp `SessionFactory::create`
(verified role-independent) and orthogonal to 033 FR-004a (which scopes only the *runtime
peer-advertised-version* refuse to the acceptor; this guard validates *this* side's own configured
default). The inbound peer-`1137` serviceability check (`session.cpp:2186-2200`) stays live and unchanged.

**No production behavior change beyond the new fail-closed disposition** — no new public wire field,
error slot, config field, codegen output, or C-ABI surface; reuses `error::invalid_session_config`
(already returned by the sibling FQ-1 / security-profile / credential open() guards) and the existing
engine `version_registry::get()` (`const noexcept`, same call already used inbound at :2195).

## Technical Context

**Language/Version**: C++23 (clang + gcc), CMake + Conan presets
**Primary Dependencies**: Boost.Asio (`Session::open()` is an awaitable / `co_return`s), GoogleTest,
`dict::version_registry` (engine-lifetime, non-owning handle on the session)
**Storage**: N/A (no store-format change)
**Testing**: GoogleTest via ctest; `tests/session/test_fixt_logon_establishment.cpp` (`FixtSetup`
fixture); lcov DA/BRDA per `[const §IX.1]` for the new guard branch
**Target Platform**: Linux (primary); platform-agnostic (pure config-load logic, no syscalls)
**Project Type**: Library (FIX engine) — session layer
**Performance Goals**: N/A — one extra `noexcept` registry lookup on the cold `open()` path (per
session lifetime, not per message); zero hot-path impact
**Constraints**: Fail-closed before observable mutation; no new error code; no new config; no wire or
C-ABI change; correctly-configured FIXT + all non-FIXT sessions byte-identical
**Scale/Scope**: ~1 production line (one disjunct) + comment update + witnesses; no new public surface

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **§ Gate A trigger (Article XVII §1 / pipeline step 4)**: **TRIGGERED.** Touches session
  establishment / error semantics — `open()` gains a new fail-closed disposition (a previously-opening
  misconfigured FIXT session now fails at config-load). → **Gate A required** (unlike 039, which made
  no behavior change). `/clarify` (done) + `/analyze` are likewise mandatory for the session/error-
  semantics trigger set (Article XVI §3 / §4).
- **§XII.5 no-implicit-default / fail-closed establishment**: the new guard is fail-closed and rejects
  a misconfiguration before any mutation — strengthens, never weakens, the no-silent-establishment
  posture. → **PASS (aligned).**
- **§X.1 frozen C-ABI (MAJOR=1)**: no C-ABI surface touched. → **PASS.**
- **§IX.1 coverage (lcov DA/BRDA)**: the new guard branch (the third disjunct's true arm) MUST be
  covered by a RED-first / mutation-tested witness using a real non-null registry missing the dict.
  → **Obligation (T-witness); no waiver anticipated.**
- **§XV.9 no-`std::mutex`-in-awaitable corpus gate**: `Session::open()` is in the awaitable corpus.
  The new call (`version_registry::get`, `const noexcept`) and its header (`version_registry.hpp`) are
  **already included** in `session.cpp` and already called inside the awaitable `on_inbound_frame`
  (:2195) — **no new include edge**, so no risk of dragging `std::mutex` into the `open()` closure
  ([[feedback_awaitable_header_mutex_include_edge]]). → **PASS (no new edge); verify with an unfiltered
  Tier-1 build.**
- **Article VI §5 (Normative References)**: spec.md cites FIX Session-layer FIXT establishment +
  QuickFIX config-load behavior; a Normative References pointer will be carried (per the 039 Gate A
  round-1 lesson). → **PASS.**

No constitutional violations requiring Complexity Tracking.

## Project Structure

### Documentation (this feature)

```text
specs/042-fixt-version-serviceability-guard/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions (mechanism, role scope, error disposition, byte-identity)
├── data-model.md        # Phase 1 — no new entities; the guard truth table + serviceability predicate
├── contracts/
│   └── open-serviceability-guard.md   # Phase 1 — the open() validation-contract extension
├── quickstart.md        # Phase 1 — how to run the witnesses (RED-first / mutation steps)
├── checklists/
│   └── requirements.md  # spec quality checklist (passing, 0 markers)
└── tasks.md             # Phase 2 (/speckit-tasks — not created here)
```

### Source Code (repository root)

```text
src/session/session.cpp                         # the FQ-1 guard (:940-943) — add the 3rd disjunct +
                                                #   update the :923-939 comment (new arm IS reachable,
                                                #   opposite the documented-unreachable #2 null-registry arm)
tests/session/test_fixt_logon_establishment.cpp # witnesses: unserviceable-own-default open() → invalid_session_config
                                                #   (acceptor + initiator); serviceable-default open() unaffected;
                                                #   inbound peer-1137 reject still live (SC-003 non-deadness)
spec/behaviors-and-limitations.md               # L-033-5 (:1438) → RESOLVED with code ref (close-out)
spec/feature-catalogue.md                       # 042 row / amend S-020 disposition (close-out)
spec/coverage-index.md                          # new guard-branch coverage entry (close-out)
```

**Structure Decision**: Library layout (existing). No new modules, headers, or public surface. The
change is a single guard disjunct at the existing shared `open()` validation block, plus witnesses and
the L-033-5 close-out docs. One implementer invocation (the `phase-implementer-sonnet` runaway-scope
guard; the feature is a single coherent concern, not a multi-US bundle).

## Complexity Tracking

> No Constitution Check violations — section intentionally empty.

## Gate A

Pending (pipeline step 4 — runs after this plan, before `/speckit-tasks`). Gate A is **required** for
042 (session establishment / error-semantics behavior change). Records will be archived under
`research/reviews/{codex,opus}_042-fixt-version-serviceability-guard_gate_a*.md` and the disposition
recorded in `phases/phase-4/session/042-fixt-version-serviceability-guard.md`.
