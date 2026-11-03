# Implementation Plan: FIXT application version, the validation half

**Branch**: `099-fixt-appl-version-validation` | **Date**: 2026-11-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/099-fixt-appl-version-validation/spec.md`

## Summary

A FIXT.1.1 session stops using one dictionary for everything. Per frame it selects the transport dictionary for an admin message and,
for an application message, the dictionary of its resolved version (1128, else the negotiated 1137). The frame scan, the validator and
the `fromApp` parse all use that one table. A validating session refuses a message no dictionary can answer. The session dictionary
keeps answering its own version, so an overlay survives. The engine-held registry builds each table once. C, Python and TOML gain what a
FIXT session needs.

Decisions and rejected alternatives: [research.md](./research.md). Shapes: [data-model.md](./data-model.md). Surface:
[contracts/surface.md](./contracts/surface.md). Witnesses: [contracts/witnesses.md](./contracts/witnesses.md).

## Technical Context

**Language/Version**: C++23; C for the ABI surface; Python through SWIG.

**Primary Dependencies**: existing only (pugixml, asio, toml++). No new dependency, no pin.

**Storage**: N/A.

**Testing**: GoogleTest, real `Session` over raw frames, `tests/capi`, `bindings/python/tests`, the parent's interop harness.

**Target Platform**: Linux (clang, gcc), MSVC.

**Project Type**: library.

**Performance Goals**: non-FIXT inbound and validate within the paired band; one branch added per inbound frame. No per-message heap on FIXT.

**Constraints**: non-FIXT behaviour byte-identical. C ABI additive. Tables built at most once per dictionary per engine.

**Scale/Scope**: one session-path change touching every inbound arm; four C symbols; one TOML key.

## Constitution Check

| Article | Check | Status |
|---|---|---|
| §I.1, §XVIII.1 | FIXT.1.1 session and application layers are v1.0 scope | Pass |
| §VI | S-026 traceability; new rows carry witnesses | Pass, tasks |
| §VIII.2, §VIII.5 | paired bench on the hot path; no per-message allocation | Pass, W-16, R-14 |
| §IX | coverage assessed in verify | Pass, tasks |
| §X.1, §X.6, §X.7 | C ABI additive MINOR; `/clarify`, `/analyze`, Gate A and the owner's `/plan` sign-off | **Sign-off pending** |
| §XV.13 | hybrid dictionary model unchanged; S stays authoritative for its version | Pass |
| §XVII | Gate A before tasks; verify; Gate B | Pass, pipeline |
| Article XX | no article changes; no amendment | Pass |

No violation to justify.

## Project Structure

```text
specs/099-fixt-appl-version-validation/
├── plan.md  research.md  data-model.md  quickstart.md  spec.md
├── contracts/{surface,witnesses}.md
└── checklists/requirements.md

include/fixpp/dict/{version_registry,version_profile,dictionary,table_view}.hpp
include/fixpp/wire/{validator,parser}.hpp
include/fixpp/session/{session,session_config}.hpp
include/fix/c_api/{session,engine,message,version}.h
src/dictionary/{version_registry,dictionary}.cpp
src/session/{session.cpp,peek_fixt_route.hpp}
src/capi/{config,engine,message_read}.cpp, capi_internal.hpp
src/config/selector_resolver.cpp
bindings/python/{fixpp.i,tests/}
tests/session/test_099_*.cpp, tests/capi/capi_099_*, tests/config/test_099_*
spec/, brain/, parent trackers
```

## Phases

1. **Registry and tables** (E-1, E-2, E-3): W-15, W-17.
2. **Validator** (E-4, envelope): existing validator tests stay green; W-3's unit half.
3. **Peek and selection** (E-6, E-7), `open()` check (R-2): W-8.
4. **Arms**: scan, validate gate, interpret, dispatch (R-7, R-8, R-9): W-1 to W-7, W-9, W-16, W-19.
5. **Accessor** (E-5): W-10. Outbound pin: W-11.
6. **C ABI, Python, TOML**: W-12, W-13, W-14.
7. **Live cell**: W-18, per the owner's ruling.
8. **#651 comments, records, brain**.

Phases 1–3 precede 4; 5 and 6 are parallel after 4. Mutants run after each phase's witnesses, from a separate worktree.

## Owner sign-off (`[const §X.6]`) — pending

The `/plan` sign-off, including research R-13's live-cell option and research R-8's pre-Logon ending on a frame that is also invalid.
