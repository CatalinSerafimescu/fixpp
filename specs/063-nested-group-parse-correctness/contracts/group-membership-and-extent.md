# Contract: Group Membership & Instance Extent (063)

Observable contracts the fix must satisfy. These are behavioural; Gate-A Round 1 resolved the design to **Option A (exact context-scoped membership)**, so the interface note under C-1 is fixed, not a fork.

## C-1 · Membership acceptance, context-scoped (Defect A — Option A)
- **Given** a dictionary loaded from real XML where a `NumInGroup` tag N heads groups with **differing** membership across messages,
- **When** the parser evaluates the context-scoped predicate `group_member_fn(dict, context, N, delim)` — where `context = (msg_type, bounded parent-no_tag-path)` — for a wire message whose N-group begins with field `delim`,
- **Then** it returns `true` exactly for the members of N **as used in that message/context** (in particular FIX44 MassQuote: `group_member_fn(MassQuote-context, 295, 299)=true`), and **`false` for a field that belongs only to some *other* same-tagged group** — so `OffsetTable::group()` (a) accepts the group instead of returning `required_field_missing` and (b) does not swallow a trailing non-member field into the slice. Returning THIS context's member set — **not another same-tagged group's, and not a union superset of all of them**.
- **Interface impact**: `group_member_fn_t` (`offset_table.hpp:29`) gains a `context` argument (public C++, non-ABI; clarify-sanctioned). The membership store re-keys from bare `no_tag` to the context key.

## C-2 · Instance extent, nesting-aware (Defect B)
- **Given** an outer group instance containing a nested group with N > 1 entries,
- **When** `OffsetTable::group()`/`group_slices()` compute the outer instance extent,
- **Then** the extent encloses all N nested entries (no truncation at the 2nd nested entry), computed by reading the nested count and consuming exactly `declared` instances or failing closed (malformed/short/overflow; zero-count consumes no extent, preserving B-004-7), depth-bounded (`K=16`), and the computation performs **zero heap allocation**.
- **Overflow disposition**: nesting deeper than `K=16`, or a parent-path that cannot append, returns the **existing group-limit error** (`err_group_too_large`, `offset_table.cpp:477`) — observable + fail-closed, and **distinct** from the nested-count-mismatch validation (which stays the validator's job, B-004-7).

## C-3 · Real-dictionary multi-entry nested read (SC-001b, composes C-1 + C-2)
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
- **Given** all nine runtime XMLs **incl. FIXT.1.1**,
- **When** the loader-faithful (component-expanding) census runs,
- **Then** it enumerates every `NumInGroup` tag reused with differing membership (≥ the 12 FIX44 / 22 FIX50SP2 observed; + FIXT.1.1 session groups e.g. NoMsgTypes 384), **each with a discriminating regression guard** resolving to its context-correct variant. This is a **completeness/correctness aid**, not a soundness gate — a declaration-order census cannot bound the order-independent wire (B-004-1), so it neither adjudicates nor gates the design; Option A makes membership exact independently.

## Design decision (Gate A Round 1)
- **Resolved to Option A (exact context-scoped membership)**. Option B (union-per-no_tag) was rejected as unsound for the parser: a union false-positive is swallowed into the slice at `offset_table.cpp:447`, corrupting extent and nested-count detection; and the census cannot discharge the risk under order-independent acceptance. Contracts C-1…C-6 are stated for Option A.
