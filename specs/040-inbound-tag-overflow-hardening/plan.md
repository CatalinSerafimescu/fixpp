# Implementation Plan: Inbound tag-overflow hardening

**Branch**: `040-inbound-tag-overflow-hardening` | **Date**: 2026-06-15 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/040-inbound-tag-overflow-hardening/spec.md`

## Summary

A real (TLS-auth-bounded) security fix: a forged multi-digit FIX tag overflows `uint32` and wraps to
a small value, aliasing a security-relevant tag (34/49/52/56/1137) at five live-inbound hand-rolled
tag scanners — one of which (`scan_frame_header`) already ships a **defective** guard. The fix is a
single shared, `constexpr`, in-loop `0xFFFF` bound helper (the `framer.cpp:120` / `session.cpp:1588`
correct shape), called at all five sites; each site keeps its existing disposition. `build_replay_frame`
(stored own-outbound) is a justified exclusion.

## Technical Context

**Language/Version**: C++23 (clang + gcc), CMake + Conan presets
**Primary Dependencies**: none new (leaf helper header; `fixpp::wire` + `fixpp::session`)
**Storage**: N/A
**Testing**: GoogleTest via ctest — per-scanner wrap-and-continue unit witnesses + helper boundary
unit test; live cross-engine witness DEFERRED (038 L-038-2 family)
**Target Platform**: Linux (primary); the scanners are platform-independent
**Project Type**: Library (FIX engine) — wire codec + session inbound
**Performance Goals**: zero measurable regression — the helper is `constexpr`/`[[gnu::always_inline]]`
and compiles to the same code the hand-rolls emit (one compare + multiply-add per digit)
**Constraints**: `noexcept`, no allocation, no new error codes, no wire/C-ABI/config/codegen change;
bound = `0xFFFF` (16-bit tag width); detect in-loop before any wrap
**Scale/Scope**: 1 new leaf header (the helper) + 5 call-site edits + ~6 witnesses + 1 exclusion note

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Gate A trigger (Article XVII)**: **REQUIRED** — touches the wire-codec + session inbound decode
  surface. Gate A runs after this plan.
- **§XV.* (banned patterns / safety)**: the helper is `constexpr noexcept`, no allocation, no
  `std::mutex`; it does not appear in any awaitable closure in a way that trips §XV.9. → **PASS.**
- **Layering (`tools/check_layers.py` / architecture.md)**: the helper is a **wire-layer leaf header**
  (`include/fixpp/wire/tag_scan.hpp`); `fixpp::session` already depends on `fixpp::wire`, so the
  session scanners including it does not invert layers. The Gate-B fixer must re-run
  `tools/check_layers.py` after the new header lands (per `feedback_gate_b_check_layers_post_fixer`).
  → **PASS (to be re-verified post-implementation).**
- **§IX.1 coverage (lcov DA/BRDA)**: the new helper's overflow branch + every site's new reject
  branch MUST be covered by the wrap-and-continue witnesses (no new zero-hit lines). → **PASS (by
  FR-007 witnesses).**
- **§X.1 frozen C-ABI**: untouched. → **PASS.**
- **No new error codes / config / codegen** (FR-009). → **PASS.**

No violations requiring Complexity Tracking.

## Project Structure

### Documentation (this feature)

```text
specs/040-inbound-tag-overflow-hardening/
├── plan.md
├── research.md          # Phase 0 — helper design, per-site application, census
├── data-model.md        # Phase 1 — the (value, ok) helper contract + per-site disposition table
├── contracts/
│   └── tag-scan-helper.md   # the bounded-tag-parse helper contract
├── quickstart.md
├── checklists/requirements.md
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
include/fixpp/wire/tag_scan.hpp              # NEW — the shared constexpr bounded-tag accumulate helper
src/wire/offset_table.cpp:160-176            # site 1 (Index) — replace post-loop check w/ in-loop helper
include/fixpp/wire/parser.hpp:333-346        # site 2 (Scan) — add in-loop helper (none today)
src/session/admin_messages.cpp:255-266       # site 3 — interpret_logon, add in-loop helper
src/session/engine.cpp:349-353               # site 4 — scan_first_frame_ids, add in-loop helper
src/session/session.cpp:1493-1496            # site 5 — scan_frame_header, REPLACE defective >429496729U guard
src/session/session.cpp:1639                 # site 6 — build_replay_frame: add justified-exclusion comment (FR-008)

tests/
├── wire/      # helper boundary unit test; offset_table + parser wrap-and-continue witnesses
└── session/   # interpret_logon, scan_first_frame_ids, scan_frame_header wrap-and-continue witnesses

spec/behaviors-and-limitations.md            # B&L row: forged-tag overflow hardening + the site-6 exclusion note
```

**Structure Decision**: One new wire-layer leaf header holds the only copy of the bound logic; the
five call sites each replace their `tag = tag*10 + digit` step with a call to the helper and keep
their existing disposition on overflow. This directly removes the per-site divergence that produced
the `scan_frame_header` defect (root cause), with minimal control-flow churn per site.

## Complexity Tracking

> No Constitution Check violations — section intentionally empty.

## Gate A

- Round 1 applied 2026-06-15: Codex (gpt-5.5) P1=0 P2=0 P3=1; Opus post-judging P1=0 P2=0 P3=1 →
  **CONVERGED** (P1==0 AND P2==0), 0 rewrites. Both reviewers independently re-swept and confirmed the
  completeness claim (5 live-inbound scanners + `build_replay_frame` excluded; no 6th, incl. a
  non-idiom `from_chars`/`strtoul`/… sweep), the helper boundary arithmetic (incl. compiling the
  `static_assert`), all 5 per-site dispositions, the layering (wire leaf header), and the 038
  SendingTime-guard regression vector (`scan_frame_header` case 52 → `h.sending_time` → 038 MaxLatency
  guard). P3 folded in: research.md D-3 rows 4/5 spelled as `if/else-if` (digit-check before helper,
  preserving the helper's `'0'..'9'` precondition) + FR-007a non-digit negative witness at sites 4/5
  (prevents a future fold-into-helper "simplification" from accepting a non-numeric tag token).
  Reviews: `research/reviews/codex_040-inbound-tag-overflow-hardening_gate_a_review.md`,
  `research/reviews/opus_040-inbound-tag-overflow-hardening_gate_a_adversarial_review.md`.
