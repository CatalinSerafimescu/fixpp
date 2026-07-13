# Contract: load-time nested==parent delimiter guard (Part A)

## Surface
`XmlLoader::load` / `XmlLoader::load_from_string` (`include/fixpp/dict/xml_loader.hpp`). Behavior added at `LoaderState::finalize()`.

## Precondition
A dictionary XML is being loaded. After parse + component expansion, `groups_` holds **one `GroupDef` per distinct `no_tag`** — **globally first-seen-deduped** (`expand_field_list:486`), with post-component-expansion delimiters. It is **NOT** one record per raw `<group>` element; the guard therefore checks the first-seen parent/delimiter relationship per `no_tag` (residual FR-005b). The FR-001 census does not rely on this seam — it uses a strictly stronger all-contexts raw walk (see Census invariants).

## Behavior
For every group `g` with `g.parent_group_no_tag != 0`:
- let `parent = groups_[group_index_by_no_tag_[g.parent_group_no_tag]]`.
- **If `g.first_field_tag == parent.first_field_tag`** → **throw** `dict::group_delimiter_collision_error` with a message naming `g.no_tag`, the shared delimiter, and `parent.no_tag`.
- Else continue.

**Error type**: `dict::group_delimiter_collision_error` derives from `dict::xml_parse_error` and **reuses the inherited `code()`** (`dict_xml_parse_failed`) — **no new `fixpp::core::error` variant is appended** (avoids the `error_message()` `-Wswitch`/`-Werror` break and the `test_020_error_completeness.cpp` slot-132 flip). Callers discriminate it **by catch type** (`catch (dict::group_delimiter_collision_error&)`); `code()` is non-virtual, so a base-`xml_parse_error` catch cannot discriminate via `code()`. See research D-A1.

## Postcondition
- **Conforming dict** (all 9 shipped): load completes unchanged; the guard is a no-op. (SC-001)
- **Colliding dict**: `load_*` throws `dict::group_delimiter_collision_error` (catchable as `dict::xml_parse_error` and specifically); **no** `table_view` is produced; no crash/UB/mis-split. (SC-002)

## Explicit non-coverage (recorded residuals — FR-013)
- A hand-built `table_view` / non-`load_*` `Dictionary` is not re-validated (FR-005a).
- Global first-seen dedup: a collision only in a non-first-seen context of a reused `no_tag` is unguarded (FR-005b).
- Scalar-member disjointness (L-062-3) is **not** load-enforced — assertion-only (FR-004), and unpinnable for FIX40/41/42 (FR-013d).

## Census invariants (permanent, `reused_tag_census_test.cpp`)
- **FR-001**: over the **raw per-`<group>` walk with parent-delimiter threading + `<component>`-ref expansion** (deliberately NOT the guard's first-seen `groups_` seam — the census must observe *all* membership contexts, research D-A3), no nested delimiter == parent delimiter, for every runtime dict; asserts > 0 group-declaration sites observed per dict (non-vacuous — covers FIX40/41/42). Each group's delimiter MUST be resolved **post-component-expansion** (first field of the fully-expanded member list, mirroring `expand_field_list`), NOT the first literal `<field>` child — else a component-leading group (e.g. `NoQuoteEntries`) is dropped or mis-compared. Any context the walk cannot structurally reach is a recorded residual (FR-013e), bounding the all-contexts claim.
- **FR-002**: no scalar member tag shared parent↔nested child, for every dict it claims to cover; > 0 member-sets examined; FIX40/41/42 recorded as unpinned residual if not structurally recoverable.
