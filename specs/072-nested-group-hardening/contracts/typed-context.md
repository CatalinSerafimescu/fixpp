# Contract: typed depth-≥2 pushed membership context (Part B)

## Surface
Generated typed group accessors (from `tools/codegen/fixpp-codegen/emit_messages.cpp`) + `dictionary_driven_validator` (`include/fixpp/wire/validator.hpp`). No public-signature change; behavior correction only.

## The invariant
When descending from a parent group entry into a nested group, the child view's **stored** membership context MUST equal `parent.group_ctx.pushed(child_no_tag)` — so the next descent resolves a grandchild group's members under the full path. This mirrors the C-ABI cursor (`message_read.cpp:506`), which is already correct.

## Change (exactly one push site)
- **Emitter view-mint** `emit_messages.cpp:270-271`: emit the returned `group_view<G_c>`'s base context with `group_ctx = ctx_.group_ctx.pushed(<c>)` (c = nested `no_tag`, already in scope).
- **Unchanged / invariant**:
  - `nested_group_slices(...)` call arg stays `ctx_.group_ctx` (correct slicing context for the immediate child).
  - `group_view::operator[]` stays a verbatim `base_ctx_` copy — pushing there double-pushes.
  - `parser.hpp:309` depth-1 seed unchanged.
  - C-ABI cursor unchanged (do NOT un-push).

## Validator (L-063-3)
`validator.hpp` Step-3 group walk currently hardcodes `root_path = {}` → every nested lookup misses. Make it nesting-aware with a pushed context so typed read and strict validation agree at depth-≥2.

## Observable behavior
- **Pre-fix**: a depth-3 grandchild-group member read on a dict whose grandchild membership differs context-vs-bare resolves the WRONG (bare-fallback) member.
- **Post-fix**: resolves the correct context-scoped member; agrees with the C-ABI (SC-003/SC-004).
- **Shipped dicts**: byte-identical runtime results (all inert), verified by clean reconfigure across debug+san+coverage (SC-005). No checked-in golden.
- **C-ABI**: no exported-symbol/header/enum/version delta (SC-006).

## Witness (mutation-proven, TDD)
Real `v44::MassQuote` (`296→295→555`) + hand-built `table_view` with divergent context-vs-bare registration at `555`. RED on pre-fix emitter output, GREEN post-fix. Bypasses `XmlLoader::load_*` (independent of Part A). See research D-B6.
