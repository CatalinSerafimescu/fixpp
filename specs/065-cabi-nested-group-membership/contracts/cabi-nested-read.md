# Contract: C-ABI nested repeating-group read (065)

Observable behavior of the C-ABI nested-group read after this feature. No exported symbol, signature, header, or error-enum change — only the *output values* on the trailing-member layout become correct, and the absent/empty distinction is preserved.

## Functions (signatures UNCHANGED — GA-frozen)

```c
fixpp_error_t fixpp_group_get_nested_group(const fixpp_group_t* g, size_t i,
                                           uint16_t nested_tag,
                                           const fixpp_group_t** nested_out,
                                           size_t* nested_count_out);
fixpp_error_t fixpp_group_get_field_string (const fixpp_group_t* g, size_t i, uint16_t tag, const char**   v, size_t* len);
fixpp_error_t fixpp_group_get_field_int    (const fixpp_group_t* g, size_t i, uint16_t tag, int64_t* v);
fixpp_error_t fixpp_group_get_field_double (const fixpp_group_t* g, size_t i, uint16_t tag, double* v);
fixpp_error_t fixpp_group_get_field_decimal(const fixpp_group_t* g, size_t i, uint16_t tag, fixpp_decimal_t* v);
```

## C1 — Trailing outer member excluded from the last nested instance (the fix)

**Given** an outer-group entry containing a multi-entry nested group `N` followed by a declared outer-level member tag `T` (e.g. FIX44 `NoLegs(555)` entry: `NoLegSecurityAltID(604)` multi-entry nested group, then `LegQty(687)`):

- `fixpp_group_get_nested_group(outer, i, N, &nested, &nc)` → `FIXPP_ERR_OK`, `nc` = true count of `N` instances.
- For **every** nested index `j` in `[0, nc)`, including the last: `fixpp_group_get_field_*(nested, j, T, …)` → **`FIXPP_ERR_TAG_NOT_FOUND`** (`T` is an outer member, not a member of `N`). *(Was `FIXPP_ERR_OK` + wrong value for `j == nc-1` — the defect.)*
- `fixpp_group_get_field_*(nested, j, m, …)` for each genuine member `m` of `N` → `FIXPP_ERR_OK` + that instance's own value.

## C2 — Trailing outer member still reachable at the OUTER index (no regression)

**Given** the same message:
- `fixpp_group_get_field_*(outer, i, T, …)` → `FIXPP_ERR_OK` + `T`'s value (`T` genuinely belongs to the outer entry). Unchanged.

## C3 — Absent vs empty-count distinction (preserved)

- `nested_tag` **absent** from the outer entry → `fixpp_group_get_nested_group(...)` → `FIXPP_ERR_TAG_NOT_FOUND`. *(pinned: `NestedGroupAbsentTag`)*
- `nested_tag` **present with count 0** (or count field is last, no delimiter follows) → `FIXPP_ERR_OK`, `nc == 0`. *(pinned: `NestedGroupEmptyGroupCountLastField`)*

## C4 — Common case & multi-instance (byte-identical)

- Nested group with **no** trailing outer member → every nested field read identical to current behavior.
- Single-entry nested group; per-outer-entry distinct nested instances (`get_nested_group(outer,0,N)` vs `(outer,1,N)` return that entry's own nested slices) → unchanged. *(pinned: existing multi-instance / per-entry-distinct tests)*

## C5 — Membership-collision trailing tag (SCOPE LIMITATION — not a delivered guarantee)

- The membership predicate compares by tag **value** (`consume_group_extent` breaks on the first non-member, `offset_table.cpp:469`). So a trailing outer tag `T` whose value equals a **non-delimiter member** of `N` would be **absorbed** into `N`'s last instance — 065 does **not** exclude it. The delivered correctness is confined to dictionaries where parent/nested **scalar member tags are DISJOINT**, which holds for every shipped dict (mechanically verified for the 6 vendored group-bearing dicts): there `T` is a distinct non-member that ends the walk.
- The same-value collision on a user/dialect dictionary is a **documented, already-tracked limitation** — L-062-3 (`spec/behaviors-and-limitations.md:1693`) / L-063-4 (`:1701`), issue **#180** ("harden dictionary census: parent/child scalar-member disjointness"). 065 neither solves nor re-opens it.

## C5a — Depth scope (depth-1 delivered; depth-≥2 out of scope)

- C1–C4 are asserted for **depth-1** nesting (a trailing member after a *single* nested group — issue #179); C5 is a scope limitation, not an asserted guarantee. The cursor carries the arithmetically-correct full membership path, so a depth-2 C-ABI descent is membership-bounded (not positional) and fail-closed; but depth-≥2 nested-in-nested read *correctness/equivalence* is not asserted or witnessed here. A pre-existing typed-path context-threading gap at depth ≥ 2 (research Decision 7) is surfaced as a follow-up (candidate `L-065-1`), not fixed. SC-005 equivalence (C7) is scoped to the depth-1 layout.

## C6 — Bounded nesting; over-limit caught at the OUTER read (unreachable on the nested path)

- The span-returning `nested_group_slices` collapses an `err_group_too_large` from `consume_group_extent` to an **empty span** (`group_slices()`, `offset_table.cpp:583/621/625`), so the nested read does **not** independently return a group-limit error — the earlier "nested read yields a defined `err_group_too_large` mapping" claim was false.
- The over-limit condition is nonetheless **unreachable** on the public C-ABI nested path, so no silent truncation can occur: (a) the only way to obtain a nested cursor is `fixpp_msg_get_group`, whose mandatory outer-first `group_slices()` pre-walk already recurses through the nested group during the outer extent computation (`consume_group_extent` recurses at `offset_table.cpp:476-482`); and (b) the C-ABI always parses with the default `Config` (no tuning surface for `max_group_entries_per_instance`), so the sub-table's caps equal the root's. An over-limit nested group therefore fails the **OUTER** read first (`fixpp_msg_get_group` → `FIXPP_ERR_TYPE_MISMATCH`, no cursor minted); the consumer never descends. The old scanner's coded `WIRE_LIMIT_EXCEEDED` guard (untested / `LCOV_EXCL`) is **dropped**, acceptable only on this unreachability. No over-read past the outer entry slice.

## C7 — Invariants

- **No exported-symbol / header / enum / version change**: `tests/abi/golden/fixpp_capi_symbols.txt` and `tools/capi_freeze.sha256` are byte-identical (SC-003).
- **Zero global heap**: the nested sub-table and its slices come from the per-message arena (SC-004).
- **C-ABI ≡ C++ typed path** on the represented **depth-1** layout: both exclude `T` from `N`'s extent (SC-005), because both call `consume_group_extent` via the same primitive under the same `{msg_type,[outer]}` context. Verified by a real-`as_table_view()` witness (FR-011). (Depth-≥2 equivalence excluded — C5a.)
