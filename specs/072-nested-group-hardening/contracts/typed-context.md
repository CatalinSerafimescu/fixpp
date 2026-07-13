# Contract: typed depth-≥2 pushed membership context (Part B)

## Surface
Generated typed group accessors (from `tools/codegen/fixpp-codegen/emit_messages.cpp`) + `dictionary_driven_validator` (`include/fixpp/wire/validator.hpp`). No public-signature change; behavior correction only.

## The invariant
When descending from a parent group entry into a nested group, the child view's **stored** membership context MUST equal `parent.group_ctx.pushed(child_no_tag)` — so the next descent resolves a grandchild group's members under the full path. This mirrors the C-ABI cursor (`message_read.cpp:506`), which is already correct.

**Lookup key vs stored context (do not conflate).** `table_view::group_member_tags(msg_type, parent_path, no_tag)` takes the queried group's own `no_tag` **separately** from `parent_path` (which excludes it). So grandchild group `555` resolves under the **lookup key** `(msg_type="i", parent_path=[296,295], no_tag=555)` — where `msg_type` is the wire `MsgType` value `"i"`. The triple `[296,295,555]` is the **stored context** minted onto the returned view, which a *further* (depth-4) descent uses as *its* `parent_path`; it is NOT the key under which 555's own members resolve. A witness that registers/queries `555` under `parent_path=[296,295,555]` gets a permanent miss.

## Change (exactly one push site)
- **Emitter view-mint** `emit_messages.cpp:270-271`: emit the returned `group_view<G_c>`'s base context with `group_ctx = ctx_.group_ctx.pushed(<c>)` (c = nested `no_tag`, already in scope).
- **Unchanged / invariant**:
  - `nested_group_slices(...)` call arg stays `ctx_.group_ctx` (correct slicing context for the immediate child).
  - `group_view::operator[]` stays a verbatim `base_ctx_` copy — pushing there double-pushes.
  - `parser.hpp:309` depth-1 seed unchanged.
  - C-ABI cursor unchanged (do NOT un-push).

## Validator (L-063-3) — from-scratch recursive rewrite, HARD GATE
`validator.hpp` Step-3 group walk currently hardcodes `root_path = {}` (`:204`) → every nested lookup queries the root context and misses; the in-code comment (`:185`) says it "has no notion of nesting depth", and it detects instance boundaries with a hand-rolled `seen_in_instance` heuristic (`:258-267`), **not** `consume_group_extent`. Making it nesting-aware is therefore a **recursive rewrite**, not a push tweak.

- **(a) Algorithm**: replace the single flat pass over `msg.offsets().entries()` with a **recursive descent threading a path stack**, **query-before-push** — for a candidate nested group `G` at the current descent, **first** query `group_first_field`/`group_member_tags(msg_type, current_parent_path, G)` (where `current_parent_path` **excludes** `G`'s own `no_tag`) in place of the root `(msg_type, {}, G)`; **then**, only to recurse into `G`'s nested children, push `G` (`current_parent_path.pushed(G)`) and pop on exit. Mirrors `consume_group_extent`, which queries the current group under `ctx` **before** forming `child = ctx.pushed(no_tag)` for its children (and the accessor's `group_ctx.pushed(no_tag)`, pushed after minting the child view, never before the current-group lookup). Per-level instance counts come from the slicer extent, not `seen_in_instance`.
- **(b) Witness**: named + mutation-proven — `ValidatorNestedMembership_Depth2ContextMissUnderFlatWalk` (RED on the flat walk, GREEN after).
- **(c) SPLIT-TRIGGER**: if the rewrite proves unbounded / materially larger than the FR-007 accessor fix, FR-010 splits to its own follow-up feature; 072 ships Part A + the accessor half of Part B (FR-007/FR-008/FR-011), and L-063-3 stays "open/tracked" (NOT "fixed"). Decision recorded in `plan.md` before `/tasks`.

## Observable behavior
- **Pre-fix**: a depth-3 grandchild-group member read on a dict whose grandchild membership differs context-vs-bare resolves the WRONG (bare-fallback) member.
- **Post-fix**: resolves the correct context-scoped member; agrees with the C-ABI (SC-003/SC-004).
- **Shipped dicts**: byte-identical runtime results (all inert), verified by clean reconfigure across debug+san+coverage (SC-005). No checked-in golden.
- **C-ABI**: no exported-symbol/header/enum/version delta (SC-006).

## Witness (mutation-proven, TDD)
Real `v44::MassQuote` (`296→295→555`) + hand-built `table_view` with divergent context-vs-bare registration at `555`: context store keyed `add_group_member_ctx("i", [296,295], 555, …)` under the **wire `MsgType` value `"i"`** (NOT "MassQuote" — the runtime read queries by wire value, so the name would false-green the witness) vs the legacy bare `add_group_member(555, …)` store. RED on pre-fix emitter output, GREEN post-fix. Bypasses `XmlLoader::load_*` (independent of Part A). See research D-B6.
