# Phase 1 Data Model: Runtime validator required-presence scoping (fixpp#201)

This feature adds no new persisted or wire data. The "entities" are the in-memory dictionary-derivation structures the fix scopes and the additive per-group store.

## Message-level required set

- **What**: the set of top-level tags the validator requires to be present in a message (validator Step 2 flat-probes it against top-level fields).
- **Source**: `required_out` accumulated in `expand_field_list` (both loaders), surfaced via `Dictionary::required_fields(msg_type)` / `table_view::required_fields()`.
- **Rule (after fix)**: a tag is in the set iff `required='Y'` AND it is not enclosed by any group. Component-usage handling unchanged (Phase 0: no optional-component-with-required-field configuration exists). Header/trailer required fields retained.
- **Invariant**: for every message in every dict, this set equals the independent raw-XML oracle's expected set (census, exact equality). Legacy behavior for non-group messages is unchanged.

## Per-group required-member set

- **What**: for a repeating group (keyed by its `no_tag`), the members that are `required='Y'` *inside* the group.
- **Storage**: additive `table_view` stores — bare `group_required_members_[no_tag]` and context-scoped `group_ctx_[(msg_type, parent_path, no_tag)].required_members` (063 context precedent). Populated by `dictionary.cpp::as_table_view()` where `rule==Required && group_no_tag==no_tag`.
- **Accessors**: `group_required_members(no_tag)` and `group_required_members(msg_type, parent_path, no_tag)` (context first, bare fallback).
- **Consumer**: the validator's `consume_group` checks each group instance carries every member in this set.
- **Relationship**: complementary to the existing `group_members_` store (all members) — this is the required subset.

## Group-instance membership check state

- **What**: transient per-instance state in `consume_group` — a bitmask of which required members have been seen in the current group instance.
- **Bounds**: guarded ≤64 members (bitmask width); the delimiter tag is pre-marked. Fail-closed → `wire_required_field_missing(offending_tag)` on the first missing required member.
- **Lifetime**: stack-local per group instance; no allocation, no persistence.

## Field-ref context (unchanged, referenced)

- `FieldRef::rule` (own `required`/`presence`), `FieldRef::group_no_tag` (which group a member belongs to, 0 = top level), `FieldRef::component_index` (lexical component pointer). The fix reads `group_no_tag` (already present); it does **not** need to extend `FieldRef` — component-usage required-ness is not threaded (vacuous per Phase 0).

## Census entities (test-only)

- **Expected required set**: `(msg_type) → set<tag>` from an independent raw-XML walker (group members excluded).
- **Shipped required set**: `(msg_type) → set<tag>` from `Dictionary::required_fields()` and the codegen IR top-level list.
- **QuickFIX required set**: `(msg_type) → set<tag>` from quickfix-cpp 1.16.0 `DataDictionary`, captured to a checked-in golden (9 QuickFIX dicts).
