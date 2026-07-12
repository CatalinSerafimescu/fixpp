# Contract: load-time nested==parent delimiter guard (Part A)

## Surface
`XmlLoader::load` / `XmlLoader::load_from_string` (`include/fixpp/dict/xml_loader.hpp`). Behavior added at `LoaderState::finalize()`.

## Precondition
A dictionary XML is being loaded. After parse + component expansion, `groups_` holds one `GroupDef` per raw `<group>` (global first-seen per `no_tag`).

## Behavior
For every group `g` with `g.parent_group_no_tag != 0`:
- let `parent = groups_[group_index_by_no_tag_[g.parent_group_no_tag]]`.
- **If `g.first_field_tag == parent.first_field_tag`** → **throw** `dict::group_delimiter_collision_error` with a message naming `g.no_tag`, the shared delimiter, and `parent.no_tag`.
- Else continue.

## Postcondition
- **Conforming dict** (all 9 shipped): load completes unchanged; the guard is a no-op. (SC-001)
- **Colliding dict**: `load_*` throws `dict::group_delimiter_collision_error` (catchable as `dict::xml_parse_error` and specifically); **no** `table_view` is produced; no crash/UB/mis-split. (SC-002)

## Explicit non-coverage (recorded residuals — FR-013)
- A hand-built `table_view` / non-`load_*` `Dictionary` is not re-validated (FR-005a).
- Global first-seen dedup: a collision only in a non-first-seen context of a reused `no_tag` is unguarded (FR-005b).
- Scalar-member disjointness (L-062-3) is **not** load-enforced — assertion-only (FR-004), and unpinnable for FIX40/41/42 (FR-013d).

## Census invariants (permanent, `reused_tag_census_test.cpp`)
- **FR-001**: over the structural `groups_` walk, no nested delimiter == parent delimiter, for every runtime dict; asserts > 0 groups observed per dict (non-vacuous — covers FIX40/41/42).
- **FR-002**: no scalar member tag shared parent↔nested child, for every dict it claims to cover; > 0 member-sets examined; FIX40/41/42 recorded as unpinned residual if not structurally recoverable.
