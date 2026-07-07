# Contract: Group Membership & Instance Extent (063)

Observable contracts the fix must satisfy. These are behavioural (verifiable without prescribing the chosen option); the Option A/B decision surface is noted where it affects the interface.

## C-1 · Membership acceptance (Defect A)
- **Given** a dictionary loaded from real XML where a `NumInGroup` tag N heads groups with differing membership across messages,
- **When** the parser evaluates `group_member_fn(dict, N, delim)` for a wire message whose N-group begins with field `delim`,
- **Then** it returns `true` for every field that is a legitimate member of N in that message's usage (in particular FIX44: `group_member_fn(295, 299)=true`), so `OffsetTable::group()` accepts the group instead of returning `required_field_missing`.
- **Interface impact**: Option B — no signature change (`group_member_fn_t` unchanged; membership is a per-no_tag superset). Option A — `group_member_fn_t` gains a `context` argument.

## C-2 · Instance extent, nesting-aware (Defect B)
- **Given** an outer group instance containing a nested group with N > 1 entries,
- **When** `OffsetTable::group()`/`group_slices()` compute the outer instance extent,
- **Then** the extent encloses all N nested entries (no truncation at the 2nd nested entry), and the computation performs **zero heap allocation**.

## C-3 · Real-dictionary multi-entry nested read (SC-001, composes C-1 + C-2)
- **Given** real `FIX44.xml` via `Dictionary::as_table_view()` and a MassQuote wire frame with `QuoteSet[0]` holding two `QuoteEntries` (distinct 132/133),
- **When** `msg.quote_sets()[0].quote_entries()` is read,
- **Then** `.size() == 2`, and `entry[0]`/`entry[1]` each return their own exact 299/132/133 through the (superset) generated accessors.

## C-4 · C-ABI stability (SC-005)
- **Given** the built C-ABI,
- **When** the symbol golden (`tests/abi/golden/fixpp_capi_symbols.txt`) and header freeze (`tools/capi_freeze.sha256`) are verified,
- **Then** both pass unchanged — no exported group symbol (`fixpp_msg_get_group`, `fixpp_group_*`, …) added/changed — while `fixpp_msg_get_group`'s runtime output for a nested group is now correct.

## C-5 · Preserve prior-correct behaviour (FR-006)
- Single-entry nested reads, count-of-zero groups (`B-004-7`), flat (non-nested) groups, and benign same-membership tag reuse continue to return identical results (regression-guarded, each mutation-sensitive where applicable).

## C-6 · Census completeness (FR-002)
- **Given** all nine runtime XMLs,
- **When** the loader-faithful census runs,
- **Then** it enumerates every `NumInGroup` tag reused with differing membership (≥ the 12 FIX44 / 22 FIX50SP2 observed), each with a discriminating guard; and (Option-B gate) reports any OFFICIAL-message group whose trailing wire-neighbour ∈ its no_tag union (the over-extension set — must be empty for Option B to ship, else Option A / per-tag exception).

## Decision surface (for Gate A)
- **Primary question**: Defect-A **Option B (union-per-no_tag, mirror codegen)** vs **Option A (per-context membership)**. Recommendation: Option B, gated on C-6's over-extension set being empty for the OFFICIAL messages. Contracts C-1…C-5 hold under either.
