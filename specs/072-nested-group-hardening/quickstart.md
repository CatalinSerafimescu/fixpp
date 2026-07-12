# Quickstart / Validation Guide: 072-nested-group-hardening

Prerequisites: configured build tree in the library submodule; Linux/Clang for coverage; the codegen emitter builds.

All commands run with cwd = `research/G19-fix-fpml-iso20022/library`.

## Part A — census + load guard

1. **Census pins trip on a bad dict (TDD red first)**
   - Temporarily introduce a nested==parent-delimiter and a shared parent/child scalar member in a throwaway inline XML; assert FR-001/FR-002 logic flags it. Then the permanent assertions over the 9 shipped dicts pass (0 collisions), **non-vacuously** (each dict reports > 0 groups; FIX40/41/42 included via the structural walk).
   - `ctest -L dictionary` (grouped) → `reused_tag_census` green; assertions observed > 0 groups per dict.

2. **Load-rejection witness**
   - A small inline XML with a nested group whose `first_field_tag` equals its parent's, loaded via `XmlLoader{}.load_from_string(xml, &arena)`, MUST throw `dict::group_delimiter_collision_error` (catchable as `dict::xml_parse_error`), produce no view, and not crash.
   - A conforming variant loads fine.

3. **Shipped dicts unaffected**: full `dictionary` suite green; all 9 dicts still load.

## Part B — typed depth-3 pushed context

1. **Depth-3 discrimination witness (mutation-proven RED first)**
   - Build a `v44::MassQuote` read over a **hand-built `table_view`** whose grandchild group `555` context store `(MassQuote,[296,295],555)` and bare `555` store carry *different* member sets.
   - On the **pre-fix** emitter output the witness reads the wrong bare-fallback member → RED. Apply the emitter push (`emit_messages.cpp:270-271`) → GREEN.
   - If constructible on the same hand-built dict, assert the C-ABI depth-3 read agrees (SC-004).

2. **Validator witness**: a depth-≥2 membership scenario where the flat-context validator previously missed → passes post-fix.

3. **Clean codegen reconfigure (no stale header)**
   - `rm -rf <build>/_codegen` && reconfigure so the changed emitter regenerates all 4 codegen-input dicts.
   - Recompile + run the typed-read + validation suites across **debug, sanitizer, coverage** configs; shipped-dict runtime results byte-identical to pre-fix (SC-005).
   - Re-index CodeGraph after regen: `codegraph index --force` (structural change).

## Gates
- `/speckit-analyze` (mandatory) → Gate A (mandatory) → `/tasks` → `/implement` (TDD) → `/simplify` → `/speckit-verify` → Gate B.
- `/speckit-verify` must show: ASan/UBSan/TSan green, coverage ≥95/85 on `dict/` + `wire/` (uncovered error paths assessed), ABI hygiene no-delta (SC-006), clean-reconfigure shipped-dict parity (SC-005).
